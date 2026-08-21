#include "dual_camera_manager.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "camera_manager.h"
#include "camera_settings.h"
#include "kd_display.h"
#include "mp4_format.h"
#include "mpi_sensor_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
#include "mpi_vicap_api.h"

#define DUAL_REAR_DEV VICAP_DEV_ID_0
#define DUAL_FRONT_DEV VICAP_DEV_ID_1
#define DUAL_FULL_CHANNEL VICAP_CHN_ID_0
#define DUAL_PIP_CHANNEL VICAP_CHN_ID_1
#define DUAL_CAPTURE_CHANNEL VICAP_CHN_ID_2
#define DUAL_FULL_LAYER K_VO_LAYER_VIDEO1
#define DUAL_PIP_LAYER K_VO_LAYER_VIDEO2
#define DUAL_WIDTH 640U
#define DUAL_HEIGHT 480U
#define DUAL_FRONT_WIDTH 224U
#define DUAL_FRONT_HEIGHT 168U
#define DUAL_PIP_DEFAULT_X 392U
#define DUAL_PIP_DEFAULT_Y 24U
#define DUAL_PIP_RADIUS 22U
#define DUAL_PIP_BORDER 4U
#define DUAL_FPS 30U
#define DUAL_VENC_CH 0U

typedef struct {
    int initialized;
    int streaming;
} dual_device_state_t;

typedef struct {
    int bound;
    int layer_enabled;
    k_vicap_dev dev;
    k_vicap_chn channel;
    k_vo_layer_id layer;
} dual_layer_state_t;

static dual_device_state_t g_devices[2];
static dual_layer_state_t g_layers[2];
static k_vicap_sensor_info g_sensor_info[2];
static int g_sensor_valid[2];
static int g_started;

static k_u32 g_venc_pool = VB_INVALID_POOLID;
static k_u32 g_composite_pool = VB_INVALID_POOLID;
static int g_venc_attached;
static int g_venc_created;
static int g_venc_started;
static int g_recording;
static volatile int g_record_thread_running;
static pthread_t g_record_thread;
static KD_HANDLE g_mp4;
static KD_HANDLE g_mp4_track;
static char g_video_path[320];
static uint64_t g_mux_start_monotonic_us;
static uint64_t g_mux_last_timestamp_us;
static unsigned g_mux_frame_count;
static uint64_t g_record_capture_total_us;
static uint64_t g_record_encode_total_us;
static unsigned g_record_cycle_count;
static unsigned char *g_first_frame_data;
static size_t g_first_frame_size;
static int g_mux_started;
static unsigned g_video_sequence;
static unsigned g_photo_sequence;
static unsigned g_pip_ui_x = DUAL_PIP_DEFAULT_X;
static unsigned g_pip_ui_y = DUAL_PIP_DEFAULT_Y;
static int g_pip_locked;
static int g_front_primary;
static unsigned g_output_width = 1920U;
static unsigned g_output_height = 1080U;

static uint64_t dual_monotonic_us(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0;
    return (uint64_t)value.tv_sec * 1000000ULL +
           (uint64_t)value.tv_nsec / 1000ULL;
}

static size_t dual_y_plane_bytes(void)
{
    return (size_t)g_output_width * g_output_height;
}

static size_t dual_y_plane_storage(void)
{
    return VB_ALIGN_UP(dual_y_plane_bytes(), 4096);
}

static size_t dual_frame_bytes(void)
{
    return dual_y_plane_storage() + dual_y_plane_bytes() / 2U;
}

static size_t dual_frame_block_bytes(void)
{
    /* The private pool itself may start at a 1 KiB-aligned physical address.
     * Leave enough headroom to select a page-aligned address inside it. */
    return VB_ALIGN_UP(dual_frame_bytes() + 4095U, 4096);
}

static size_t dual_venc_stream_bytes(void)
{
    /* A high-quality 1080P JPEG key frame can exceed half a raw luma plane.
     * Reserve one byte per pixel so both JPEG and H.264 I-frames fit. */
    return (size_t)g_output_width * g_output_height;
}

/* VIDEO layers and Gallery playback are rotated 270 degrees.  Both placement
 * axes therefore use the inverse of the logical 640x480 UI coordinates. */
static unsigned dual_pip_raw_x(void)
{
    return DUAL_WIDTH - g_pip_ui_x - DUAL_FRONT_WIDTH;
}

static unsigned dual_pip_raw_y(void)
{
    return DUAL_HEIGHT - g_pip_ui_y - DUAL_FRONT_HEIGHT;
}

typedef struct {
    const unsigned char *y;
    const unsigned char *uv;
    size_t y_bytes;
    size_t uv_bytes;
    unsigned y_stride;
    unsigned uv_stride;
} dual_nv12_map_t;

static int dual_map_nv12(const k_video_frame_info *frame, unsigned width,
                         unsigned height, dual_nv12_map_t *map)
{
    k_u64 y_physical = frame->v_frame.phys_addr[0];
    unsigned y_stride = frame->v_frame.stride[0];
    unsigned uv_stride = frame->v_frame.stride[1];

    memset(map, 0, sizeof(*map));
    if (y_stride < width || y_stride > width * 2U)
        y_stride = width;
    if (uv_stride < width || uv_stride > width * 2U)
        uv_stride = y_stride;
    map->y_stride = y_stride;
    map->uv_stride = uv_stride;
    map->y_bytes = (size_t)y_stride * height;
    map->uv_bytes = (size_t)uv_stride * height / 2U;

    /*
     * VICAP aligns each plane independently.  In particular, a 224x168 Y
     * plane is 37632 bytes but its UV plane starts at the next 4 KiB boundary.
     * Never derive UV by adding width*height to the Y virtual address.
     */
    k_u64 uv_physical = frame->v_frame.phys_addr[1];
    if (uv_physical == 0)
        uv_physical = y_physical + VB_ALIGN_UP(map->y_bytes, 4096);
    /* VICAP writes physical memory while composition repeatedly reads every
     * pixel.  A cached mapping is substantially faster than uncached CPU
     * reads; invalidate it first so the CPU observes the latest frame. */
    map->y = kd_mpi_sys_mmap_cached(y_physical, map->y_bytes);
    map->uv = kd_mpi_sys_mmap_cached(uv_physical, map->uv_bytes);
    if (map->y == NULL || map->uv == NULL) {
        if (map->uv != NULL)
            kd_mpi_sys_munmap((void *)map->uv, map->uv_bytes);
        if (map->y != NULL)
            kd_mpi_sys_munmap((void *)map->y, map->y_bytes);
        memset(map, 0, sizeof(*map));
        return -1;
    }
    if (kd_mpi_sys_mmz_invalidate_cache(y_physical, (void *)map->y,
                                        map->y_bytes) != K_SUCCESS ||
        kd_mpi_sys_mmz_invalidate_cache(uv_physical, (void *)map->uv,
                                        map->uv_bytes) != K_SUCCESS) {
        kd_mpi_sys_munmap((void *)map->uv, map->uv_bytes);
        kd_mpi_sys_munmap((void *)map->y, map->y_bytes);
        memset(map, 0, sizeof(*map));
        return -1;
    }
    return 0;
}

static void dual_unmap_nv12(dual_nv12_map_t *map)
{
    if (map->uv != NULL)
        kd_mpi_sys_munmap((void *)map->uv, map->uv_bytes);
    if (map->y != NULL)
        kd_mpi_sys_munmap((void *)map->y, map->y_bytes);
    memset(map, 0, sizeof(*map));
}

static int dual_probe(int slot, int csi)
{
    k_vicap_probe_config probe = { 0 };
    k_vicap_sensor_info info = { 0 };
    k_s32 ret;

    if (g_sensor_valid[slot])
        return 0;
    probe.csi_num = csi;
    probe.width = 1920;
    probe.height = 1080;
    probe.fps = DUAL_FPS;
    ret = kd_mpi_sensor_adapt_get(&probe, &info);
    if (ret != K_SUCCESS) {
        printf("[dual-camera] CSI%d probe failed, ret=%d\n", csi, ret);
        return ret;
    }
    ret = kd_mpi_vicap_get_sensor_info(info.sensor_type, &info);
    if (ret != K_SUCCESS) {
        printf("[dual-camera] CSI%d sensor info failed, ret=%d\n", csi,
               ret);
        return ret;
    }
    g_sensor_info[slot] = info;
    g_sensor_valid[slot] = 1;
    printf("[dual-camera] CSI%d: %s, type=%d, %ux%u\n", csi,
           info.sensor_name, info.sensor_type, info.width, info.height);
    return 0;
}

static int dual_configure_channel(k_vicap_dev dev, k_vicap_chn channel,
                                  k_u32 width, k_u32 height)
{
    k_vicap_chn_attr chn_attr = { 0 };

    chn_attr.out_win.width = width;
    chn_attr.out_win.height = height;
    chn_attr.crop_enable = K_FALSE;
    chn_attr.scale_enable = K_FALSE;
    chn_attr.chn_enable = K_TRUE;
    chn_attr.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    chn_attr.buffer_num = 4;
    chn_attr.buffer_size = VB_ALIGN_UP(width * height * 3U / 2U, 4096);
    chn_attr.buffer_pool_id = VB_INVALID_POOLID;
    chn_attr.alignment = 12;
    k_s32 ret = kd_mpi_vicap_set_chn_attr(dev, channel, chn_attr);
    if (ret != K_SUCCESS)
        printf("[dual-camera] dev%d chn%d failed, ret=%d\n",
               dev, channel, ret);
    return ret;
}

static int dual_configure_device(int slot, k_vicap_dev dev, int csi)
{
    k_vicap_dev_attr dev_attr = { 0 };
    const k_vicap_sensor_info *info = &g_sensor_info[slot];
    k_s32 ret;

    dev_attr.acq_win.width = info->width;
    dev_attr.acq_win.height = info->height;
    /* Multiple physical sensors must run in the offline VICAP mode. */
    dev_attr.mode = VICAP_WORK_OFFLINE_MODE;
    dev_attr.buffer_num = 4;
    dev_attr.buffer_size = VB_ALIGN_UP(info->width * info->height * 2U,
                                       4096);
    dev_attr.buffer_pool_id = VB_INVALID_POOLID;
    dev_attr.pipe_ctrl.bits.ae_enable = 1;
    dev_attr.pipe_ctrl.bits.awb_enable = 1;
    dev_attr.mirror = csi == DSHANPI_CAMERA_FRONT_CSI
                          ? VICAP_MIRROR_HOR : VICAP_MIRROR_BOTH;
    dev_attr.sensor_info = *info;
    ret = kd_mpi_vicap_set_dev_attr(dev, dev_attr);
    if (ret != K_SUCCESS) {
        printf("[dual-camera] dev%d attributes failed, ret=%d\n", dev, ret);
        return ret;
    }

    if (dual_configure_channel(dev, DUAL_FULL_CHANNEL,
                               DUAL_WIDTH, DUAL_HEIGHT) != K_SUCCESS ||
        dual_configure_channel(dev, DUAL_PIP_CHANNEL,
                               DUAL_FRONT_WIDTH,
                               DUAL_FRONT_HEIGHT) != K_SUCCESS ||
        dual_configure_channel(dev, DUAL_CAPTURE_CHANNEL,
                               g_output_width,
                               g_output_height) != K_SUCCESS)
        return -1;
    /* Both channels can be dumped for recording while one is bound to VO. */
    kd_mpi_vicap_set_dump_reserved(dev, DUAL_FULL_CHANNEL, K_TRUE);
    kd_mpi_vicap_set_dump_reserved(dev, DUAL_PIP_CHANNEL, K_TRUE);
    kd_mpi_vicap_set_dump_reserved(dev, DUAL_CAPTURE_CHANNEL, K_TRUE);
    return 0;
}

static int dual_bind_layer(int index, k_vicap_dev dev, k_vo_layer_id layer,
                           k_vicap_chn channel, k_u32 width, k_u32 height,
                           k_u32 x, k_u32 y)
{
    k_mpp_chn source = { K_ID_VI, dev, channel };
    k_mpp_chn target = { K_ID_VO, K_VO_DISPLAY_DEV_ID, layer };

    if (kd_display_layer_configure(layer,
                                   PIXEL_FORMAT_YUV_SEMIPLANAR_420,
                                   width, height, x, y, 255,
                                   GDMA_ROTATE_DEGREE_270, 3, 2) !=
        K_SUCCESS) {
        printf("[dual-camera] VIDEO%d configure failed\n",
               layer - K_VO_LAYER_VIDEO0);
        return -1;
    }
    if (kd_display_layer_enable(layer) != K_SUCCESS) {
        printf("[dual-camera] VIDEO%d enable failed\n",
               layer - K_VO_LAYER_VIDEO0);
        return -1;
    }
    g_layers[index].layer_enabled = 1;
    g_layers[index].dev = dev;
    g_layers[index].channel = channel;
    g_layers[index].layer = layer;
    if (kd_mpi_sys_bind(&source, &target) != K_SUCCESS) {
        printf("[dual-camera] dev%d chn%d -> VIDEO%d bind failed\n",
               dev, channel, layer - K_VO_LAYER_VIDEO0);
        kd_display_layer_disable(layer);
        g_layers[index].layer_enabled = 0;
        return -1;
    }
    g_layers[index].bound = 1;
    return 0;
}

static void dual_unbind_layer(int index)
{
    dual_layer_state_t *state = &g_layers[index];
    k_mpp_chn source = { K_ID_VI, state->dev, state->channel };
    k_mpp_chn target = { K_ID_VO, K_VO_DISPLAY_DEV_ID, state->layer };

    if (state->bound) {
        kd_mpi_sys_unbind(&source, &target);
        state->bound = 0;
    }
    if (state->layer_enabled) {
        kd_display_layer_disable(state->layer);
        state->layer_enabled = 0;
    }
}

static int dual_bind_views(void)
{
    k_vicap_dev primary = g_front_primary ? DUAL_FRONT_DEV : DUAL_REAR_DEV;
    k_vicap_dev secondary = g_front_primary ? DUAL_REAR_DEV : DUAL_FRONT_DEV;

    if (dual_bind_layer(0, primary, DUAL_FULL_LAYER, DUAL_FULL_CHANNEL,
                        DUAL_WIDTH, DUAL_HEIGHT, 0, 0) != 0)
        return -1;
    if (dual_bind_layer(1, secondary, DUAL_PIP_LAYER, DUAL_PIP_CHANNEL,
                        DUAL_FRONT_WIDTH, DUAL_FRONT_HEIGHT,
                        dual_pip_raw_x(), dual_pip_raw_y()) != 0) {
        dual_unbind_layer(0);
        return -1;
    }
    return 0;
}

static int scaled_pip_inside(unsigned x, unsigned y, unsigned width,
                             unsigned height, unsigned radius,
                             unsigned inset)
{
    unsigned left = inset;
    unsigned top = inset;
    unsigned right = width - 1U - inset;
    unsigned bottom = height - 1U - inset;

    if (x < left || x > right || y < top || y > bottom)
        return 0;
    if (radius <= inset)
        return 1;
    radius -= inset;
    if ((x >= left + radius && x <= right - radius) ||
        (y >= top + radius && y <= bottom - radius))
        return 1;
    unsigned cx = x < left + radius ? left + radius : right - radius;
    unsigned cy = y < top + radius ? top + radius : bottom - radius;
    int dx = (int)x - (int)cx;
    int dy = (int)y - (int)cy;
    return dx * dx + dy * dy <= (int)(radius * radius);
}

static void composite_nv12(unsigned char *destination,
                           const k_video_frame_info *primary,
                           const k_video_frame_info *secondary,
                           unsigned secondary_width,
                           unsigned secondary_height)
{
    dual_nv12_map_t primary_map;
    dual_nv12_map_t secondary_map;
    int primary_mapped = dual_map_nv12(primary, g_output_width,
                                       g_output_height,
                                       &primary_map) == 0;
    int secondary_mapped =
        dual_map_nv12(secondary, secondary_width, secondary_height,
                      &secondary_map) == 0;
    unsigned pip_width = g_output_width * DUAL_FRONT_WIDTH / DUAL_WIDTH;
    unsigned pip_height = g_output_height * DUAL_FRONT_HEIGHT / DUAL_HEIGHT;
    unsigned pip_x = g_output_width * dual_pip_raw_x() / DUAL_WIDTH;
    unsigned pip_y = g_output_height * dual_pip_raw_y() / DUAL_HEIGHT;
    unsigned radius_x = pip_width * DUAL_PIP_RADIUS / DUAL_FRONT_WIDTH;
    unsigned radius_y = pip_height * DUAL_PIP_RADIUS / DUAL_FRONT_HEIGHT;
    unsigned radius = radius_x < radius_y ? radius_x : radius_y;
    unsigned border_x = pip_width * DUAL_PIP_BORDER / DUAL_FRONT_WIDTH;
    unsigned border_y = pip_height * DUAL_PIP_BORDER / DUAL_FRONT_HEIGHT;
    unsigned border = border_x < border_y ? border_x : border_y;
    unsigned source_x_map[DUAL_FRONT_WIDTH * 3U];
    pip_width &= ~1U;
    pip_height &= ~1U;
    pip_x &= ~1U;
    pip_y &= ~1U;
    if (border == 0)
        border = 1;

    if (pip_width > sizeof(source_x_map) / sizeof(source_x_map[0])) {
        memset(destination, 16, dual_y_plane_bytes());
        memset(destination + dual_y_plane_storage(), 128,
               dual_y_plane_bytes() / 2U);
        goto done;
    }
    for (unsigned x = 0; x < pip_width; ++x)
        source_x_map[x] = x * secondary_width / pip_width;

    if (!primary_mapped || !secondary_mapped) {
        memset(destination, 16, dual_y_plane_bytes());
        memset(destination + dual_y_plane_storage(), 128,
               dual_y_plane_bytes() / 2U);
        goto done;
    }

    for (unsigned y = 0; y < g_output_height; ++y)
        memcpy(destination + (size_t)y * g_output_width,
               primary_map.y + (size_t)y * primary_map.y_stride,
               g_output_width);
    unsigned char *destination_uv =
        destination + dual_y_plane_storage();
    for (unsigned y = 0; y < g_output_height / 2U; ++y)
        memcpy(destination_uv + (size_t)y * g_output_width,
               primary_map.uv + y * primary_map.uv_stride,
               g_output_width);

    /* Rounded PIP with a subtle neutral-white four-pixel frame. */
    for (unsigned y = 0; y < pip_height; ++y) {
        unsigned source_y = y * secondary_height / pip_height;
        for (unsigned x = 0; x < pip_width; ++x) {
            if (!scaled_pip_inside(x, y, pip_width, pip_height,
                                   radius, 0))
                continue;
            unsigned source_x = source_x_map[x];
            unsigned char value =
                scaled_pip_inside(x, y, pip_width, pip_height,
                                  radius, border)
                                      ? secondary_map.y[(size_t)source_y *
                                            secondary_map.y_stride + source_x]
                                      : 224;
            destination[(size_t)(pip_y + y) * g_output_width +
                        pip_x + x] = value;
        }
    }
    for (unsigned y = 0; y < pip_height / 2U; ++y) {
        unsigned source_y = y * secondary_height / pip_height;
        for (unsigned x = 0; x < pip_width; x += 2U) {
            unsigned pixel_y = y * 2U;
            if (!scaled_pip_inside(x, pixel_y, pip_width, pip_height,
                                   radius, 0))
                continue;
            unsigned source_x = source_x_map[x] & ~1U;
            size_t target = (size_t)(pip_y / 2U + y) * g_output_width +
                            pip_x + x;
            if (scaled_pip_inside(x, pixel_y, pip_width, pip_height,
                                  radius, border)) {
                destination_uv[target] =
                    secondary_map.uv[(size_t)source_y *
                                     secondary_map.uv_stride + source_x];
                destination_uv[target + 1U] =
                    secondary_map.uv[(size_t)source_y *
                                     secondary_map.uv_stride + source_x + 1U];
            } else {
                destination_uv[target] = 128;
                destination_uv[target + 1U] = 128;
            }
        }
    }

done:
    if (secondary_mapped)
        dual_unmap_nv12(&secondary_map);
    if (primary_mapped)
        dual_unmap_nv12(&primary_map);
}

static void dual_destroy_encoder(void)
{
    if (g_venc_started) {
        kd_mpi_venc_stop_chn(DUAL_VENC_CH);
        g_venc_started = 0;
    }
    if (g_venc_created) {
        kd_mpi_venc_destroy_chn(DUAL_VENC_CH);
        g_venc_created = 0;
    }
    if (g_venc_attached) {
        kd_mpi_venc_detach_vb_pool(DUAL_VENC_CH);
        g_venc_attached = 0;
    }
    if (g_venc_pool != VB_INVALID_POOLID) {
        kd_mpi_vb_destory_pool(g_venc_pool);
        g_venc_pool = VB_INVALID_POOLID;
    }
    /* Do not leave stale header/frame packs for the next JPEG/H.264 mode. */
    kd_mpi_venc_close_fd();
}

static int dual_configure_encoder(k_payload_type type)
{
    k_venc_chn_attr attr = { 0 };

    g_venc_pool = kd_mpi_vb_create_pool_ex(
        VB_ALIGN_UP(dual_venc_stream_bytes(), 4096), 5,
        VB_REMAP_MODE_NOCACHE);
    if (g_venc_pool == VB_INVALID_POOLID ||
        kd_mpi_venc_attach_vb_pool(DUAL_VENC_CH, g_venc_pool) != K_SUCCESS)
        return -1;
    g_venc_attached = 1;
    attr.venc_attr.type = type;
    attr.venc_attr.pic_width = g_output_width;
    attr.venc_attr.pic_height = g_output_height;
    if (type == K_PT_JPEG) {
        attr.rc_attr.rc_mode = K_VENC_RC_MODE_MJPEG_FIXQP;
        attr.rc_attr.mjpeg_fixqp.src_frame_rate = DUAL_FPS;
        attr.rc_attr.mjpeg_fixqp.dst_frame_rate = DUAL_FPS;
        attr.rc_attr.mjpeg_fixqp.q_factor = 88;
    } else {
        attr.venc_attr.profile = VENC_PROFILE_H264_HIGH;
        attr.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
        attr.rc_attr.cbr.src_frame_rate = DUAL_FPS;
        attr.rc_attr.cbr.dst_frame_rate = DUAL_FPS;
        attr.rc_attr.cbr.bit_rate =
            g_output_width >= 1920 ? 8000 :
            g_output_width >= 1280 ? 5000 : 3000;
    }
    if (kd_mpi_venc_create_chn(DUAL_VENC_CH, &attr) != K_SUCCESS)
        return -1;
    g_venc_created = 1;
    if (type == K_PT_H264)
        kd_mpi_venc_enable_idr(DUAL_VENC_CH, K_TRUE);
    if (kd_mpi_venc_start_chn(DUAL_VENC_CH) != K_SUCCESS)
        return -1;
    g_venc_started = 1;
    return 0;
}

static int dual_acquire_composite(k_video_frame_info *output,
                                  k_vb_blk_handle *handle)
{
    k_video_frame_info primary_frame = { 0 };
    k_video_frame_info secondary_frame = { 0 };
    int primary_acquired = 0;
    int secondary_acquired = 0;
    void *destination = NULL;
    int result = -1;
    k_vicap_dev primary = g_front_primary ? DUAL_FRONT_DEV : DUAL_REAR_DEV;
    k_vicap_dev secondary = g_front_primary ? DUAL_REAR_DEV : DUAL_FRONT_DEV;

    *handle = VB_INVALID_HANDLE;

    if (kd_mpi_vicap_dump_frame(primary, DUAL_CAPTURE_CHANNEL,
                                VICAP_DUMP_YUV, &primary_frame, 300) !=
        K_SUCCESS)
        goto done;
    primary_acquired = 1;
    /* The secondary view only occupies 35% of the final frame.  Reuse the
     * VICAP 640x480 hardware-scaled channel instead of reading and CPU-
     * downscaling a second full-resolution capture for every output frame. */
    if (kd_mpi_vicap_dump_frame(secondary, DUAL_FULL_CHANNEL,
                                VICAP_DUMP_YUV, &secondary_frame, 300) !=
        K_SUCCESS)
        goto done;
    secondary_acquired = 1;

    *handle = kd_mpi_vb_get_block(g_composite_pool,
                                  dual_frame_block_bytes(), NULL);
    if (*handle == VB_INVALID_HANDLE)
        goto done;
    k_u64 block_physical = kd_mpi_vb_handle_to_phyaddr(*handle);
    k_u64 physical = VB_ALIGN_UP(block_physical, 4096);
    destination = kd_mpi_sys_mmap_cached(physical, dual_frame_bytes());
    if (physical == 0 || destination == NULL)
        goto done;
    composite_nv12(destination, &primary_frame, &secondary_frame,
                   DUAL_WIDTH, DUAL_HEIGHT);
    if (kd_mpi_sys_mmz_flush_cache(physical, destination,
                                   dual_frame_bytes()) != K_SUCCESS)
        goto done;

    memset(output, 0, sizeof(*output));
    output->pool_id = g_composite_pool;
    output->mod_id = K_ID_VENC;
    output->v_frame.width = g_output_width;
    output->v_frame.height = g_output_height;
    output->v_frame.field = VIDEO_FIELD_FRAME;
    output->v_frame.pixel_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    output->v_frame.video_format = VIDEO_FORMAT_LINEAR;
    output->v_frame.dynamic_range = DYNAMIC_RANGE_SDR8;
    output->v_frame.compress_mode = COMPRESS_MODE_NONE;
    output->v_frame.color_gamut = COLOR_GAMUT_BT709;
    output->v_frame.stride[0] = g_output_width;
    output->v_frame.stride[1] = g_output_width;
    output->v_frame.phys_addr[0] = physical;
    output->v_frame.phys_addr[1] =
        physical + dual_y_plane_storage();
    output->v_frame.virt_addr[0] = (k_u64)(uintptr_t)destination;
    output->v_frame.virt_addr[1] =
        (k_u64)(uintptr_t)((unsigned char *)destination +
                           dual_y_plane_storage());
    output->v_frame.pts = primary_frame.v_frame.pts;
    result = 0;

done:
    if (result != 0 && destination != NULL)
        kd_mpi_sys_munmap(destination, dual_frame_bytes());
    if (secondary_acquired)
        kd_mpi_vicap_dump_release(secondary, DUAL_FULL_CHANNEL,
                                  &secondary_frame);
    if (primary_acquired)
        kd_mpi_vicap_dump_release(primary, DUAL_CAPTURE_CHANNEL,
                                  &primary_frame);
    if (result != 0 && *handle != VB_INVALID_HANDLE) {
        kd_mpi_vb_release_block(*handle);
        *handle = VB_INVALID_HANDLE;
    }
    return result;
}

static void dual_release_composite(k_video_frame_info *frame,
                                   k_vb_blk_handle handle)
{
    if (frame->v_frame.virt_addr[0] != 0)
        kd_mpi_sys_munmap((void *)(uintptr_t)frame->v_frame.virt_addr[0],
                          dual_frame_bytes());
    if (handle != VB_INVALID_HANDLE)
        kd_mpi_vb_release_block(handle);
}

static int dual_write_jpeg(const char *path)
{
    k_video_frame_info frame = { 0 };
    k_vb_blk_handle handle = VB_INVALID_HANDLE;
    k_venc_stream stream = { 0 };
    k_venc_pack packs[8] = { 0 };
    FILE *file = NULL;
    int result = -1;

    if (dual_configure_encoder(K_PT_JPEG) != 0 ||
        dual_acquire_composite(&frame, &handle) != 0)
        goto done;
    if (kd_mpi_venc_send_frame(DUAL_VENC_CH, &frame, 1000) != K_SUCCESS)
        goto done;
    /* VENC consumes user frames asynchronously; hold the VB block until the
     * encoded stream is returned, then release it below. */
    k_venc_chn_status status = { 0 };
    for (int wait = 0; wait < 150; ++wait) {
        if (kd_mpi_venc_query_status(DUAL_VENC_CH, &status) == K_SUCCESS &&
            status.cur_packs > 0)
            break;
        usleep(10 * 1000);
    }
    if (status.cur_packs == 0 ||
        status.cur_packs > sizeof(packs) / sizeof(packs[0]))
        goto done;
    stream.pack = packs;
    stream.pack_cnt = status.cur_packs;
    if (kd_mpi_venc_get_stream(DUAL_VENC_CH, &stream, 1500) != K_SUCCESS)
        goto done;
    file = fopen(path, "wb");
    if (file == NULL)
        goto release_stream;
    result = 0;
    for (k_u32 i = 0; i < stream.pack_cnt; ++i) {
        void *data = kd_mpi_sys_mmap(stream.pack[i].phys_addr,
                                     stream.pack[i].len);
        if (data == NULL ||
            fwrite(data, 1, stream.pack[i].len, file) !=
                stream.pack[i].len)
            result = -1;
        if (data != NULL)
            kd_mpi_sys_munmap(data, stream.pack[i].len);
    }
    if (fclose(file) != 0)
        result = -1;
    file = NULL;

release_stream:
    kd_mpi_venc_release_stream(DUAL_VENC_CH, &stream);
done:
    if (file != NULL)
        fclose(file);
    dual_release_composite(&frame, handle);
    dual_destroy_encoder();
    if (result != 0)
        unlink(path);
    return result;
}

static int dual_mux_stream(k_venc_stream *stream,
                           uint64_t capture_monotonic_us)
{
    for (k_u32 i = 0; i < stream->pack_cnt; ++i) {
        void *data = kd_mpi_sys_mmap(stream->pack[i].phys_addr,
                                     stream->pack[i].len);
        if (data != NULL) {
            k_mp4_frame_data_s output = { 0 };
            output.codec_id = K_MP4_CODEC_ID_H264;
            if (!g_mux_started) {
                size_t required = g_first_frame_size + stream->pack[i].len;
                unsigned char *combined = required <= 1024U * 1024U
                                              ? realloc(g_first_frame_data,
                                                        required)
                                              : NULL;
                if (combined != NULL) {
                    g_first_frame_data = combined;
                    memcpy(g_first_frame_data + g_first_frame_size, data,
                           stream->pack[i].len);
                    g_first_frame_size = required;
                }
                if (stream->pack[i].type == K_VENC_I_FRAME &&
                    g_first_frame_size > 0) {
                    g_mux_start_monotonic_us = capture_monotonic_us;
                    g_mux_last_timestamp_us = 0;
                    output.data = g_first_frame_data;
                    output.data_length = g_first_frame_size;
                    output.time_stamp = 0;
                    if (kd_mp4_write_frame(g_mp4, g_mp4_track, &output) ==
                        K_SUCCESS) {
                        g_mux_started = 1;
                        g_mux_frame_count = 1;
                    }
                    free(g_first_frame_data);
                    g_first_frame_data = NULL;
                    g_first_frame_size = 0;
                }
            } else {
                uint64_t timestamp_us =
                    capture_monotonic_us >= g_mux_start_monotonic_us
                        ? capture_monotonic_us - g_mux_start_monotonic_us
                        : g_mux_last_timestamp_us + 1000U;
                if (timestamp_us <= g_mux_last_timestamp_us)
                    timestamp_us = g_mux_last_timestamp_us + 1000U;
                output.data = data;
                output.data_length = stream->pack[i].len;
                output.time_stamp = timestamp_us;
                if (kd_mp4_write_frame(g_mp4, g_mp4_track, &output) ==
                    K_SUCCESS) {
                    g_mux_last_timestamp_us = timestamp_us;
                    ++g_mux_frame_count;
                }
            }
            kd_mpi_sys_munmap(data, stream->pack[i].len);
        }
    }
    return 0;
}

static int dual_drain_encoded_frame(uint64_t capture_monotonic_us)
{
    int encoded_frame_seen = 0;

    /* A newly-created H.264 channel may return a header separately. */
    for (int wait = 0; wait < 200 && !encoded_frame_seen; ++wait) {
        k_venc_chn_status status = { 0 };
        k_venc_stream stream = { 0 };
        k_venc_pack local_packs[8] = { 0 };
        k_venc_pack *packs = local_packs;

        if (kd_mpi_venc_query_status(DUAL_VENC_CH, &status) != K_SUCCESS ||
            status.cur_packs == 0) {
            usleep(2000);
            continue;
        }
        if (status.cur_packs > sizeof(local_packs) / sizeof(local_packs[0])) {
            packs = calloc(status.cur_packs, sizeof(*packs));
            if (packs == NULL)
                continue;
        }
        stream.pack_cnt = status.cur_packs;
        stream.pack = packs;
        if (kd_mpi_venc_get_stream(DUAL_VENC_CH, &stream, 500) == K_SUCCESS) {
            for (k_u32 i = 0; i < stream.pack_cnt; ++i) {
                if (stream.pack[i].type != K_VENC_HEADER)
                    encoded_frame_seen = 1;
            }
            dual_mux_stream(&stream, capture_monotonic_us);
            kd_mpi_venc_release_stream(DUAL_VENC_CH, &stream);
        }
        if (packs != local_packs)
            free(packs);
    }
    return encoded_frame_seen ? 0 : -1;
}

static void *dual_record_worker(void *argument)
{
    (void)argument;
    enum { DUAL_ENCODE_PIPELINE_DEPTH = 2 };
    k_video_frame_info pending_frames[DUAL_ENCODE_PIPELINE_DEPTH] = { 0 };
    k_vb_blk_handle pending_handles[DUAL_ENCODE_PIPELINE_DEPTH] = {
        VB_INVALID_HANDLE, VB_INVALID_HANDLE
    };
    uint64_t pending_capture_us[DUAL_ENCODE_PIPELINE_DEPTH] = { 0 };
    unsigned pending_head = 0;
    unsigned pending_count = 0;

    while (g_record_thread_running) {
        k_video_frame_info frame = { 0 };
        k_vb_blk_handle handle = VB_INVALID_HANDLE;
        uint64_t cycle_started_us = dual_monotonic_us();
        uint64_t capture_monotonic_us;

        if (dual_acquire_composite(&frame, &handle) != 0)
            continue;
        capture_monotonic_us = dual_monotonic_us();
        if (capture_monotonic_us >= cycle_started_us)
            g_record_capture_total_us +=
                capture_monotonic_us - cycle_started_us;

        /* Keep two VENC inputs in flight; the three-block composite pool
         * leaves one additional block for assembling the next frame. */
        if (pending_count == DUAL_ENCODE_PIPELINE_DEPTH) {
            uint64_t drain_started_us = dual_monotonic_us();
            dual_drain_encoded_frame(pending_capture_us[pending_head]);
            uint64_t drain_finished_us = dual_monotonic_us();
            if (drain_finished_us >= drain_started_us)
                g_record_encode_total_us +=
                    drain_finished_us - drain_started_us;
            dual_release_composite(&pending_frames[pending_head],
                                   pending_handles[pending_head]);
            memset(&pending_frames[pending_head], 0,
                   sizeof(pending_frames[pending_head]));
            pending_handles[pending_head] = VB_INVALID_HANDLE;
            pending_head =
                (pending_head + 1U) % DUAL_ENCODE_PIPELINE_DEPTH;
            --pending_count;
        }
        if (kd_mpi_venc_send_frame(DUAL_VENC_CH, &frame, 500) != K_SUCCESS) {
            dual_release_composite(&frame, handle);
            continue;
        }

        unsigned pending_tail =
            (pending_head + pending_count) % DUAL_ENCODE_PIPELINE_DEPTH;
        pending_frames[pending_tail] = frame;
        pending_handles[pending_tail] = handle;
        pending_capture_us[pending_tail] = capture_monotonic_us;
        ++pending_count;
        ++g_record_cycle_count;
    }

    while (pending_count > 0) {
        uint64_t drain_started_us = dual_monotonic_us();
        dual_drain_encoded_frame(pending_capture_us[pending_head]);
        uint64_t drain_finished_us = dual_monotonic_us();
        if (drain_finished_us >= drain_started_us)
            g_record_encode_total_us += drain_finished_us - drain_started_us;
        dual_release_composite(&pending_frames[pending_head],
                               pending_handles[pending_head]);
        pending_handles[pending_head] = VB_INVALID_HANDLE;
        pending_head =
            (pending_head + 1U) % DUAL_ENCODE_PIPELINE_DEPTH;
        --pending_count;
    }
    return NULL;
}

static int dual_make_paths(char *video, size_t video_size,
                           char *thumbnail, size_t thumbnail_size)
{
    struct tm now_tm;
    time_t now = time(NULL);
    int length;

    if (mkdir(DSHANPI_VIDEO_DIR, 0755) != 0 && errno != EEXIST)
        return -1;
    localtime_r(&now, &now_tm);
    ++g_video_sequence;
    length = snprintf(video, video_size,
                      DSHANPI_VIDEO_DIR
                      "/DUAL_%04d%02d%02d_%02d%02d%02d_%03u.mp4",
                      now_tm.tm_year + 1900, now_tm.tm_mon + 1,
                      now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min,
                      now_tm.tm_sec, g_video_sequence % 1000U);
    if (length <= 0 || (size_t)length >= video_size)
        return -1;
    length = snprintf(thumbnail, thumbnail_size, "%.*s.jpg",
                      (int)strlen(video) - 4, video);
    return length > 0 && (size_t)length < thumbnail_size ? 0 : -1;
}

static int dual_make_photo_path(char *path, size_t path_size)
{
    struct tm now_tm;
    time_t now = time(NULL);
    int length;

    if (mkdir(DSHANPI_PHOTO_DIR, 0755) != 0 && errno != EEXIST)
        return -1;
    localtime_r(&now, &now_tm);
    ++g_photo_sequence;
    length = snprintf(path, path_size,
                      DSHANPI_PHOTO_DIR
                      "/DUAL_IMG_%04d%02d%02d_%02d%02d%02d_%03u.jpg",
                      now_tm.tm_year + 1900, now_tm.tm_mon + 1,
                      now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min,
                      now_tm.tm_sec, g_photo_sequence % 1000U);
    return length > 0 && (size_t)length < path_size ? 0 : -1;
}

int dshanpi_dual_camera_start(int resolution)
{
    k_s32 ret;
    unsigned output_width;
    unsigned output_height;

    if (dshanpi_camera_resolution_dimensions(
            resolution, &output_width, &output_height) != 0)
        return -1;
    if (g_started && output_width == g_output_width &&
        output_height == g_output_height)
        return 0;
    if (g_started)
        dshanpi_dual_camera_stop();
    g_output_width = output_width;
    g_output_height = output_height;
    if (dual_probe(0, DSHANPI_CAMERA_REAR_CSI) != 0 ||
        dual_probe(1, DSHANPI_CAMERA_FRONT_CSI) != 0)
        return -1;
    if (dual_configure_device(0, DUAL_REAR_DEV,
                              DSHANPI_CAMERA_REAR_CSI) != 0 ||
        dual_configure_device(1, DUAL_FRONT_DEV,
                              DSHANPI_CAMERA_FRONT_CSI) != 0)
        goto fail;

    ret = kd_mpi_vicap_init(DUAL_REAR_DEV);
    if (ret != K_SUCCESS) {
        kd_mpi_vicap_deinit(DUAL_REAR_DEV);
        goto fail;
    }
    g_devices[0].initialized = 1;
    ret = kd_mpi_vicap_init(DUAL_FRONT_DEV);
    if (ret != K_SUCCESS) {
        kd_mpi_vicap_deinit(DUAL_FRONT_DEV);
        goto fail;
    }
    g_devices[1].initialized = 1;

    g_composite_pool = kd_mpi_vb_create_pool_ex(
        dual_frame_block_bytes(), 3, VB_REMAP_MODE_CACHED);
    if (g_composite_pool == VB_INVALID_POOLID)
        goto fail;

    if (kd_mpi_vicap_start_stream(DUAL_REAR_DEV) != K_SUCCESS)
        goto fail;
    g_devices[0].streaming = 1;
    if (kd_mpi_vicap_start_stream(DUAL_FRONT_DEV) != K_SUCCESS)
        goto fail;
    g_devices[1].streaming = 1;

    if (dual_bind_views() != 0)
        goto fail;

    g_started = 1;
    g_pip_locked = 0;
    printf("[dual-camera] ready: %s full screen, %s PIP at (%u,%u), "
           "output=%ux%u\n",
           g_front_primary ? "front CSI2" : "rear CSI0",
           g_front_primary ? "rear CSI0" : "front CSI2",
           g_pip_ui_x, g_pip_ui_y, g_output_width, g_output_height);
    return 0;

fail:
    dshanpi_dual_camera_stop();
    return -1;
}

void dshanpi_dual_camera_stop(void)
{
    if (!g_started && !g_recording &&
        !g_devices[0].initialized && !g_devices[1].initialized &&
        !g_devices[0].streaming && !g_devices[1].streaming &&
        g_composite_pool == VB_INVALID_POOLID &&
        g_venc_pool == VB_INVALID_POOLID)
        return;
    if (g_recording)
        dshanpi_dual_camera_record_stop();
    dual_unbind_layer(1);
    dual_unbind_layer(0);
    usleep(50 * 1000);
    for (int index = 1; index >= 0; --index) {
        k_vicap_dev dev = index == 0 ? DUAL_REAR_DEV : DUAL_FRONT_DEV;
        if (g_devices[index].streaming) {
            kd_mpi_vicap_stop_stream(dev);
            g_devices[index].streaming = 0;
        }
    }
    for (int index = 1; index >= 0; --index) {
        k_vicap_dev dev = index == 0 ? DUAL_REAR_DEV : DUAL_FRONT_DEV;
        if (g_devices[index].initialized) {
            kd_mpi_vicap_deinit(dev);
            g_devices[index].initialized = 0;
        }
        kd_mpi_vicap_set_dump_reserved(dev, DUAL_FULL_CHANNEL, K_FALSE);
        kd_mpi_vicap_set_dump_reserved(dev, DUAL_PIP_CHANNEL, K_FALSE);
        kd_mpi_vicap_set_dump_reserved(dev, DUAL_CAPTURE_CHANNEL, K_FALSE);
    }
    dual_destroy_encoder();
    if (g_composite_pool != VB_INVALID_POOLID) {
        kd_mpi_vb_destory_pool(g_composite_pool);
        g_composite_pool = VB_INVALID_POOLID;
    }
    g_started = 0;
    g_pip_locked = 0;
    usleep(300 * 1000);
}

int dshanpi_dual_camera_set_pip_position(unsigned x, unsigned y)
{
    if (!g_started || g_pip_locked || g_recording ||
        x > DUAL_WIDTH - DUAL_FRONT_WIDTH ||
        y > DUAL_HEIGHT - DUAL_FRONT_HEIGHT)
        return -1;

    /* NV12 chroma and the VO layer require an even origin. */
    x &= ~1U;
    y &= ~1U;
    unsigned vo_x = DUAL_WIDTH - x - DUAL_FRONT_WIDTH;
    unsigned vo_y = DUAL_HEIGHT - y - DUAL_FRONT_HEIGHT;
    if (kd_display_layer_update_position(DUAL_PIP_LAYER, vo_x, vo_y) !=
        K_SUCCESS)
        return -1;
    g_pip_ui_x = x;
    g_pip_ui_y = y;
    return 0;
}

int dshanpi_dual_camera_swap_views(void)
{
    if (!g_started || g_pip_locked || g_recording)
        return -1;

    g_pip_locked = 1;
    dual_unbind_layer(1);
    dual_unbind_layer(0);
    g_front_primary = !g_front_primary;
    if (dual_bind_views() != 0) {
        g_front_primary = !g_front_primary;
        dual_unbind_layer(1);
        dual_unbind_layer(0);
        dual_bind_views();
        g_pip_locked = 0;
        return -1;
    }
    g_pip_locked = 0;
    printf("[dual-camera] views swapped: %s full screen, %s PIP\n",
           g_front_primary ? "front CSI2" : "rear CSI0",
           g_front_primary ? "rear CSI0" : "front CSI2");
    return 0;
}

int dshanpi_dual_camera_capture_jpeg(char *output_path,
                                     size_t output_path_size)
{
    int result;

    if (!g_started || g_recording || g_pip_locked || output_path == NULL ||
        output_path_size == 0 ||
        dual_make_photo_path(output_path, output_path_size) != 0)
        return -1;

    /* Freeze PIP movement/view swapping while both source frames are dumped
     * and composited into the still image. */
    g_pip_locked = 1;
    result = dual_write_jpeg(output_path);
    g_pip_locked = 0;
    if (result != 0) {
        output_path[0] = '\0';
        return -1;
    }

    printf("[dual-camera] photo saved: %s (%s full screen, PIP %u,%u)\n",
           output_path, g_front_primary ? "front CSI2" : "rear CSI0",
           g_pip_ui_x, g_pip_ui_y);
    return 0;
}

int dshanpi_dual_camera_record_start(char *output_path,
                                     size_t output_path_size)
{
    char thumbnail[320];
    k_mp4_config_s config = { 0 };
    k_mp4_track_info_s track = { 0 };

    if (!g_started || g_recording || output_path == NULL ||
        output_path_size == 0 ||
        dual_make_paths(g_video_path, sizeof(g_video_path), thumbnail,
                        sizeof(thumbnail)) != 0)
        return -1;

    g_pip_locked = 1;

    if (dual_write_jpeg(thumbnail) != 0)
        printf("[dual-camera] thumbnail capture failed: %s\n", thumbnail);
    if (dual_configure_encoder(K_PT_H264) != 0)
        goto fail;
    config.config_type = K_MP4_CONFIG_MUXER;
    config.muxer_config.fmp4_flag = 1;
    if (strlen(g_video_path) >= sizeof(config.muxer_config.file_name))
        goto fail;
    memcpy(config.muxer_config.file_name, g_video_path,
           strlen(g_video_path) + 1U);
    if (kd_mp4_create(&g_mp4, &config) < 0)
        goto fail;
    track.track_type = K_MP4_STREAM_VIDEO;
    /* kd_mp4_write_frame accepts microsecond timestamps and stores a
     * millisecond timeline.  Keep the track on the SDK's standard 1 kHz
     * timescale, matching the reference muxer and the regular Camera app. */
    track.time_scale = 1000;
    track.video_info.width = g_output_width;
    track.video_info.height = g_output_height;
    track.video_info.codec_id = K_MP4_CODEC_ID_H264;
    if (kd_mp4_create_track(g_mp4, &g_mp4_track, &track) < 0)
        goto fail;

    g_mux_start_monotonic_us = 0;
    g_mux_last_timestamp_us = 0;
    g_mux_frame_count = 0;
    g_record_capture_total_us = 0;
    g_record_encode_total_us = 0;
    g_record_cycle_count = 0;
    free(g_first_frame_data);
    g_first_frame_data = NULL;
    g_first_frame_size = 0;
    g_mux_started = 0;
    g_record_thread_running = 1;
    kd_mpi_venc_request_idr(DUAL_VENC_CH);
    if (pthread_create(&g_record_thread, NULL, dual_record_worker, NULL) != 0)
        goto fail;
    g_recording = 1;
    snprintf(output_path, output_path_size, "%s", g_video_path);
    printf("[dual-camera] recording started: %s\n", g_video_path);
    return 0;

fail:
    g_record_thread_running = 0;
    if (g_mp4 != NULL) {
        kd_mp4_destroy_tracks(g_mp4);
        kd_mp4_destroy(g_mp4);
        g_mp4 = NULL;
        g_mp4_track = NULL;
    }
    dual_destroy_encoder();
    unlink(g_video_path);
    g_pip_locked = 0;
    return -1;
}

int dshanpi_dual_camera_record_stop(void)
{
    int result;

    if (!g_recording)
        return 0;
    g_record_thread_running = 0;
    pthread_join(g_record_thread, NULL);
    result = g_mux_started ? 0 : -1;
    free(g_first_frame_data);
    g_first_frame_data = NULL;
    g_first_frame_size = 0;
    dual_destroy_encoder();
    kd_mp4_destroy_tracks(g_mp4);
    kd_mp4_destroy(g_mp4);
    g_mp4 = NULL;
    g_mp4_track = NULL;
    g_recording = 0;
    g_pip_locked = 0;
    if (result == 0) {
        uint64_t duration_ms = g_mux_last_timestamp_us / 1000U;
        unsigned fps_tenths = duration_ms > 0
                                  ? (unsigned)((uint64_t)g_mux_frame_count *
                                               10000U / duration_ms)
                                  : 0;
        unsigned capture_average_us = g_record_cycle_count > 0
                                          ? (unsigned)(
                                                g_record_capture_total_us /
                                                g_record_cycle_count)
                                          : 0;
        unsigned encode_average_us = g_record_cycle_count > 0
                                         ? (unsigned)(
                                               g_record_encode_total_us /
                                               g_record_cycle_count)
                                         : 0;
        printf("[dual-camera] recording saved: %s (frames=%u, "
               "duration=%llu ms, fps=%u.%u, capture=%u us, "
               "encode=%u us)\n",
               g_video_path, g_mux_frame_count,
               (unsigned long long)duration_ms, fps_tenths / 10U,
               fps_tenths % 10U, capture_average_us, encode_average_us);
    } else {
        unlink(g_video_path);
        printf("[dual-camera] recording discarded: no encoded frames\n");
    }
    return result;
}

int dshanpi_dual_camera_is_recording(void)
{
    return g_recording;
}
