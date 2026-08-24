/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <signal.h>
#ifdef DSHANPI_UVC_APP
#include <pthread.h>
#include "drv_touch.h"
#include "k_type.h"
#include "k_vb_comm.h"
#include "k_video_comm.h"
#include "mpi_vb_api.h"
#include "mpi_sys_api.h"
#include "mpi_vo_api.h"
#include "k_vo_comm.h"
#include "framework/kd_display.h"

#define UVC_BACK_X 12
#define UVC_BACK_Y 12
#define UVC_BACK_WIDTH 58
#define UVC_BACK_HEIGHT 58
#define UVC_BACK_HIT_SIZE 104
#define UVC_OSD_WIDTH 640
#define UVC_OSD_HEIGHT 480

typedef struct {
    k_s32 pool;
    k_vb_blk_handle block;
    k_video_frame_info frame;
    bool enabled;
} uvc_back_osd;

typedef struct {
    k_s32 pool;
    k_vb_blk_handle block;
    k_video_frame_info frame;
    uint32_t *pixels;
    bool enabled;
} uvc_fps_osd;

static const uint8_t *fps_glyph(char c)
{
    static const uint8_t font[][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7},
        {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
        {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7},
        {7, 5, 7, 1, 7},
    };
    static const uint8_t f[5] = {7, 4, 6, 4, 4};
    static const uint8_t p[5] = {6, 5, 6, 4, 4};
    static const uint8_t s[5] = {7, 4, 7, 1, 7};
    static const uint8_t dot[5] = {0, 0, 0, 0, 2};
    if (c >= '0' && c <= '9') return font[c - '0'];
    if (c == 'F') return f;
    if (c == 'P') return p;
    if (c == 'S') return s;
    if (c == '.') return dot;
    return NULL;
}

static void update_fps_osd(uvc_fps_osd *osd, double fps)
{
    enum { WIDTH = UVC_OSD_WIDTH, HEIGHT = UVC_OSD_HEIGHT, SCALE = 3,
           PANEL_X = 500, PANEL_Y = 428, PANEL_W = 128, PANEL_H = 40,
           PANEL_RADIUS = 13 };
    char text[16];
    int length;
    int start_x;

    if (!osd->enabled || !osd->pixels) return;
    snprintf(text, sizeof(text), "FPS %.1f", fps);
    length = strlen(text);
    start_x = PANEL_X + (PANEL_W - length * 4 * SCALE + SCALE) / 2;
    for (int i = 0; i < WIDTH * HEIGHT; ++i)
        osd->pixels[i] = 0x00000000U;
    for (int py = 0; py < PANEL_H; ++py) {
        for (int px = 0; px < PANEL_W; ++px) {
            bool inside = (px >= PANEL_RADIUS &&
                           px < PANEL_W - PANEL_RADIUS) ||
                          (py >= PANEL_RADIUS &&
                           py < PANEL_H - PANEL_RADIUS) ||
                          ((px - PANEL_RADIUS) * (px - PANEL_RADIUS) +
                           (py - PANEL_RADIUS) * (py - PANEL_RADIUS) <=
                           PANEL_RADIUS * PANEL_RADIUS) ||
                          ((px - (PANEL_W - PANEL_RADIUS - 1)) *
                               (px - (PANEL_W - PANEL_RADIUS - 1)) +
                           (py - PANEL_RADIUS) * (py - PANEL_RADIUS) <=
                           PANEL_RADIUS * PANEL_RADIUS) ||
                          ((px - PANEL_RADIUS) * (px - PANEL_RADIUS) +
                           (py - (PANEL_H - PANEL_RADIUS - 1)) *
                               (py - (PANEL_H - PANEL_RADIUS - 1)) <=
                           PANEL_RADIUS * PANEL_RADIUS) ||
                          ((px - (PANEL_W - PANEL_RADIUS - 1)) *
                               (px - (PANEL_W - PANEL_RADIUS - 1)) +
                           (py - (PANEL_H - PANEL_RADIUS - 1)) *
                               (py - (PANEL_H - PANEL_RADIUS - 1)) <=
                           PANEL_RADIUS * PANEL_RADIUS);
            if (inside)
                osd->pixels[(PANEL_Y + py) * WIDTH + PANEL_X + px] =
                    0xDC282828U;
        }
    }
    for (int n = 0; n < length; ++n) {
        const uint8_t *glyph = fps_glyph(text[n]);
        if (!glyph) continue;
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (!(glyph[row] & (1U << (2 - col)))) continue;
                for (int sy = 0; sy < SCALE; ++sy)
                    for (int sx = 0; sx < SCALE; ++sx)
                        osd->pixels[(PANEL_Y + 12 + row * SCALE + sy) * WIDTH +
                                    start_x + n * 4 * SCALE + col * SCALE + sx] =
                            0xFFFFFFFFU;
            }
        }
    }
    kd_display_layer_push_frame(K_VO_LAYER_OSD1, &osd->frame);
}

static int show_fps_osd(uvc_fps_osd *osd)
{
    enum { WIDTH = UVC_OSD_WIDTH, HEIGHT = UVC_OSD_HEIGHT };
    k_vb_pool_config pool_cfg = { 0 };
    size_t bytes = WIDTH * HEIGHT * 4;
    pool_cfg.blk_cnt = 1;
    pool_cfg.blk_size = bytes;
    pool_cfg.mode = VB_REMAP_MODE_NONE;
    osd->pool = kd_mpi_vb_create_pool(&pool_cfg);
    if (osd->pool == VB_INVALID_POOLID) return -1;
    osd->block = kd_mpi_vb_get_block(osd->pool, bytes, NULL);
    if (osd->block == VB_INVALID_HANDLE) return -1;
    memset(&osd->frame, 0, sizeof(osd->frame));
    osd->frame.pool_id = osd->pool;
    osd->frame.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(osd->block);
    osd->frame.v_frame.width = WIDTH;
    osd->frame.v_frame.height = HEIGHT;
    osd->frame.v_frame.stride[0] = WIDTH * 4;
    osd->frame.v_frame.pixel_format = PIXEL_FORMAT_ARGB_8888;
    osd->pixels = kd_mpi_sys_mmap(osd->frame.v_frame.phys_addr[0], bytes);
    if (!osd->pixels) return -1;
    if (kd_display_layer_configure(K_VO_LAYER_OSD1, PIXEL_FORMAT_ARGB_8888,
                                   WIDTH, HEIGHT, 0, 0, 255,
                                   GDMA_ROTATE_DEGREE_90, 2, 0) != 0 ||
        kd_display_layer_enable(K_VO_LAYER_OSD1) != 0)
        return -1;
    osd->enabled = true;
    update_fps_osd(osd, 0.0);
    return 0;
}

static void hide_fps_osd(uvc_fps_osd *osd)
{
    enum { WIDTH = UVC_OSD_WIDTH, HEIGHT = UVC_OSD_HEIGHT };
    if (osd->enabled) kd_display_layer_disable(K_VO_LAYER_OSD1);
    if (osd->pixels) kd_mpi_sys_munmap(osd->pixels, WIDTH * HEIGHT * 4);
    if (osd->block != VB_INVALID_HANDLE) kd_mpi_vb_release_block(osd->block);
    if (osd->pool != VB_INVALID_POOLID) kd_mpi_vb_destory_pool(osd->pool);
}

static int show_back_osd(uvc_back_osd *osd)
{
    enum { WIDTH = UVC_OSD_WIDTH, HEIGHT = UVC_OSD_HEIGHT };
    k_vb_pool_config pool_cfg = { 0 };
    uint32_t *pixels;
    size_t bytes = WIDTH * HEIGHT * 4;
    pool_cfg.blk_cnt = 1;
    pool_cfg.blk_size = bytes;
    pool_cfg.mode = VB_REMAP_MODE_NONE;
    osd->pool = kd_mpi_vb_create_pool(&pool_cfg);
    if (osd->pool == VB_INVALID_POOLID) return -1;
    osd->block = kd_mpi_vb_get_block(osd->pool, bytes, NULL);
    if (osd->block == VB_INVALID_HANDLE) return -1;
    memset(&osd->frame, 0, sizeof(osd->frame));
    osd->frame.pool_id = osd->pool;
    osd->frame.v_frame.phys_addr[0] =
        kd_mpi_vb_handle_to_phyaddr(osd->block);
    osd->frame.v_frame.width = WIDTH;
    osd->frame.v_frame.height = HEIGHT;
    osd->frame.v_frame.stride[0] = WIDTH * 4;
    osd->frame.v_frame.pixel_format = PIXEL_FORMAT_ARGB_8888;
    pixels = kd_mpi_sys_mmap(osd->frame.v_frame.phys_addr[0], bytes);
    if (pixels == NULL) return -1;
    memset(pixels, 0, bytes);
    for (int by = 0; by < UVC_BACK_HEIGHT; ++by) {
        for (int bx = 0; bx < UVC_BACK_WIDTH; ++bx) {
            const int radius = 18;
            bool inside = (bx >= radius && bx < UVC_BACK_WIDTH - radius) ||
                          (by >= radius && by < UVC_BACK_HEIGHT - radius) ||
                          ((bx - radius) * (bx - radius) +
                           (by - radius) * (by - radius) <= radius * radius) ||
                          ((bx - (UVC_BACK_WIDTH - radius - 1)) *
                               (bx - (UVC_BACK_WIDTH - radius - 1)) +
                           (by - radius) * (by - radius) <= radius * radius) ||
                          ((bx - radius) * (bx - radius) +
                           (by - (UVC_BACK_HEIGHT - radius - 1)) *
                               (by - (UVC_BACK_HEIGHT - radius - 1)) <= radius * radius) ||
                          ((bx - (UVC_BACK_WIDTH - radius - 1)) *
                               (bx - (UVC_BACK_WIDTH - radius - 1)) +
                           (by - (UVC_BACK_HEIGHT - radius - 1)) *
                               (by - (UVC_BACK_HEIGHT - radius - 1)) <= radius * radius);
            /* Face Studio draws a plain centered '<' without a horizontal
             * stem.  Use the same compact proportions in this C-only OSD. */
            bool arrow = bx >= 23 && bx <= 38 &&
                         abs((bx - 23) - abs(by - 29)) <= 2;
            pixels[(UVC_BACK_Y + by) * WIDTH + UVC_BACK_X + bx] =
                !inside ? 0x00000000U :
                arrow ? 0xFFFFFFFFU : 0xDC282828U;
        }
    }
    kd_mpi_sys_munmap(pixels, bytes);
    if (kd_display_layer_configure(K_VO_LAYER_OSD0,
                                   PIXEL_FORMAT_ARGB_8888,
                                   WIDTH, HEIGHT, 0, 0, 255,
                                   GDMA_ROTATE_DEGREE_90, 2, 0) != 0 ||
        kd_display_layer_enable(K_VO_LAYER_OSD0) != 0 ||
        kd_display_layer_push_frame(K_VO_LAYER_OSD0, &osd->frame) != 0)
        return -1;
    osd->enabled = true;
    return 0;
}

static void hide_back_osd(uvc_back_osd *osd)
{
    if (osd->enabled) kd_display_layer_disable(K_VO_LAYER_OSD0);
    if (osd->block != VB_INVALID_HANDLE) kd_mpi_vb_release_block(osd->block);
    if (osd->pool != VB_INVALID_POOLID) kd_mpi_vb_destory_pool(osd->pool);
}
#endif

#include "k_module.h"
#include "k_type.h"
#include "k_vb_comm.h"
#include "k_sys_comm.h"
#include "mpi_vb_api.h"
#include "mpi_sys_api.h"
#include "mpi_vo_api.h"
#include "k_vo_comm.h"

#include "k_connector_comm.h"
#include "mpi_connector_api.h"
#include "mpi_vdec_api.h"

#include "mpi_uvc_api.h"
#include "hal_utils.h"

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align)-1))
#define VO_POOL_BLOCK_COUNT 4
#define OUTPUT_BUF_CNT 6
#ifdef DSHANPI_UVC_APP
#define UVC_FRAME_TIMEOUT_MS 1000U
#define UVC_RECONNECT_DELAY_MS 500U
#endif

typedef struct
{
    k_u32 ch;
    k_vo_layer_id chn_id;
    bool is_mjpeg;

    bool vb_inited;
    bool uvc_started;
    bool vo_layer_enabled;
    bool vo_pool_created;
    bool vo_block_created;
    bool vo_mapped;
    bool vdec_pool_created;
    bool vdec_attached;
    bool vdec_chn_requested;
    bool vdec_created;
    bool vdec_started;
    bool vdec_frame_held;

    k_s32 vo_poolid;
    k_s32 vdec_poolid;
    k_vb_blk_handle vo_block;
    void *vo_vaddr;
    k_u32 vo_map_size;
    k_video_frame_info vdec_frame;
} sample_uvc_runtime;

int vb_init(void)
{
    k_s32 ret = 0;
    k_vb_config config;

    memset(&config, 0, sizeof(config));

    config.max_pool_cnt = 10;

    ret = kd_mpi_vb_set_config(&config);
    if (ret) {
        printf("kd_mpi_vb_set_config fail, ret = %d\n", ret);
        goto out;
    }

    ret = kd_mpi_vb_init();
    if (ret) {
        printf("kd_mpi_vb_init fail, ret = %d\n", ret);
    }

out:
    return ret;
}

int vb_create_vo_pool(int width, int height, k_pixel_format pixel_format)
{
    k_s32 pool_id;
    k_vb_pool_config pool_config;
    k_u32 frame_size = pixel_format == PIXEL_FORMAT_RGB_565_LE
                           ? ALIGN_UP(width * height * 2, 0x1000)
                           : ALIGN_UP(width * height * 3 / 2, 0x1000);

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.blk_cnt = VO_POOL_BLOCK_COUNT;
    pool_config.blk_size = frame_size;
    pool_config.mode = VB_REMAP_MODE_NONE;
    pool_id = kd_mpi_vb_create_pool(&pool_config);

    if (VB_INVALID_POOLID == pool_id) {
        printf("create vo pool fail\n");
        return VB_INVALID_POOLID;
    }

    return pool_id;
}

k_s32 sample_connector_init(k_connector_type type)
{
    k_u32 ret = 0;
    k_s32 connector_fd = -1;
    k_u32 chip_id = 0x00;
    k_connector_type connector_type = type;
    k_connector_info connector_info;

    memset(&connector_info, 0, sizeof(k_connector_info));

    //connector get sensor info
    ret = kd_mpi_get_connector_info(connector_type, &connector_info);
    if (ret) {
        printf("sample_vicap, the sensor type not supported!\n");
        return ret;
    }

    connector_fd = kd_mpi_connector_open(connector_info.connector_name);
    if (connector_fd < 0) {
        printf("%s, connector open failed.\n", __func__);
        return K_ERR_VO_NOTREADY;
    }

    // connector init
    ret = kd_mpi_connector_init(connector_fd, connector_info);
    if (ret) {
        goto out;
    }

    // set connect power
    ret = kd_mpi_connector_power_set(connector_fd, 1);
    if (ret) {
        goto out;
    }
    // set connect get id
    ret = kd_mpi_connector_id_get(connector_fd, &chip_id);
    if (ret) {
        goto out;
    }

out:
    if (connector_fd >= 0) {
        kd_mpi_connector_close(connector_fd);
    }
    return ret;
}

static int sample_vo_layer_start(k_vo_layer_id layer_id, int width, int height,
                                 bool rotation, k_pixel_format pixel_format)
{
    k_vo_layer_attr attr;

    if (layer_id < K_VO_LAYER_VIDEO1 || layer_id > K_VO_LAYER_VIDEO3) {
        printf("input layer id %d not supported\n", layer_id);
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.layer_id = layer_id;
    attr.position.x = 0;
    attr.position.y = 0;
    attr.img_size.width = width;
    attr.img_size.height = height;
    attr.pixel_format = pixel_format;
    attr.func = rotation ? GDMA_ROTATE_DEGREE_270 : GDMA_ROTATE_DEGREE_0;
    attr.rot_buf_nr = rotation ? 2 : 0;
    attr.global_alpha = 0xff;

    if (kd_mpi_vo_set_layer_attr(layer_id, &attr) != K_SUCCESS) {
        printf("kd_mpi_vo_set_layer_attr failed\n");
        return -1;
    }

    if (kd_mpi_vo_enable_layer(layer_id) != K_SUCCESS) {
        printf("kd_mpi_vo_enable_layer failed\n");
        return -1;
    }

    return 0;
}

static int sample_vo_prepare_frame(sample_uvc_runtime *rt, k_video_frame_info *vf_info,
                                   int width, int height, k_pixel_format pixel_format)
{
    k_u64 phys_addr = 0;
    k_u32 y_size;
    k_u32 frame_size;

    if (!rt || !vf_info) {
        return -1;
    }

    y_size = width * height;
    frame_size = pixel_format == PIXEL_FORMAT_RGB_565_LE
                     ? ALIGN_UP(y_size * 2, 0x1000)
                     : ALIGN_UP(y_size * 3 / 2, 0x1000);

    memset(vf_info, 0, sizeof(*vf_info));
    vf_info->pool_id = rt->vo_poolid;
    vf_info->mod_id = K_ID_VO;
    vf_info->v_frame.width = width;
    vf_info->v_frame.height = height;
    vf_info->v_frame.stride[0] = pixel_format == PIXEL_FORMAT_RGB_565_LE
                                     ? width * 2 : width;
    vf_info->v_frame.stride[1] = pixel_format == PIXEL_FORMAT_RGB_565_LE
                                     ? 0 : width;
    vf_info->v_frame.pixel_format = pixel_format;

    rt->vo_block = kd_mpi_vb_get_block(rt->vo_poolid, frame_size, NULL);
    if (rt->vo_block == VB_INVALID_HANDLE) {
        printf("%s get vb block error\n", __func__);
        return -1;
    }

    phys_addr = kd_mpi_vb_handle_to_phyaddr(rt->vo_block);
    if (phys_addr == 0) {
        printf("%s get phys addr error\n", __func__);
        kd_mpi_vb_release_block(rt->vo_block);
        rt->vo_block = VB_INVALID_HANDLE;
        return -1;
    }

    rt->vo_vaddr = kd_mpi_sys_mmap(phys_addr, frame_size);
    if (rt->vo_vaddr == NULL) {
        printf("%s mmap error\n", __func__);
        kd_mpi_vb_release_block(rt->vo_block);
        rt->vo_block = VB_INVALID_HANDLE;
        return -1;
    }

    vf_info->v_frame.phys_addr[0] = phys_addr;
    if (pixel_format != PIXEL_FORMAT_RGB_565_LE) {
        vf_info->v_frame.phys_addr[1] = phys_addr + y_size;
    }
    rt->vo_map_size = frame_size;
    rt->vo_block_created = true;
    rt->vo_mapped = true;
    return 0;
}

static k_s32 vb_create_vdec_pool(int width, int height)
{
    k_vb_pool_config pool_config;

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.blk_cnt = OUTPUT_BUF_CNT;
    pool_config.blk_size = ALIGN_UP(width * height, 0x1000) * 2;
    pool_config.mode = VB_REMAP_MODE_NOCACHE;

    return kd_mpi_vb_create_pool(&pool_config);
}

static k_s32 vb_destroy_vdec_pool(k_s32 vdec_poolid)
{
    return kd_mpi_vb_destory_pool(vdec_poolid);
}

static volatile sig_atomic_t exit_flag;
#ifdef DSHANPI_UVC_APP
static void *touch_thread(void *arg)
{
    (void)arg;
    drv_touch_inst_t *touch = NULL;
    if (drv_touch_inst_create(0, &touch) != 0) return NULL;
    while (!exit_flag) {
        struct drv_touch_data points[DRV_TOUCH_POINT_NUMBER_MAX];
        int count = drv_touch_read(touch, points, DRV_TOUCH_POINT_NUMBER_MAX);
        if (count <= 0) { usleep(10000); continue; }
        if (points[0].event == DRV_TOUCH_EVENT_DOWN) {
            int x = 639 - (int)points[0].y_coordinate;
            int y = 479 - (int)points[0].x_coordinate;
            /* Same button position and forgiving hit area as Face Studio:
             * visible rect (12,12,58,58), touch target (0,0)-(104,104). */
            if (x >= 0 && y >= 0 &&
                x < UVC_BACK_HIT_SIZE && y < UVC_BACK_HIT_SIZE)
                exit_flag = 1;
        }
    }
    drv_touch_inst_destroy(&touch);
    return NULL;
}
#endif

static void sig_handler(int sig_no) {
    (void)sig_no;
    exit_flag = 1;
}

static int parse_fourcc_arg(const char *arg, unsigned int *fourcc)
{
    char *endptr = NULL;
    unsigned long value;

    if (!arg || !fourcc) {
        return -1;
    }

    if (!strcmp(arg, "YUY2")) {
        *fourcc = USBH_VIDEO_FOURCC_YUY2;
        return 0;
    }
    if (!strcmp(arg, "UYVY")) {
        *fourcc = USBH_VIDEO_FOURCC_UYVY;
        return 0;
    }
    if (!strcmp(arg, "NV12")) {
        *fourcc = USBH_VIDEO_FOURCC_NV12;
        return 0;
    }
    if (!strcmp(arg, "I420")) {
        *fourcc = USBH_VIDEO_FOURCC_I420;
        return 0;
    }
    if (!strcmp(arg, "MJPEG") || !strcmp(arg, "MJPG")) {
        *fourcc = USBH_VIDEO_FOURCC_MJPEG;
        return 0;
    }

    value = strtoul(arg, &endptr, 0);
    if ((endptr != arg) && (*endptr == '\0') && (value <= 0xffffffffUL)) {
        *fourcc = (unsigned int)value;
        return 0;
    }

    return -1;
}

static int parse_int_arg(const char *arg, int *value)
{
    char *endptr = NULL;
    long tmp;

    if (!arg || !value) {
        return -1;
    }

    errno = 0;
    tmp = strtol(arg, &endptr, 0);
    if (errno || endptr == arg || *endptr != '\0' || tmp < INT_MIN || tmp > INT_MAX) {
        return -1;
    }

    *value = (int)tmp;
    return 0;
}

static const char *fourcc_to_str(unsigned int fourcc, char text[5])
{
    text[0] = (char)(fourcc & 0xff);
    text[1] = (char)((fourcc >> 8) & 0xff);
    text[2] = (char)((fourcc >> 16) & 0xff);
    text[3] = (char)((fourcc >> 24) & 0xff);
    text[4] = '\0';

    for (int i = 0; i < 4; i++) {
        if (!isprint((unsigned char)text[i])) {
            text[i] = '.';
        }
    }

    return text;
}

static void print_usage(void)
{
    printf("Usage: ./sample_uvc [connector_type] [rotation] [fourcc] [width] [height] [total_frame]\n");
    printf("  [fourcc] supports: YUY2 UYVY NV12 I420 MJPEG (or numeric, e.g. 0x47504a4d)\n");
}

static int sample_vdec_start(sample_uvc_runtime *rt, int width, int height)
{
    k_vdec_chn_attr attr;
    int ret;

    ret = kd_mpi_vdec_request_chn(&rt->ch);
    if (ret) {
        printf("kd_mpi_vdec_request_chn fail, ret = %d\n", ret);
        return ret;
    }
    rt->vdec_chn_requested = true;

    rt->vdec_poolid = vb_create_vdec_pool(width, height);
    if (rt->vdec_poolid == VB_INVALID_POOLID) {
        printf("fail to create vdec pool\n");
        return -1;
    }
    rt->vdec_pool_created = true;

    ret = kd_mpi_vdec_attach_vb_pool(rt->ch, rt->vdec_poolid);
    if (ret) {
        printf("kd_mpi_vdec_attach_vb_pool fail, ret = %d\n", ret);
        return ret;
    }
    rt->vdec_attached = true;

    memset(&attr, 0, sizeof(attr));
    attr.pic_width = width;
    attr.pic_height = height;
    attr.stream_buf_size = ALIGN_UP(width * height, 0x1000);
    attr.type = K_PT_JPEG;
    attr.mode = K_VDEC_SEND_MODE_FRAME;

    ret = kd_mpi_vdec_create_chn(rt->ch, &attr);
    if (ret) {
        printf("kd_mpi_vdec_create_chn fail, ret = %d\n", ret);
        return ret;
    }
    rt->vdec_created = true;

    ret = kd_mpi_vdec_start_chn(rt->ch);
    if (ret) {
        printf("kd_mpi_vdec_start_chn fail, ret = %d\n", ret);
        return ret;
    }
    rt->vdec_started = true;

    return 0;
}

static void sample_cleanup(sample_uvc_runtime *rt)
{
    /* Stop the USB producer before tearing down the asynchronous decoder.
     * Otherwise a disconnect/URB callback can race VDEC destruction. */
    if (rt->uvc_started) {
        uvc_host_exit();
        rt->uvc_started = false;
    }
    if (rt->vo_layer_enabled) {
        kd_mpi_vo_disable_layer(rt->chn_id);
        rt->vo_layer_enabled = false;
    }
    if (rt->vdec_frame_held) {
        kd_mpi_vdec_release_frame(rt->ch, &rt->vdec_frame);
        memset(&rt->vdec_frame, 0, sizeof(rt->vdec_frame));
        rt->vdec_frame_held = false;
    }
    if (rt->vdec_started) {
        kd_mpi_vdec_stop_chn(rt->ch);
        rt->vdec_started = false;
    }
    /* The SDK VDEC lifecycle requires detaching the output pool before
     * destroying the channel.  Reversing these two operations can leave the
     * kernel vpu_task dereferencing channel state that has already vanished. */
    if (rt->vdec_attached) {
        kd_mpi_vdec_detach_vb_pool(rt->ch);
        rt->vdec_attached = false;
    }
    if (rt->vdec_created) {
        kd_mpi_vdec_destroy_chn(rt->ch);
        rt->vdec_created = false;
    }
    if (rt->vdec_pool_created) {
        vb_destroy_vdec_pool(rt->vdec_poolid);
        rt->vdec_pool_created = false;
        rt->vdec_poolid = VB_INVALID_POOLID;
    }
    if (rt->vdec_chn_requested) {
        kd_mpi_vdec_release_chn(rt->ch);
        rt->vdec_chn_requested = false;
    }

    if (rt->vo_mapped) {
        kd_mpi_sys_munmap(rt->vo_vaddr, rt->vo_map_size);
        rt->vo_mapped = false;
        rt->vo_vaddr = NULL;
        rt->vo_map_size = 0;
    }
    if (rt->vo_block_created) {
        kd_mpi_vb_release_block(rt->vo_block);
        rt->vo_block_created = false;
        rt->vo_block = VB_INVALID_HANDLE;
    }
    if (rt->vo_pool_created) {
        kd_mpi_vb_destory_pool(rt->vo_poolid);
        rt->vo_pool_created = false;
        rt->vo_poolid = VB_INVALID_POOLID;
    }

    if (rt->vb_inited) {
        kd_mpi_vb_exit();
        rt->vb_inited = false;
    }
}

int main(int argc, char **argv)
{
    int ret = 0;
    int width, height;
    int total_frame;
    int frame_num = 0;
    char input_fourcc_text[5];
    char negotiated_fourcc_text[5];
    uint64_t fps_last_ms = 0;
    int fps_frames = 0;
    k_connector_type type;
    bool rotation;
    struct uvc_format format = { 0 };
    k_video_frame_info vf_info;
    k_pixel_format vo_pixel_format;
    sample_uvc_runtime rt = {
        .ch = 0,
        .chn_id = K_VO_LAYER_VIDEO1,
        .vo_poolid = VB_INVALID_POOLID,
        .vdec_poolid = VB_INVALID_POOLID,
        .vo_block = VB_INVALID_HANDLE,
    };
#ifdef DSHANPI_UVC_APP
    uvc_back_osd back_osd = {
        .pool = VB_INVALID_POOLID, .block = VB_INVALID_HANDLE
    };
    uvc_fps_osd fps_osd = {
        .pool = VB_INVALID_POOLID, .block = VB_INVALID_HANDLE
    };
#endif

#ifndef DSHANPI_UVC_APP
    if (argc != 7) {
        print_usage();
        return -1;
    }
    {
        int tmp_type;
        int tmp_rotation;

        if (parse_int_arg(argv[1], &tmp_type) || parse_int_arg(argv[2], &tmp_rotation)) {
            print_usage();
            return -1;
        }
        type = (k_connector_type)tmp_type;
        rotation = (tmp_rotation != 0);
    }

    if (parse_fourcc_arg(argv[3], &format.fourcc)) {
        printf("invalid fourcc: %s\n", argv[3]);
        print_usage();
        return -1;
    }
    if (parse_int_arg(argv[4], &width) || parse_int_arg(argv[5], &height) || parse_int_arg(argv[6], &total_frame) ||
        width <= 0 || height <= 0 || total_frame <= 0) {
        print_usage();
        return -1;
    }
#else
    (void)argc;
    (void)argv;
    type = ST7701_480_640_DSI_V1;
    rotation = true;
    /* Match the stable MicroPython UVC capture lifecycle: select YUY2
     * explicitly, convert each snapshot synchronously, display the converted
     * buffer, then return the USB frame immediately.  Native VIDEO layers on
     * K230 only accept YUV420SP, so this path converts to NV12 instead of the
     * RGB565 used by MicroPython's higher-level Display.show_image(). */
    format.fourcc = USBH_VIDEO_FOURCC_YUY2;
    width = 640;
    height = 480;
    format.frameinterval = 333333;
    total_frame = INT_MAX;
#endif

    printf("type = %d, rotation = %d, fourcc = %s (0x%08x), (%d X %d) = %d frame\n",
           type, rotation, fourcc_to_str(format.fourcc, input_fourcc_text),
           format.fourcc, width, height, total_frame);
    rt.is_mjpeg = (format.fourcc == USBH_VIDEO_FOURCC_MJPEG);
    vo_pixel_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

    exit_flag = 0;
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);
#ifdef DSHANPI_UVC_APP
    pthread_t touch_tid;
    pthread_create(&touch_tid, NULL, touch_thread, NULL);
#endif

    ret = vb_init();
    if (ret) {
        goto cleanup;
    }
    rt.vb_inited = true;

    format.width = width;
    format.height = height;
#ifdef DSHANPI_UVC_APP
    /* UVC.probe() equivalent: a camera can finish enumerating shortly after
     * the application starts, so wait instead of treating that as a crash. */
    while (!exit_flag) {
        char device_info[128];
        if (uvc_host_get_devinfo(device_info, sizeof(device_info)) == 0) {
            printf("detect USB Camera %s\n", device_info);
            break;
        }
        usleep(100000);
    }
    if (exit_flag) {
        goto cleanup;
    }
#endif
    ret = uvc_host_init(&format);
    if (ret) {
        printf("uvc_host_init fail\n");
        goto cleanup;
    }

    ret = uvc_host_start_stream();
    if (ret) {
        printf("uvc start stream fail\n");
        goto cleanup;
    }
    rt.uvc_started = true;

    width = format.width;
    height = format.height;
    rt.is_mjpeg = (format.fourcc == USBH_VIDEO_FOURCC_MJPEG);
    vo_pixel_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    printf("uvc resolution is (%d X %d), negotiated fourcc=%s (0x%08x)\n",
           width, height, fourcc_to_str(format.fourcc, negotiated_fourcc_text),
           format.fourcc);
    fps_last_ms = utils_cpu_ticks_ms();

    ret = sample_connector_init(type);
    if (ret) {
        goto cleanup;
    }

    if (sample_vo_layer_start(rt.chn_id, width, height, rotation,
                              vo_pixel_format) != 0) {
        ret = -1;
        goto cleanup;
    }
    rt.vo_layer_enabled = true;
#ifdef DSHANPI_UVC_APP
    if (show_back_osd(&back_osd) != 0)
        printf("warning: unable to show UVC back button\n");
    if (show_fps_osd(&fps_osd) != 0)
        printf("warning: unable to show UVC FPS overlay\n");
#endif

    if (rt.is_mjpeg) {
        ret = sample_vdec_start(&rt, width, height);
        if (ret) {
            goto cleanup;
        }
    } else {
        rt.vo_poolid = vb_create_vo_pool(width, height, vo_pixel_format);
        if (rt.vo_poolid == VB_INVALID_POOLID) {
            ret = -1;
            goto cleanup;
        }
        rt.vo_pool_created = true;

        ret = sample_vo_prepare_frame(&rt, &vf_info, width, height,
                                      vo_pixel_format);
        if (ret) {
            ret = -1;
            goto cleanup;
        }
    }

    while (!exit_flag) {
        struct uvc_frame frame;

        memset(&frame, 0, sizeof(frame));
        ret = uvc_host_get_frame(&frame,
#ifdef DSHANPI_UVC_APP
                                 UVC_FRAME_TIMEOUT_MS
#else
                                 5000
#endif
        );
        if (ret) {
#ifdef DSHANPI_UVC_APP
            /*
             * DWC2 can report a real disconnect interrupt even when the
             * camera was not physically touched.  The device is normally
             * enumerated again immediately.  Keep the preview alive and
             * reopen /dev/video0 instead of treating one lost stream as an
             * application-fatal error.
             */
            printf("UVC stream lost; waiting for camera to reconnect...\n");
            uvc_host_exit();
            rt.uvc_started = false;

            while (!exit_flag) {
                struct uvc_format retry_format = {
                    .fourcc = USBH_VIDEO_FOURCC_YUY2,
                    .width = 640,
                    .height = 480,
                    .frameinterval = 333333,
                };

                usleep(UVC_RECONNECT_DELAY_MS * 1000U);
                if (uvc_host_init(&retry_format) == 0 &&
                    uvc_host_start_stream() == 0) {
                    format = retry_format;
                    rt.uvc_started = true;
                    ret = 0;
                    fps_last_ms = utils_cpu_ticks_ms();
                    fps_frames = 0;
                    printf("UVC camera reconnected: %d x %d @ 30 fps\n",
                           format.width, format.height);
                    break;
                }
                uvc_host_exit();
            }

            if (!exit_flag && rt.uvc_started) {
                continue;
            }
            ret = 0;
#else
            if (!exit_flag) {
                printf("uvc_host_get_frame fail\n");
            } else {
                ret = 0;
            }
#endif
            break;
        }

        if (rt.is_mjpeg) {
            k_vdec_supplement_info supplement;

            /* Match MicroPython snapshot ownership: keep the decoded frame
             * alive through display insertion and release it only when the
             * next frame is about to replace it. */
            if (rt.vdec_frame_held) {
                kd_mpi_vdec_release_frame(rt.ch, &rt.vdec_frame);
                memset(&rt.vdec_frame, 0, sizeof(rt.vdec_frame));
                rt.vdec_frame_held = false;
            }

            ret = kd_mpi_vdec_send_stream(rt.ch, &frame.v_stream, 1000);
            if (ret) {
                printf("kd_mpi_vdec_send_stream fail\n");
            } else {
                memset(&supplement, 0, sizeof(supplement));
                ret = kd_mpi_vdec_get_frame(rt.ch, &rt.vdec_frame,
                                            &supplement, 1000);
                if (ret) {
                    printf("kd_mpi_vdec_get_frame fail\n");
                } else {
                    rt.vdec_frame_held = true;
                    ret = kd_mpi_vo_insert_frame(rt.chn_id,
                                                 &rt.vdec_frame);
                    if (ret) {
                        printf("kd_mpi_vo_insert_frame fail\n");
                    }
                }
            }
        } else {
            /* Keep conversion synchronous and copy into a VO-owned buffer
             * before returning the UVC frame.  VIDEO1 only supports NV12. */
            ret = uvc_host_raw_to_nv12(&frame, rt.vo_vaddr,
                                       width * height * 3 / 2);
            if (!ret) {
                ret = kd_mpi_vo_insert_frame(rt.chn_id, &vf_info);
                if (ret) {
                    printf("kd_mpi_vo_insert_frame fail\n");
                }
            }
        }

        int put_ret = uvc_host_put_frame(&frame);
        if (put_ret) {
            printf("uvc_host_put_frame fail\n");
            if (!ret) {
                ret = put_ret;
            }
        }

        if (ret) {
            break;
        }

        fps_frames++;
        uint64_t now_ms = utils_cpu_ticks_ms();
        uint64_t elapsed_ms = now_ms - fps_last_ms;

        if (elapsed_ms >= 1000ULL) {
            double fps = (double)fps_frames * 1000.0 / (double)elapsed_ms;
#ifdef DSHANPI_UVC_APP
            update_fps_osd(&fps_osd, fps);
#else
            printf("fps: %.2f (%d frames / %llums)\n",
                   fps, fps_frames, (unsigned long long)elapsed_ms);
#endif
            fps_last_ms = now_ms;
            fps_frames = 0;
        }

        if (++frame_num >= total_frame) {
            break;
        }
    }

cleanup:
#ifdef DSHANPI_UVC_APP
    hide_fps_osd(&fps_osd);
    hide_back_osd(&back_osd);
#endif
    sample_cleanup(&rt);
#ifdef DSHANPI_UVC_APP
    exit_flag = 1;
    pthread_join(touch_tid, NULL);
#endif

    if (ret) {
        return ret;
    }
    return 0;
}
