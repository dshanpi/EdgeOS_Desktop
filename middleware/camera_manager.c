#include "camera_manager.h"
#include "camera_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "mpi_sensor_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
#include "mpi_vicap_api.h"
#include "kd_display.h"
#include "mp4_format.h"

#define PREVIEW_WIDTH  640
#define PREVIEW_HEIGHT 480
#define SENSOR_WIDTH   1920
#define SENSOR_HEIGHT  1080
#define CAPTURE_FPS    30
#define PHOTO_VENC_CH 0
#define CAMERA_VO_LAYER K_VO_LAYER_VIDEO1
#define CAMERA_COUNT 2

static unsigned g_photo_sequence;
static k_u32 g_venc_pool = VB_INVALID_POOLID;
static int g_vicap_initialized[CAMERA_COUNT];
static int g_vicap_started[CAMERA_COUNT];
static int g_venc_attached;
static int g_venc_created;
static int g_venc_started;
static int g_recording;
static int g_record_thread_running;
static pthread_t g_record_thread;
static KD_HANDLE g_mp4;
static KD_HANDLE g_mp4_video_track;
static uint64_t g_record_first_pts;
static char g_video_path[320];
static int g_camera_started;
static int g_active_csi = -1;
static int g_active_dev = -1;
static unsigned g_capture_width = SENSOR_WIDTH;
static unsigned g_capture_height = SENSOR_HEIGHT;
static int g_vo_layer_enabled;
static int g_vo_bound;
static int g_bound_dev = -1;
static k_vicap_sensor_info g_sensor_info[3];
static int g_sensor_info_valid[3];

static int probe_camera(int csi)
{
    k_vicap_probe_config probe = { 0 };
    k_vicap_sensor_info sensor_info = { 0 };
    k_s32 ret;

    if (g_sensor_info_valid[csi]) {
        return 0;
    }

    probe.csi_num = csi;
    probe.width = SENSOR_WIDTH;
    probe.height = SENSOR_HEIGHT;
    probe.fps = CAPTURE_FPS;
    ret = kd_mpi_sensor_adapt_get(&probe, &sensor_info);
    if (ret != K_SUCCESS) {
        printf("[camera] GC2093 probe failed on CSI%d, ret=%d\n", csi, ret);
        return ret;
    }
    ret = kd_mpi_vicap_get_sensor_info(sensor_info.sensor_type, &sensor_info);
    if (ret != K_SUCCESS) {
        printf("[camera] get CSI%d sensor info failed, ret=%d\n", csi, ret);
        return ret;
    }

    memcpy(&g_sensor_info[csi], &sensor_info, sizeof(sensor_info));
    g_sensor_info_valid[csi] = 1;
    printf("[camera] cached CSI%d sensor: %s, type=%d, %ux%u\n",
           csi, sensor_info.sensor_name, sensor_info.sensor_type,
           sensor_info.width, sensor_info.height);
    return 0;
}

static int bind_camera_to_vo(int dev)
{
    k_mpp_chn vicap_chn = {
        .mod_id = K_ID_VI,
        .dev_id = dev,
        .chn_id = DSHANPI_CAMERA_PREVIEW_CH,
    };
    k_mpp_chn vo_chn = {
        .mod_id = K_ID_VO,
        .dev_id = K_VO_DISPLAY_DEV_ID,
        .chn_id = CAMERA_VO_LAYER,
    };

    if (kd_display_layer_configure(
            CAMERA_VO_LAYER, PIXEL_FORMAT_YUV_SEMIPLANAR_420,
            PREVIEW_WIDTH, PREVIEW_HEIGHT, 0, 0, 255,
            GDMA_ROTATE_DEGREE_270, 2, 2) != K_SUCCESS) {
        printf("[camera] VIDEO1 configure failed\n");
        return -1;
    }
    if (kd_display_layer_enable(CAMERA_VO_LAYER) != K_SUCCESS) {
        printf("[camera] VIDEO1 enable failed\n");
        return -1;
    }
    g_vo_layer_enabled = 1;

    if (kd_mpi_sys_bind(&vicap_chn, &vo_chn) != K_SUCCESS) {
        printf("[camera] VICAP CHN0 -> VIDEO1 bind failed\n");
        kd_display_layer_disable(CAMERA_VO_LAYER);
        g_vo_layer_enabled = 0;
        return -1;
    }
    g_vo_bound = 1;
    g_bound_dev = dev;
    printf("[camera] VICAP dev%d ch0 bound to VO VIDEO1\n", dev);
    return 0;
}

static void unbind_camera_from_vo(void)
{
    k_mpp_chn vicap_chn = {
        .mod_id = K_ID_VI,
        .dev_id = g_bound_dev,
        .chn_id = DSHANPI_CAMERA_PREVIEW_CH,
    };
    k_mpp_chn vo_chn = {
        .mod_id = K_ID_VO,
        .dev_id = K_VO_DISPLAY_DEV_ID,
        .chn_id = CAMERA_VO_LAYER,
    };

    if (g_vo_bound) {
        kd_mpi_sys_unbind(&vicap_chn, &vo_chn);
        g_vo_bound = 0;
        g_bound_dev = -1;
    }
    if (g_vo_layer_enabled) {
        kd_display_layer_disable(CAMERA_VO_LAYER);
        g_vo_layer_enabled = 0;
    }
}

static int make_photo_path(char *path, size_t path_size)
{
    struct tm now_tm;
    time_t now = time(NULL);

    if (mkdir(DSHANPI_PHOTO_DIR, 0755) != 0 && errno != EEXIST) {
        printf("[camera] mkdir %s failed: %s\n", DSHANPI_PHOTO_DIR,
               strerror(errno));
        return -1;
    }

    localtime_r(&now, &now_tm);
    ++g_photo_sequence;
    int length = snprintf(
        path, path_size,
        DSHANPI_PHOTO_DIR "/IMG_%04d%02d%02d_%02d%02d%02d_%03u.jpg",
        now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
        now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
        g_photo_sequence % 1000);
    return length > 0 && (size_t)length < path_size ? 0 : -1;
}

static int make_video_path(char *path, size_t path_size)
{
    struct tm now_tm;
    time_t now = time(NULL);
    if (mkdir(DSHANPI_VIDEO_DIR, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    localtime_r(&now, &now_tm);
    int length = snprintf(
        path, path_size,
        DSHANPI_VIDEO_DIR "/VID_%04d%02d%02d_%02d%02d%02d.mp4",
        now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
        now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
    return length > 0 && (size_t)length < path_size ? 0 : -1;
}

static int configure_vicap_attrs(int dev, int csi)
{
    k_vicap_sensor_info sensor_info = { 0 };
    k_vicap_dev_attr dev_attr = { 0 };
    k_vicap_chn_attr chn_attr = { 0 };
    k_s32 ret;

    if (!g_sensor_info_valid[csi]) {
        printf("[camera] CSI%d was not found during initial probe\n", csi);
        return -1;
    }
    memcpy(&sensor_info, &g_sensor_info[csi], sizeof(sensor_info));
    printf("[camera] using cached CSI%d sensor info\n", csi);

    dev_attr.acq_win.width = sensor_info.width;
    dev_attr.acq_win.height = sensor_info.height;
    dev_attr.mode = VICAP_WORK_ONLINE_MODE;
    dev_attr.buffer_num = 6;
    dev_attr.buffer_size =
        VB_ALIGN_UP(sensor_info.width * sensor_info.height * 2, 4096);
    dev_attr.buffer_pool_id = VB_INVALID_POOLID;
    dev_attr.pipe_ctrl.bits.ae_enable = 1;
    dev_attr.pipe_ctrl.bits.awb_enable = 1;
    /*
     * The two GC2093 modules are mounted differently.  CSI0 needs a
     * 180-degree (both-axis) correction, while CSI2 only needs the
     * front-camera horizontal mirror.  Using BOTH for CSI2 leaves its image
     * vertically inverted.
     */
    dev_attr.mirror = csi == DSHANPI_CAMERA_FRONT_CSI
                          ? VICAP_MIRROR_HOR
                          : VICAP_MIRROR_BOTH;
    memcpy(&dev_attr.sensor_info, &sensor_info, sizeof(sensor_info));
    ret = kd_mpi_vicap_set_dev_attr((k_vicap_dev)dev, dev_attr);
    if (ret != K_SUCCESS) {
        printf("[camera] set dev%d attributes failed, ret=%d\n", dev, ret);
        return ret;
    }

    chn_attr.out_win.width = PREVIEW_WIDTH;
    chn_attr.out_win.height = PREVIEW_HEIGHT;
    /*
     * Match CanMV Sensor.set_framesize(640, 480) exactly.  VICAP derives the
     * output size from out_win; explicitly enabling the scaler here corrupts
     * the CSI2 1280x960 NV12 layout in multi-camera/offline mode.
     */
    chn_attr.crop_enable = K_FALSE;
    chn_attr.scale_enable = K_FALSE;
    chn_attr.chn_enable = K_TRUE;
    chn_attr.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    chn_attr.buffer_num = 6;
    chn_attr.buffer_size =
        VB_ALIGN_UP(PREVIEW_WIDTH * PREVIEW_HEIGHT * 3 / 2, 4096);
    chn_attr.buffer_pool_id = VB_INVALID_POOLID;
    chn_attr.alignment = 0;
    ret = kd_mpi_vicap_set_chn_attr((k_vicap_dev)dev,
                                    DSHANPI_CAMERA_PREVIEW_CH,
                                    chn_attr);
    if (ret != K_SUCCESS) {
        printf("[camera] set dev%d channel attributes failed, ret=%d\n",
               dev, ret);
        return ret;
    }
    /* Channel 1 is permanently reserved for hot-pluggable AI algorithms. */
    memset(&chn_attr, 0, sizeof(chn_attr));
    chn_attr.out_win.width = PREVIEW_WIDTH;
    chn_attr.out_win.height = PREVIEW_HEIGHT;
    chn_attr.chn_enable = K_TRUE;
    chn_attr.pix_format = PIXEL_FORMAT_RGB_888_PLANAR;
    chn_attr.buffer_num = 4;
    chn_attr.buffer_size =
        VB_ALIGN_UP(PREVIEW_WIDTH * PREVIEW_HEIGHT * 3, 4096);
    chn_attr.buffer_pool_id = VB_INVALID_POOLID;
    ret = kd_mpi_vicap_set_chn_attr((k_vicap_dev)dev,
                                    DSHANPI_CAMERA_AI_CH, chn_attr);
    if (ret != K_SUCCESS) {
        printf("[camera-manager] configure AI channel failed, ret=%d\n", ret);
        return ret;
    }

    /* Channel 2 is the real JPEG/MP4 source. Its dimensions follow the
     * selected 640P/720P/1080P output setting, independently of preview. */
    memset(&chn_attr, 0, sizeof(chn_attr));
    chn_attr.out_win.width = g_capture_width;
    chn_attr.out_win.height = g_capture_height;
    chn_attr.chn_enable = K_TRUE;
    chn_attr.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    chn_attr.buffer_num = 4;
    chn_attr.buffer_size =
        VB_ALIGN_UP(g_capture_width * g_capture_height * 3 / 2, 4096);
    chn_attr.buffer_pool_id = VB_INVALID_POOLID;
    /*
     * VENC imports VICAP frames through the MPP binding path and requires
     * their physical base address to be page aligned.  This field is the
     * log2 alignment used by VICAP (12 == 4096 bytes); aligning only
     * buffer_size does not align the address of each produced frame.
     */
    chn_attr.alignment = 12;
    ret = kd_mpi_vicap_set_chn_attr((k_vicap_dev)dev,
                                    DSHANPI_CAMERA_CAPTURE_CH, chn_attr);
    if (ret != K_SUCCESS) {
        printf("[camera-manager] configure capture channel failed, ret=%d\n",
               ret);
        return ret;
    }

    printf("[camera-manager] channels: preview=640x480, ai=640x480, "
           "capture=%ux%u\n", g_capture_width, g_capture_height);
    return 0;
}

static int configure_encoder(void)
{
    k_venc_chn_attr attr = { 0 };

    g_venc_pool = kd_mpi_vb_create_pool_ex(
        VB_ALIGN_UP(g_capture_width * g_capture_height, 4096), 4,
        VB_REMAP_MODE_NOCACHE);
    if (g_venc_pool == VB_INVALID_POOLID) {
        printf("[camera] VENC buffer pool creation failed\n");
        return -1;
    }
    if (kd_mpi_venc_attach_vb_pool(PHOTO_VENC_CH, g_venc_pool) !=
        K_SUCCESS) {
        printf("[camera] VENC buffer pool attach failed\n");
        return -1;
    }
    g_venc_attached = 1;

    attr.venc_attr.type = K_PT_JPEG;
    attr.venc_attr.pic_width = g_capture_width;
    attr.venc_attr.pic_height = g_capture_height;
    attr.rc_attr.rc_mode = K_VENC_RC_MODE_MJPEG_FIXQP;
    attr.rc_attr.mjpeg_fixqp.src_frame_rate = CAPTURE_FPS;
    attr.rc_attr.mjpeg_fixqp.dst_frame_rate = CAPTURE_FPS;
    attr.rc_attr.mjpeg_fixqp.q_factor = 90;
    if (kd_mpi_venc_create_chn(PHOTO_VENC_CH, &attr) != K_SUCCESS) {
        printf("[camera] JPEG encoder creation failed\n");
        return -1;
    }
    g_venc_created = 1;
    /*
     * Store still images in the same upright orientation as recorded video,
     * rather than relying on Gallery-only display transforms.  A 180-degree
     * rotation keeps the JPEG dimensions unchanged and is handled by VPU
     * before the bitstream is written.
     */
    if (kd_mpi_venc_set_rotation(PHOTO_VENC_CH,
                                 K_VPU_ROTATION_180) != K_SUCCESS) {
        printf("[camera] JPEG encoder rotation configuration failed\n");
        return -1;
    }
    if (kd_mpi_venc_start_chn(PHOTO_VENC_CH) != K_SUCCESS) {
        printf("[camera] JPEG encoder start failed\n");
        return -1;
    }
    g_venc_started = 1;
    return 0;
}

static void destroy_encoder(void)
{
    if (g_venc_started) {
        kd_mpi_venc_stop_chn(PHOTO_VENC_CH);
        g_venc_started = 0;
    }
    if (g_venc_created) {
        kd_mpi_venc_destroy_chn(PHOTO_VENC_CH);
        g_venc_created = 0;
    }
    if (g_venc_attached) {
        kd_mpi_venc_detach_vb_pool(PHOTO_VENC_CH);
        g_venc_attached = 0;
    }
    if (g_venc_pool != VB_INVALID_POOLID) {
        kd_mpi_vb_destory_pool(g_venc_pool);
        g_venc_pool = VB_INVALID_POOLID;
    }
}

static int configure_video_encoder(void)
{
    k_venc_chn_attr attr = { 0 };
    g_venc_pool = kd_mpi_vb_create_pool_ex(
        VB_ALIGN_UP(g_capture_width * g_capture_height, 4096), 6,
        VB_REMAP_MODE_NOCACHE);
    if (g_venc_pool == VB_INVALID_POOLID ||
        kd_mpi_venc_attach_vb_pool(PHOTO_VENC_CH, g_venc_pool) !=
            K_SUCCESS)
        return -1;
    g_venc_attached = 1;
    attr.venc_attr.type = K_PT_H264;
    attr.venc_attr.profile = VENC_PROFILE_H264_HIGH;
    attr.venc_attr.pic_width = g_capture_width;
    attr.venc_attr.pic_height = g_capture_height;
    attr.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
    attr.rc_attr.cbr.src_frame_rate = CAPTURE_FPS;
    attr.rc_attr.cbr.dst_frame_rate = CAPTURE_FPS;
    attr.rc_attr.cbr.bit_rate =
        g_capture_width >= 1920 ? 8000 :
        g_capture_width >= 1280 ? 5000 : 3000;
    if (kd_mpi_venc_create_chn(PHOTO_VENC_CH, &attr) != K_SUCCESS)
        return -1;
    g_venc_created = 1;
    kd_mpi_venc_enable_idr(PHOTO_VENC_CH, K_TRUE);
    if (kd_mpi_venc_start_chn(PHOTO_VENC_CH) != K_SUCCESS)
        return -1;
    g_venc_started = 1;
    return 0;
}

static void bind_capture_to_encoder(int bind)
{
    k_mpp_chn src = { K_ID_VI, g_active_dev, DSHANPI_CAMERA_CAPTURE_CH };
    k_mpp_chn dst = { K_ID_VENC, 0, PHOTO_VENC_CH };
    if (bind)
        kd_mpi_sys_bind(&src, &dst);
    else
        kd_mpi_sys_unbind(&src, &dst);
}

static void *record_stream_worker(void *argument)
{
    (void)argument;
    unsigned char *first_frame = NULL;
    size_t first_frame_size = 0;
    int mux_started = 0;
    while (g_record_thread_running) {
        k_venc_chn_status status = { 0 };
        k_venc_stream stream = { 0 };
        if (kd_mpi_venc_query_status(PHOTO_VENC_CH, &status) != K_SUCCESS ||
            status.cur_packs == 0) {
            usleep(10000);
            continue;
        }
        stream.pack_cnt = status.cur_packs;
        stream.pack = calloc(stream.pack_cnt, sizeof(*stream.pack));
        if (stream.pack == NULL)
            continue;
        if (kd_mpi_venc_get_stream(PHOTO_VENC_CH, &stream, 500) ==
            K_SUCCESS) {
            for (k_u32 i = 0; i < stream.pack_cnt; ++i) {
                void *data = kd_mpi_sys_mmap(stream.pack[i].phys_addr,
                                             stream.pack[i].len);
                if (data != NULL) {
                    k_mp4_frame_data_s frame = { 0 };
                    frame.codec_id = K_MP4_CODEC_ID_H264;
                    if (!mux_started) {
                        size_t required =
                            first_frame_size + stream.pack[i].len;
                        unsigned char *combined =
                            required <= 2U * 1024U * 1024U
                                ? realloc(first_frame, required)
                                : NULL;
                        if (combined != NULL) {
                            first_frame = combined;
                            memcpy(first_frame + first_frame_size, data,
                                   stream.pack[i].len);
                            first_frame_size = required;
                        }
                        if (stream.pack[i].type == K_VENC_I_FRAME &&
                            first_frame_size > 0) {
                            g_record_first_pts = stream.pack[i].pts;
                            frame.data = first_frame;
                            frame.data_length = first_frame_size;
                            frame.time_stamp = 0;
                            if (kd_mp4_write_frame(
                                    g_mp4, g_mp4_video_track, &frame) ==
                                K_SUCCESS)
                                mux_started = 1;
                            free(first_frame);
                            first_frame = NULL;
                            first_frame_size = 0;
                        }
                    } else {
                        frame.data = data;
                        frame.data_length = stream.pack[i].len;
                        frame.time_stamp =
                            stream.pack[i].pts - g_record_first_pts;
                        kd_mp4_write_frame(g_mp4, g_mp4_video_track, &frame);
                    }
                    kd_mpi_sys_munmap(data, stream.pack[i].len);
                }
            }
            kd_mpi_venc_release_stream(PHOTO_VENC_CH, &stream);
        }
        free(stream.pack);
    }
    free(first_frame);
    return NULL;
}

static int capture_to_file(const char *path);

void dshanpi_camera_stop(void)
{
    k_s32 ret;

    if (g_recording)
        dshanpi_camera_record_stop();
    /*
     * Disconnect the consumer first.  Stopping VICAP while the rotating VO
     * layer still owns frames can leave VI waiting forever for those frames.
     */
    unbind_camera_from_vo();
    usleep(50 * 1000);

    for (int dev = CAMERA_COUNT - 1; dev >= 0; --dev) {
        if (g_vicap_started[dev]) {
            ret = kd_mpi_vicap_stop_stream((k_vicap_dev)dev);
            if (ret != K_SUCCESS) {
                printf("[camera] VICAP dev%d stop failed, ret=%d\n",
                       dev, ret);
            }
            g_vicap_started[dev] = 0;
        }
    }
    for (int dev = CAMERA_COUNT - 1; dev >= 0; --dev) {
        if (g_vicap_initialized[dev]) {
            ret = kd_mpi_vicap_deinit((k_vicap_dev)dev);
            if (ret != K_SUCCESS) {
                printf("[camera] VICAP dev%d deinit failed, ret=%d\n",
                       dev, ret);
            }
            g_vicap_initialized[dev] = 0;
        }
    }
    destroy_encoder();
    g_camera_started = 0;
    g_active_csi = -1;
    g_active_dev = -1;

    /* Let the VI/ISP worker and sensor power sequence finish before reprobe. */
    usleep(300 * 1000);
}

int dshanpi_camera_record_start(char *output_path, size_t output_path_size)
{
    k_mp4_config_s config = { 0 };
    k_mp4_track_info_s track = { 0 };
    if (!g_camera_started || g_recording || output_path == NULL ||
        make_video_path(g_video_path, sizeof(g_video_path)) != 0)
        return -1;

    /* Capture a JPEG thumbnail before switching the encoder to H.264 so the
     * Gallery can show the first frame as the video preview.  The thumbnail
     * shares the video filename stem with a .jpg extension. */
    {
        char thumb_path[320];
        size_t vid_len = strlen(g_video_path);
        if (vid_len > 4 && vid_len + 1 < sizeof(thumb_path)) {
            memcpy(thumb_path, g_video_path, vid_len - 4);
            memcpy(thumb_path + vid_len - 4, ".jpg", 5);
            capture_to_file(thumb_path);
        }
    }

    destroy_encoder();
    if (configure_video_encoder() != 0)
        goto fail;
    config.config_type = K_MP4_CONFIG_MUXER;
    if (strlen(g_video_path) >= sizeof(config.muxer_config.file_name))
        goto fail;
    memcpy(config.muxer_config.file_name, g_video_path,
           strlen(g_video_path) + 1);
    config.muxer_config.fmp4_flag = 1;
    if (kd_mp4_create(&g_mp4, &config) < 0)
        goto fail;
    track.track_type = K_MP4_STREAM_VIDEO;
    track.time_scale = 1000;
    track.video_info.width = g_capture_width;
    track.video_info.height = g_capture_height;
    track.video_info.codec_id = K_MP4_CODEC_ID_H264;
    if (kd_mp4_create_track(g_mp4, &g_mp4_video_track, &track) < 0)
        goto fail;
    g_record_first_pts = 0;
    g_record_thread_running = 1;
    bind_capture_to_encoder(1);
    kd_mpi_venc_request_idr(PHOTO_VENC_CH);
    if (pthread_create(&g_record_thread, NULL, record_stream_worker, NULL) != 0)
        goto fail_bound;
    g_recording = 1;
    snprintf(output_path, output_path_size, "%s", g_video_path);
    return 0;
fail_bound:
    bind_capture_to_encoder(0);
    g_record_thread_running = 0;
fail:
    if (g_mp4 != NULL) {
        kd_mp4_destroy_tracks(g_mp4);
        kd_mp4_destroy(g_mp4);
        g_mp4 = NULL;
    }
    destroy_encoder();
    configure_encoder();
    unlink(g_video_path);
    return -1;
}

int dshanpi_camera_record_stop(void)
{
    if (!g_recording)
        return 0;
    g_record_thread_running = 0;
    pthread_join(g_record_thread, NULL);
    bind_capture_to_encoder(0);
    destroy_encoder();
    kd_mp4_destroy_tracks(g_mp4);
    kd_mp4_destroy(g_mp4);
    g_mp4 = NULL;
    g_mp4_video_track = NULL;
    g_recording = 0;
    return configure_encoder();
}

int dshanpi_camera_is_recording(void)
{
    return g_recording;
}

int dshanpi_camera_start(int csi, int resolution)
{
    unsigned capture_width;
    unsigned capture_height;

    if (csi != DSHANPI_CAMERA_REAR_CSI &&
        csi != DSHANPI_CAMERA_FRONT_CSI) {
        printf("[camera] unsupported CSI%d\n", csi);
        return -1;
    }
    if (dshanpi_camera_resolution_dimensions(
            resolution, &capture_width, &capture_height) != 0) {
        printf("[camera] unsupported resolution %d\n", resolution);
        return -1;
    }
    if (g_camera_started && g_active_csi == csi &&
        g_capture_width == capture_width &&
        g_capture_height == capture_height) {
        return 0;
    }
    if (g_camera_started)
        dshanpi_camera_stop();
    g_capture_width = capture_width;
    g_capture_height = capture_height;
    if (probe_camera(csi) != 0) {
        return -1;
    }

    if (configure_vicap_attrs(0, csi) != 0) {
        dshanpi_camera_stop();
        return -1;
    }
    k_s32 ret = kd_mpi_vicap_init(VICAP_DEV_ID_0);
    if (ret != K_SUCCESS) {
        printf("[camera] VICAP dev0 init failed, ret=%d\n", ret);
        kd_mpi_vicap_deinit(VICAP_DEV_ID_0);
        dshanpi_camera_stop();
        return -1;
    }
    g_vicap_initialized[0] = 1;
    if (configure_encoder() != 0) {
        dshanpi_camera_stop();
        return -1;
    }
    if (kd_mpi_vicap_start_stream(VICAP_DEV_ID_0) != K_SUCCESS) {
        printf("[camera] VICAP dev0 stream start failed\n");
        dshanpi_camera_stop();
        return -1;
    }
    g_vicap_started[0] = 1;
    if (bind_camera_to_vo(0) != 0) {
        dshanpi_camera_stop();
        return -1;
    }

    g_camera_started = 1;
    g_active_dev = 0;
    g_active_csi = csi;
    printf("[camera] %s Camera session ready on CSI%d, output=%ux%u\n",
           dshanpi_camera_setting_name(csi), csi,
           g_capture_width, g_capture_height);
    return 0;
}

static int capture_to_file(const char *path)
{
    k_venc_stream stream = { 0 };
    k_venc_chn_status status = { 0 };
    FILE *file = NULL;
    int encoder_bound = 0;
    int stream_acquired = 0;
    size_t bytes_written = 0;
    int jpeg_soi_found = 0;
    int ret = -1;

    if (!g_camera_started || path == NULL) {
        return -1;
    }

    /*
     * Do not pass a VICAP dump frame to kd_mpi_venc_send_frame() here.
     * A high-resolution NV12 dump may start at a non-page-aligned offset within
     * its VB block, while the VENC user-frame queue requires both VA and PA
     * to be page aligned.  The MPP VI->VENC binding path transfers the same
     * frame inside the media pipeline and does not have that restriction.
     */
    bind_capture_to_encoder(1);
    encoder_bound = 1;

    for (int wait = 0; wait < 150; ++wait) {
        if (kd_mpi_venc_query_status(PHOTO_VENC_CH, &status) == K_SUCCESS &&
            status.cur_packs > 0) {
            break;
        }
        usleep(10 * 1000);
    }
    if (status.cur_packs == 0) {
        printf("[camera] JPEG encoder frame timed out on CSI%d\n",
               g_active_csi);
        goto cleanup;
    }

    stream.pack_cnt = status.cur_packs;
    stream.pack = calloc(stream.pack_cnt, sizeof(*stream.pack));
    if (stream.pack == NULL ||
        kd_mpi_venc_get_stream(PHOTO_VENC_CH, &stream, 1500) != K_SUCCESS) {
        goto cleanup;
    }
    stream_acquired = 1;
    file = fopen(path, "wb");
    if (file == NULL) {
        goto cleanup;
    }
    for (k_u32 i = 0; i < stream.pack_cnt; ++i) {
        void *data =
            kd_mpi_sys_mmap(stream.pack[i].phys_addr, stream.pack[i].len);
        if (data == NULL ||
            fwrite(data, 1, stream.pack[i].len, file) != stream.pack[i].len) {
            if (data != NULL) {
                kd_mpi_sys_munmap(data, stream.pack[i].len);
            }
            goto cleanup;
        }
        if (bytes_written == 0 && stream.pack[i].len >= 2) {
            const unsigned char *jpeg = data;
            jpeg_soi_found = jpeg[0] == 0xff && jpeg[1] == 0xd8;
        }
        bytes_written += stream.pack[i].len;
        kd_mpi_sys_munmap(data, stream.pack[i].len);
    }
    ret = fclose(file) == 0 && jpeg_soi_found && bytes_written > 4 ? 0 : -1;
    file = NULL;
    if (ret != 0) {
        printf("[camera] invalid JPEG stream: bytes=%lu soi=%d\n",
               (unsigned long)bytes_written, jpeg_soi_found);
    }

cleanup:
    if (file != NULL) {
        fclose(file);
    }
    if (stream_acquired) {
        kd_mpi_venc_release_stream(PHOTO_VENC_CH, &stream);
    }
    if (encoder_bound) {
        bind_capture_to_encoder(0);
    }
    free(stream.pack);
    if (ret != 0) {
        unlink(path);
    }
    return ret;
}

int dshanpi_camera_capture_preview(const char *preview_path)
{
    return capture_to_file(preview_path);
}

int dshanpi_camera_capture_jpeg(int csi, char *output_path,
                                size_t output_path_size)
{
    if (output_path == NULL || output_path_size == 0 ||
        make_photo_path(output_path, output_path_size) != 0) {
        return -1;
    }
    if ((!g_camera_started || g_active_csi != csi) &&
        dshanpi_camera_start(csi, dshanpi_camera_resolution_load()) != 0) {
        output_path[0] = '\0';
        return -1;
    }
    int ret = capture_to_file(output_path);
    if (ret == 0) {
        printf("[camera] saved CSI%d photo: %s\n", csi, output_path);
    } else {
        output_path[0] = '\0';
    }
    return ret;
}
