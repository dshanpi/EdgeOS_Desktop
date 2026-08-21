#include "dual_stream.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "mpi_sensor_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
#include "mpi_vicap_api.h"

namespace {
constexpr k_vicap_dev kRearDev = VICAP_DEV_ID_0;
constexpr k_vicap_dev kFrontDev = VICAP_DEV_ID_1;
constexpr k_vicap_chn kChannel = VICAP_CHN_ID_0;
constexpr int kRearCsi = 0;
constexpr int kFrontCsi = 2;
constexpr unsigned kFps = 30;
constexpr k_u32 kVencChannel = 0;

size_t y_storage(unsigned output_width, unsigned output_height) {
    return VB_ALIGN_UP(
        static_cast<size_t>(output_width) * output_height, 4096);
}

size_t frame_bytes(unsigned output_width, unsigned output_height) {
    return y_storage(output_width, output_height) +
           static_cast<size_t>(output_width) * output_height / 2;
}

size_t frame_block_bytes(unsigned output_width, unsigned output_height) {
    return VB_ALIGN_UP(
        frame_bytes(output_width, output_height) + 4095, 4096);
}

struct Nv12Map {
    const unsigned char *y{nullptr};
    const unsigned char *uv{nullptr};
    size_t y_bytes{0};
    size_t uv_bytes{0};
    unsigned y_stride{0};
    unsigned uv_stride{0};
};

int map_nv12(const k_video_frame_info &frame, unsigned source_width,
             unsigned source_height, Nv12Map &map) {
    map.y_stride = frame.v_frame.stride[0];
    map.uv_stride = frame.v_frame.stride[1];
    if (map.y_stride < source_width || map.y_stride > source_width * 2)
        map.y_stride = source_width;
    if (map.uv_stride < source_width ||
        map.uv_stride > source_width * 2)
        map.uv_stride = map.y_stride;
    map.y_bytes = static_cast<size_t>(map.y_stride) * source_height;
    map.uv_bytes = static_cast<size_t>(map.uv_stride) * source_height / 2;
    k_u64 y_physical = frame.v_frame.phys_addr[0];
    k_u64 uv_physical = frame.v_frame.phys_addr[1];
    if (uv_physical == 0)
        uv_physical = y_physical + VB_ALIGN_UP(map.y_bytes, 4096);
    map.y = static_cast<const unsigned char *>(
        kd_mpi_sys_mmap_cached(y_physical, map.y_bytes));
    map.uv = static_cast<const unsigned char *>(
        kd_mpi_sys_mmap_cached(uv_physical, map.uv_bytes));
    if (map.y == nullptr || map.uv == nullptr)
        return -1;
    if (kd_mpi_sys_mmz_invalidate_cache(y_physical,
                                        const_cast<unsigned char *>(map.y),
                                        map.y_bytes) != K_SUCCESS ||
        kd_mpi_sys_mmz_invalidate_cache(uv_physical,
                                        const_cast<unsigned char *>(map.uv),
                                        map.uv_bytes) != K_SUCCESS)
        return -1;
    return 0;
}

void unmap_nv12(Nv12Map &map) {
    if (map.uv != nullptr)
        kd_mpi_sys_munmap(const_cast<unsigned char *>(map.uv), map.uv_bytes);
    if (map.y != nullptr)
        kd_mpi_sys_munmap(const_cast<unsigned char *>(map.y), map.y_bytes);
    map = {};
}

int probe_sensor(int csi, k_vicap_sensor_info &info) {
    k_vicap_probe_config probe{};
    probe.csi_num = csi;
    probe.width = 1920;
    probe.height = 1080;
    probe.fps = kFps;
    if (kd_mpi_sensor_adapt_get(&probe, &info) != K_SUCCESS)
        return -1;
    return kd_mpi_vicap_get_sensor_info(info.sensor_type, &info) == K_SUCCESS
               ? 0
               : -1;
}

int configure_device(k_vicap_dev dev, int csi,
                     const k_vicap_sensor_info &info, unsigned source_width,
                     unsigned source_height) {
    k_vicap_dev_attr dev_attr{};
    dev_attr.acq_win.width = info.width;
    dev_attr.acq_win.height = info.height;
    dev_attr.mode = VICAP_WORK_OFFLINE_MODE;
    dev_attr.buffer_num = 4;
    dev_attr.buffer_size = VB_ALIGN_UP(info.width * info.height * 2U, 4096);
    dev_attr.buffer_pool_id = VB_INVALID_POOLID;
    dev_attr.pipe_ctrl.bits.ae_enable = 1;
    dev_attr.pipe_ctrl.bits.awb_enable = 1;
    /* The encoded side-by-side stream has no VO rotation.  Preserve the
     * corrected vertical direction without horizontally mirroring either
     * source. */
    dev_attr.mirror = csi == kFrontCsi ? VICAP_MIRROR_VER
                                       : VICAP_MIRROR_NONE;
    dev_attr.sensor_info = info;
    if (kd_mpi_vicap_set_dev_attr(dev, dev_attr) != K_SUCCESS)
        return -1;

    k_vicap_chn_attr channel{};
    channel.out_win.width = source_width;
    channel.out_win.height = source_height;
    channel.crop_enable = K_FALSE;
    channel.scale_enable = K_FALSE;
    channel.chn_enable = K_TRUE;
    channel.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    channel.buffer_num = 4;
    channel.buffer_size = VB_ALIGN_UP(
        source_width * source_height * 3U / 2U, 4096);
    channel.buffer_pool_id = VB_INVALID_POOLID;
    channel.alignment = 12;
    if (kd_mpi_vicap_set_chn_attr(dev, kChannel, channel) != K_SUCCESS)
        return -1;
    kd_mpi_vicap_set_dump_reserved(dev, kChannel, K_TRUE);
    return 0;
}
}  // namespace

class DualRtspPipeline::Impl {
public:
    int Init(IOnVEncData *sink, unsigned output_width,
             unsigned output_height, unsigned bitrate_kbps) {
        if (output_width < 640 || output_height < 360 ||
            (output_width & 1U) != 0 || (output_height & 1U) != 0)
            return -1;
        output_width_ = output_width;
        output_height_ = output_height;
        source_width_ = output_width / 2;
        source_height_ = output_height / 2;
        output_y_offset_ = (output_height - source_height_) / 2;
        bitrate_kbps_ = bitrate_kbps;
        sink_ = sink;
        k_vb_config vb{};
        vb.max_pool_cnt = 64;
        if (kd_mpi_vb_set_config(&vb) != K_SUCCESS ||
            kd_mpi_vb_init() != K_SUCCESS)
            return -1;
        vb_initialized_ = true;

        if (probe_sensor(kRearCsi, sensor_[0]) != 0 ||
            probe_sensor(kFrontCsi, sensor_[1]) != 0 ||
            configure_device(kRearDev, kRearCsi, sensor_[0], source_width_,
                             source_height_) != 0 ||
            configure_device(kFrontDev, kFrontCsi, sensor_[1], source_width_,
                             source_height_) != 0)
            return -1;
        if (kd_mpi_vicap_init(kRearDev) != K_SUCCESS)
            return -1;
        initialized_[0] = true;
        if (kd_mpi_vicap_init(kFrontDev) != K_SUCCESS)
            return -1;
        initialized_[1] = true;

        composite_pool_ = kd_mpi_vb_create_pool_ex(
            frame_block_bytes(output_width_, output_height_), 3,
            VB_REMAP_MODE_CACHED);
        venc_pool_ = kd_mpi_vb_create_pool_ex(
            VB_ALIGN_UP(static_cast<size_t>(output_width_) * output_height_,
                        4096),
            6,
            VB_REMAP_MODE_NOCACHE);
        if (composite_pool_ == VB_INVALID_POOLID ||
            venc_pool_ == VB_INVALID_POOLID ||
            kd_mpi_venc_attach_vb_pool(kVencChannel, venc_pool_) !=
                K_SUCCESS)
            return -1;
        venc_attached_ = true;

        k_venc_chn_attr attr{};
        attr.venc_attr.type = K_PT_H264;
        attr.venc_attr.profile = VENC_PROFILE_H264_HIGH;
        attr.venc_attr.pic_width = output_width_;
        attr.venc_attr.pic_height = output_height_;
        attr.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
        attr.rc_attr.cbr.src_frame_rate = kFps;
        attr.rc_attr.cbr.dst_frame_rate = kFps;
        attr.rc_attr.cbr.bit_rate = bitrate_kbps_;
        if (kd_mpi_venc_create_chn(kVencChannel, &attr) != K_SUCCESS)
            return -1;
        venc_created_ = true;
        kd_mpi_venc_enable_idr(kVencChannel, K_TRUE);
        return 0;
    }

    int Start() {
        if (kd_mpi_venc_start_chn(kVencChannel) != K_SUCCESS)
            return -1;
        venc_started_ = true;
        if (kd_mpi_vicap_start_stream(kRearDev) != K_SUCCESS)
            return -1;
        streaming_[0] = true;
        if (kd_mpi_vicap_start_stream(kFrontDev) != K_SUCCESS)
            return -1;
        streaming_[1] = true;
        running_.store(true);
        worker_ = std::thread(&Impl::Worker, this);
        std::printf("[rtsp-stream] dual source started: CSI0 | CSI2, "
                    "%ux%u H.264\n", output_width_, output_height_);
        return 0;
    }

    void DeInit() {
        running_.store(false);
        if (worker_.joinable())
            worker_.join();
        if (venc_started_) {
            kd_mpi_venc_stop_chn(kVencChannel);
            venc_started_ = false;
        }
        for (int index = 1; index >= 0; --index) {
            k_vicap_dev dev = index == 0 ? kRearDev : kFrontDev;
            if (streaming_[index]) {
                kd_mpi_vicap_stop_stream(dev);
                streaming_[index] = false;
            }
            if (initialized_[index]) {
                kd_mpi_vicap_deinit(dev);
                initialized_[index] = false;
            }
            kd_mpi_vicap_set_dump_reserved(dev, kChannel, K_FALSE);
        }
        if (venc_created_) {
            kd_mpi_venc_destroy_chn(kVencChannel);
            venc_created_ = false;
        }
        if (venc_attached_) {
            kd_mpi_venc_detach_vb_pool(kVencChannel);
            venc_attached_ = false;
        }
        if (venc_pool_ != VB_INVALID_POOLID) {
            kd_mpi_vb_destory_pool(venc_pool_);
            venc_pool_ = VB_INVALID_POOLID;
        }
        if (composite_pool_ != VB_INVALID_POOLID) {
            kd_mpi_vb_destory_pool(composite_pool_);
            composite_pool_ = VB_INVALID_POOLID;
        }
        kd_mpi_venc_close_fd();
        if (vb_initialized_) {
            kd_mpi_vb_exit();
            vb_initialized_ = false;
        }
        sink_ = nullptr;
    }

private:
    int AcquireComposite(k_video_frame_info &output,
                         k_vb_blk_handle &handle) {
        k_video_frame_info source[2]{};
        bool acquired[2]{false, false};
        Nv12Map map[2];
        unsigned char *destination = nullptr;
        k_u64 physical = 0;
        const size_t y_bytes = y_storage(output_width_, output_height_);
        int result = -1;
        handle = VB_INVALID_HANDLE;

        if (kd_mpi_vicap_dump_frame(kRearDev, kChannel, VICAP_DUMP_YUV,
                                    &source[0], 400) != K_SUCCESS)
            goto done;
        acquired[0] = true;
        if (kd_mpi_vicap_dump_frame(kFrontDev, kChannel, VICAP_DUMP_YUV,
                                    &source[1], 400) != K_SUCCESS)
            goto done;
        acquired[1] = true;
        if (map_nv12(source[0], source_width_, source_height_, map[0]) != 0 ||
            map_nv12(source[1], source_width_, source_height_, map[1]) != 0)
            goto done;

        handle = kd_mpi_vb_get_block(
            composite_pool_, frame_block_bytes(output_width_, output_height_),
            nullptr);
        if (handle == VB_INVALID_HANDLE)
            goto done;
        physical = VB_ALIGN_UP(kd_mpi_vb_handle_to_phyaddr(handle), 4096);
        destination = static_cast<unsigned char *>(
            kd_mpi_sys_mmap_cached(
                physical, frame_bytes(output_width_, output_height_)));
        if (physical == 0 || destination == nullptr)
            goto done;

        std::memset(destination, 16, y_bytes);
        std::memset(destination + y_bytes, 128,
                    static_cast<size_t>(output_width_) * output_height_ / 2);
        for (int camera = 0; camera < 2; ++camera) {
            unsigned x = camera == 0 ? 0 : source_width_;
            for (unsigned y = 0; y < source_height_; ++y)
                std::memcpy(destination +
                                static_cast<size_t>(output_y_offset_ + y) *
                                    output_width_ + x,
                            map[camera].y +
                                static_cast<size_t>(y) * map[camera].y_stride,
                            source_width_);
            unsigned char *destination_uv = destination + y_bytes;
            for (unsigned y = 0; y < source_height_ / 2; ++y)
                std::memcpy(destination_uv +
                                static_cast<size_t>(output_y_offset_ / 2 + y) *
                                    output_width_ + x,
                            map[camera].uv +
                                static_cast<size_t>(y) * map[camera].uv_stride,
                            source_width_);
        }
        if (kd_mpi_sys_mmz_flush_cache(physical, destination,
                                       frame_bytes(output_width_,
                                                   output_height_)) !=
            K_SUCCESS)
            goto done;

        output.pool_id = composite_pool_;
        output.mod_id = K_ID_VENC;
        output.v_frame.width = output_width_;
        output.v_frame.height = output_height_;
        output.v_frame.field = VIDEO_FIELD_FRAME;
        output.v_frame.pixel_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        output.v_frame.video_format = VIDEO_FORMAT_LINEAR;
        output.v_frame.dynamic_range = DYNAMIC_RANGE_SDR8;
        output.v_frame.compress_mode = COMPRESS_MODE_NONE;
        output.v_frame.color_gamut = COLOR_GAMUT_BT709;
        output.v_frame.stride[0] = output_width_;
        output.v_frame.stride[1] = output_width_;
        output.v_frame.phys_addr[0] = physical;
        output.v_frame.phys_addr[1] = physical + y_bytes;
        output.v_frame.virt_addr[0] =
            reinterpret_cast<k_u64>(destination);
        output.v_frame.virt_addr[1] =
            reinterpret_cast<k_u64>(destination + y_bytes);
        output.v_frame.pts = source[0].v_frame.pts;
        result = 0;

    done:
        unmap_nv12(map[1]);
        unmap_nv12(map[0]);
        if (acquired[1])
            kd_mpi_vicap_dump_release(kFrontDev, kChannel, &source[1]);
        if (acquired[0])
            kd_mpi_vicap_dump_release(kRearDev, kChannel, &source[0]);
        if (result != 0) {
            if (destination != nullptr)
                kd_mpi_sys_munmap(
                    destination, frame_bytes(output_width_, output_height_));
            if (handle != VB_INVALID_HANDLE)
                kd_mpi_vb_release_block(handle);
            handle = VB_INVALID_HANDLE;
        }
        return result;
    }

    void ReleaseComposite(k_video_frame_info &frame,
                          k_vb_blk_handle handle) {
        if (frame.v_frame.virt_addr[0] != 0)
            kd_mpi_sys_munmap(
                reinterpret_cast<void *>(frame.v_frame.virt_addr[0]),
                frame_bytes(output_width_, output_height_));
        if (handle != VB_INVALID_HANDLE)
            kd_mpi_vb_release_block(handle);
    }

    void DrainStream() {
        k_venc_chn_status status{};
        for (int wait = 0; running_.load() && wait < 250; ++wait) {
            if (kd_mpi_venc_query_status(kVencChannel, &status) == K_SUCCESS &&
                status.cur_packs > 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (status.cur_packs == 0)
            return;
        std::vector<k_venc_pack> packs(status.cur_packs);
        k_venc_stream stream{};
        stream.pack = packs.data();
        stream.pack_cnt = status.cur_packs;
        if (kd_mpi_venc_get_stream(kVencChannel, &stream, 500) != K_SUCCESS)
            return;
        for (k_u32 i = 0; i < stream.pack_cnt; ++i) {
            void *data = kd_mpi_sys_mmap(stream.pack[i].phys_addr,
                                         stream.pack[i].len);
            if (data != nullptr && sink_ != nullptr)
                sink_->OnVEncData(kVencChannel, data, stream.pack[i].len,
                                  stream.pack[i].type, stream.pack[i].pts);
            if (data != nullptr)
                kd_mpi_sys_munmap(data, stream.pack[i].len);
        }
        kd_mpi_venc_release_stream(kVencChannel, &stream);
    }

    void Worker() {
        kd_mpi_venc_request_idr(kVencChannel);
        while (running_.load()) {
            k_video_frame_info frame{};
            k_vb_blk_handle handle = VB_INVALID_HANDLE;
            if (AcquireComposite(frame, handle) != 0)
                continue;
            if (kd_mpi_venc_send_frame(kVencChannel, &frame, 500) ==
                K_SUCCESS)
                DrainStream();
            ReleaseComposite(frame, handle);
        }
    }

    IOnVEncData *sink_{nullptr};
    std::atomic<bool> running_{false};
    std::thread worker_;
    k_vicap_sensor_info sensor_[2]{};
    bool initialized_[2]{false, false};
    bool streaming_[2]{false, false};
    bool vb_initialized_{false};
    bool venc_attached_{false};
    bool venc_created_{false};
    bool venc_started_{false};
    k_u32 composite_pool_{VB_INVALID_POOLID};
    k_u32 venc_pool_{VB_INVALID_POOLID};
    unsigned output_width_{1280};
    unsigned output_height_{720};
    unsigned source_width_{640};
    unsigned source_height_{360};
    unsigned output_y_offset_{180};
    unsigned bitrate_kbps_{4000};
};

DualRtspPipeline::DualRtspPipeline() : impl_(std::make_unique<Impl>()) {}
DualRtspPipeline::~DualRtspPipeline() { impl_->DeInit(); }
int DualRtspPipeline::Init(IOnVEncData *sink, unsigned output_width,
                           unsigned output_height,
                           unsigned bitrate_kbps) {
    return impl_->Init(sink, output_width, output_height, bitrate_kbps);
}
int DualRtspPipeline::Start() { return impl_->Start(); }
void DualRtspPipeline::DeInit() { impl_->DeInit(); }
