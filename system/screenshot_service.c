#include "screenshot_service.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <drv_gpio.h>
#pragma GCC diagnostic pop
#include <errno.h>
#include <mpi_sys_api.h>
#include <mpi_vb_api.h>
#include <mpi_venc_api.h>
#include <mpi_vo_api.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SCREENSHOT_KEY1_GPIO 14
#define SCREENSHOT_KEY4_GPIO 27
#define SCREENSHOT_VENC_CH 3u
#define SCREENSHOT_DIR "/data/dshanpi_photos"
#define SCREENSHOT_DEBOUNCE_SAMPLES 4
#define SCREENSHOT_WBC_SETTLE_US 70000
#define SCREENSHOT_WBC_FRAME_US 30000
#define SCREENSHOT_WBC_WARMUP_FRAMES 2

static dshanpi_screenshot_saved_cb g_saved_callback;

static void screenshot_publish_saved_notification(const char *image_path)
{
    const char *temporary = DSHANPI_SCREENSHOT_NOTICE_PATH ".tmp";
    FILE *file = fopen(temporary, "w");
    int failed;

    if (file == NULL) {
        printf("[screenshot] unable to create success notification\n");
        return;
    }
    failed = fprintf(file, "%s\n", image_path) < 0 ||
             fflush(file) != 0 || fsync(fileno(file)) != 0;
    if (fclose(file) != 0)
        failed = 1;
    if (failed) {
        unlink(temporary);
        printf("[screenshot] unable to write success notification\n");
        return;
    }

    /* The notification is level-triggered: one pending marker is sufficient
     * even if several screenshots are taken before the desktop consumes it. */
    unlink(DSHANPI_SCREENSHOT_NOTICE_PATH);
    if (rename(temporary, DSHANPI_SCREENSHOT_NOTICE_PATH) != 0) {
        unlink(temporary);
        printf("[screenshot] unable to publish success notification\n");
    }
}

static int screenshot_write_stream(const char *path, k_venc_stream *stream)
{
    FILE *file = fopen(path, "wb");
    size_t total = 0;
    int valid_soi = 0;
    int result = -1;

    if (!file)
        return -1;
    for (k_u32 i = 0; i < stream->pack_cnt; ++i) {
        void *data = kd_mpi_sys_mmap(stream->pack[i].phys_addr,
                                     stream->pack[i].len);
        if (!data)
            goto done;
        if (total == 0 && stream->pack[i].len >= 2) {
            const uint8_t *bytes = data;
            valid_soi = bytes[0] == 0xff && bytes[1] == 0xd8;
        }
        if (fwrite(data, 1, stream->pack[i].len, file) !=
            stream->pack[i].len) {
            kd_mpi_sys_munmap(data, stream->pack[i].len);
            goto done;
        }
        total += stream->pack[i].len;
        kd_mpi_sys_munmap(data, stream->pack[i].len);
    }
    result = valid_soi && total > 4 && fflush(file) == 0 &&
             fsync(fileno(file)) == 0 ? 0 : -1;
done:
    if (fclose(file) != 0)
        result = -1;
    if (result != 0)
        unlink(path);
    return result;
}

static k_rotation screenshot_get_output_rotation(
    const k_video_frame_info *frame, k_gdma_rotation_e *layer_rotation)
{
    k_vo_layer_attr layer_attr = { 0 };
    k_rotation rotation = K_VPU_ROTATION_0;

    *layer_rotation = GDMA_ROTATE_NONE;
    if (kd_mpi_vo_get_layer_attr(K_VO_LAYER_OSD0, &layer_attr) ==
        K_SUCCESS) {
        *layer_rotation = layer_attr.func;
        /* WBC contains the post-composition panel raster.  Apply the matching
         * VPU transform so the JPEG has the same upright orientation and
         * dimensions as LVGL's logical display. */
        switch (layer_attr.func) {
        case GDMA_ROTATE_DEGREE_90:
            rotation = K_VPU_ROTATION_90;
            break;
        case GDMA_ROTATE_DEGREE_180:
            rotation = K_VPU_ROTATION_180;
            break;
        case GDMA_ROTATE_DEGREE_270:
            rotation = K_VPU_ROTATION_270;
            break;
        default:
            break;
        }
    }

    /* This product always presents a landscape desktop.  Keep screenshots
     * usable even if OSD0 is being reconfigured while its attributes are
     * queried: a portrait WBC raster needs the same quarter-turn as
     * the normal LVGL ROTATION_270 / GDMA_ROTATE_DEGREE_90 path. */
    if (rotation == K_VPU_ROTATION_0 &&
        frame->v_frame.height > frame->v_frame.width)
        rotation = K_VPU_ROTATION_90;
    return rotation;
}

static int screenshot_encode_frame(const char *path,
                                   k_video_frame_info *frame,
                                   k_rotation rotation)
{
    k_venc_chn_attr attr = { 0 };
    k_venc_chn_status status = { 0 };
    k_venc_stream stream = { 0 };
    k_venc_pack packs[8] = { 0 };
    k_s32 pool = (k_s32)VB_INVALID_POOLID;
    k_u32 width = frame->v_frame.width;
    k_u32 height = frame->v_frame.height;
    int attached = 0;
    int created = 0;
    int started = 0;
    int acquired = 0;
    int result = -1;

    if (width == 0 || height == 0)
        return -1;
    /* Match the SDK WBC/JPEG reference path.  A JPEG output block is sized
     * for one complete NV12 frame so complex frames cannot exhaust the VENC
     * stream buffer. */
    pool = kd_mpi_vb_create_pool_ex(
        ((k_u64)width * height * 3u / 2u + 4095u) & ~4095u, 3,
        VB_REMAP_MODE_NOCACHE);
    if (pool == (k_s32)VB_INVALID_POOLID)
        goto done;
    if (kd_mpi_venc_attach_vb_pool(SCREENSHOT_VENC_CH, pool) != K_SUCCESS)
        goto done;
    attached = 1;
    attr.venc_attr.type = K_PT_JPEG;
    attr.venc_attr.pic_width = width;
    attr.venc_attr.pic_height = height;
    attr.rc_attr.rc_mode = K_VENC_RC_MODE_MJPEG_FIXQP;
    attr.rc_attr.mjpeg_fixqp.src_frame_rate = 1;
    attr.rc_attr.mjpeg_fixqp.dst_frame_rate = 1;
    attr.rc_attr.mjpeg_fixqp.q_factor = 92;
    if (kd_mpi_venc_create_chn(SCREENSHOT_VENC_CH, &attr) != K_SUCCESS)
        goto done;
    created = 1;
    if (rotation != K_VPU_ROTATION_0 &&
        kd_mpi_venc_set_rotation(SCREENSHOT_VENC_CH, rotation) != K_SUCCESS)
        goto done;
    if (kd_mpi_venc_start_chn(SCREENSHOT_VENC_CH) != K_SUCCESS)
        goto done;
    started = 1;
    if (kd_mpi_venc_send_frame(SCREENSHOT_VENC_CH, frame, 1000) != K_SUCCESS)
        goto done;
    for (int retry = 0; retry < 150; ++retry) {
        if (kd_mpi_venc_query_status(SCREENSHOT_VENC_CH, &status) ==
                K_SUCCESS && status.cur_packs > 0)
            break;
        usleep(10000);
    }
    if (status.cur_packs == 0 || status.cur_packs > 8)
        goto done;
    stream.pack = packs;
    stream.pack_cnt = status.cur_packs;
    if (kd_mpi_venc_get_stream(SCREENSHOT_VENC_CH, &stream, 1500) !=
        K_SUCCESS)
        goto done;
    acquired = 1;
    result = screenshot_write_stream(path, &stream);
done:
    if (acquired)
        kd_mpi_venc_release_stream(SCREENSHOT_VENC_CH, &stream);
    if (started)
        kd_mpi_venc_stop_chn(SCREENSHOT_VENC_CH);
    if (created)
        kd_mpi_venc_destroy_chn(SCREENSHOT_VENC_CH);
    if (attached)
        kd_mpi_venc_detach_vb_pool(SCREENSHOT_VENC_CH);
    if (pool != (k_s32)VB_INVALID_POOLID)
        kd_mpi_vb_destory_pool(pool);
    return result;
}

static int screenshot_dump_stable_frame(k_video_frame_info *frame)
{
    k_video_frame_info warmup = { 0 };

    /* WBC starts with newly allocated DMA buffers.  The first refresh after
     * enabling it can expose that initial buffer before a complete composed
     * frame has reached memory.  Let several panel refreshes pass, then drain
     * two frames before returning the frame that will be encoded. */
    usleep(SCREENSHOT_WBC_SETTLE_US);
    for (int i = 0; i < SCREENSHOT_WBC_WARMUP_FRAMES; ++i) {
        memset(&warmup, 0, sizeof(warmup));
        if (kd_mpi_wbc_dump_frame(&warmup, 1200) != K_SUCCESS)
            return -1;
        kd_mpi_wbc_dump_release(&warmup);
        usleep(SCREENSHOT_WBC_FRAME_US);
    }

    memset(frame, 0, sizeof(*frame));
    return kd_mpi_wbc_dump_frame(frame, 1200) == K_SUCCESS ? 0 : -1;
}

static int screenshot_capture(void)
{
    k_vo_wbc_attr attr = { .blk_cnt = 3 };
    k_video_frame_info frame = { 0 };
    char path[384];
    struct tm local;
    time_t now = time(NULL);
    int enabled = 0;
    int acquired = 0;
    int result = -1;
    k_gdma_rotation_e layer_rotation = GDMA_ROTATE_NONE;
    k_rotation output_rotation = K_VPU_ROTATION_0;

    mkdir(SCREENSHOT_DIR, 0755);
    localtime_r(&now, &local);
    snprintf(path, sizeof(path),
             SCREENSHOT_DIR "/SCREEN_%04d%02d%02d_%02d%02d%02d.jpg",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min, local.tm_sec);
    if (kd_mpi_vo_set_wbc_attr(&attr) != K_SUCCESS ||
        kd_mpi_vo_enable_wbc() != K_SUCCESS)
        goto done;
    enabled = 1;
    if (screenshot_dump_stable_frame(&frame) != 0)
        goto done;
    acquired = 1;
    output_rotation = screenshot_get_output_rotation(&frame,
                                                     &layer_rotation);
    printf("[screenshot] stable WBC frame: %ux%u format=%d, "
           "OSD rotation=%d, JPEG rotation=%d, output=%ux%u\n",
           frame.v_frame.width, frame.v_frame.height,
           frame.v_frame.pixel_format, layer_rotation, output_rotation,
           output_rotation == K_VPU_ROTATION_90 ||
                   output_rotation == K_VPU_ROTATION_270
               ? frame.v_frame.height
               : frame.v_frame.width,
           output_rotation == K_VPU_ROTATION_90 ||
                   output_rotation == K_VPU_ROTATION_270
               ? frame.v_frame.width
               : frame.v_frame.height);
    result = screenshot_encode_frame(path, &frame, output_rotation);
done:
    if (acquired)
        kd_mpi_wbc_dump_release(&frame);
    if (enabled)
        kd_mpi_vo_disable_wbc();
    if (result == 0) {
        printf("[screenshot] saved composited display: %s\n", path);
        /* Publish only after WBC is disabled, so the success toast cannot be
         * included in the screenshot that triggered it. */
        screenshot_publish_saved_notification(path);
        if (g_saved_callback != NULL)
            g_saved_callback(path);
    } else {
        printf("[screenshot] capture failed; display may be transitioning\n");
    }
    return result;
}

static void *screenshot_key_worker(void *argument)
{
    drv_gpio_inst_t *key1 = NULL;
    drv_gpio_inst_t *key4 = NULL;
    unsigned held_samples = 0;
    bool latched = false;

    (void)argument;
    if (drv_gpio_inst_create(SCREENSHOT_KEY1_GPIO, &key1) != 0 ||
        drv_gpio_inst_create(SCREENSHOT_KEY4_GPIO, &key4) != 0 ||
        drv_gpio_mode_set(key1, GPIO_DM_INPUT_PULLUP) != 0 ||
        drv_gpio_mode_set(key4, GPIO_DM_INPUT_PULLUP) != 0) {
        printf("[screenshot] KEY1/KEY4 GPIO initialization failed\n");
        goto done;
    }
    printf("[screenshot] global shortcut ready: GPIO14 + GPIO27\n");
    for (;;) {
        bool both = drv_gpio_value_get(key1) == GPIO_PV_LOW &&
                    drv_gpio_value_get(key4) == GPIO_PV_LOW;
        if (both && !latched) {
            if (++held_samples >= SCREENSHOT_DEBOUNCE_SAMPLES) {
                latched = true;
                screenshot_capture();
            }
        } else if (!both) {
            held_samples = 0;
            latched = false;
        }
        usleep(20000);
    }
done:
    drv_gpio_inst_destroy(&key1);
    drv_gpio_inst_destroy(&key4);
    return NULL;
}

int dshanpi_screenshot_service_start(dshanpi_screenshot_saved_cb callback)
{
    pthread_t thread;

    g_saved_callback = callback;
    unlink(DSHANPI_SCREENSHOT_NOTICE_PATH);
    unlink(DSHANPI_SCREENSHOT_NOTICE_PATH ".tmp");
    if (pthread_create(&thread, NULL, screenshot_key_worker, NULL) != 0)
        return -1;
    pthread_detach(thread);
    return 0;
}
