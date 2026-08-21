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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "drv_touch.h"

/* Touch control commands */
#define DRV_TOUCH_CTRL_GET_INFO       (21 * 0x100 + 1)
#define DRV_TOUCH_CTRL_RESET          (21 * 0x100 + 11)
#define DRV_TOUCH_CTRL_GET_DFT_ROTATE (21 * 0x100 + 12)
#define RT_TOUCH_CTRL_GET_DEVICE_CFG  (21 * 0x100 + 13)

static const int _drv_touch_inst_type; /**< Unique identifier for touch instance type */
static int       _drv_touch_state[KD_HARD_TOUCH_MAX_NUM]; /**< Tracks usage state of each touch interface */

/**
 * @brief Create a touch driver instance
 * @param id Touch interface ID (0 to KD_HARD_TOUCH_MAX_NUM-1)
 * @param inst Double pointer to store the created instance
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Invalid touch ID
 *         -3: Memory allocation failed
 */
int drv_touch_inst_create(int id, drv_touch_inst_t** inst)
{
    int  fd = -1;
    char dev_name[64];

    /* Parameter validation */
    if (inst == NULL) {
        return -1;
    }

    /* Check touch ID range */
    if (KD_HARD_TOUCH_MAX_NUM <= id) {
        printf("[hal_touch]: invalid id\n");
        return -2;
    }

    /* Check if touch is already in use */
    if (0x00 != _drv_touch_state[id]) {
        printf("[hal_touch]: touch%d maybe in use\n", id);
    }

    snprintf(dev_name, sizeof(dev_name), "/dev/touch%d", id);
    dev_name[sizeof(dev_name) - 1] = '\0';

    /* Clean up existing instance if provided */
    if (*inst) {
        drv_touch_inst_destroy(inst);
        *inst = NULL;
    }

    /* Open touch device */
    if (0 > (fd = open(dev_name, O_RDWR | O_NONBLOCK))) {
        printf("[hal_touch]: open %s failed\n", dev_name);
        return -1;
    }

    /* Allocate and initialize instance */
    *inst = (drv_touch_inst_t*)malloc(sizeof(drv_touch_inst_t));
    if (*inst == NULL) {
        printf("[hal_touch]: malloc instance failed");
        close(fd);
        return -3;
    }
    memset(*inst, 0, sizeof(drv_touch_inst_t));

    /* Initialize instance fields */
    (*inst)->base = (void*)&_drv_touch_inst_type;
    (*inst)->id   = id;
    (*inst)->fd   = fd;

    /* Get device information */
    if (0 != drv_touch_get_info(*inst, &(*inst)->info)) {
        printf("[hal_touch]: failed to get device info\n");
        /* Continue anyway, as info may not be critical for basic operation */
    }

    /* Mark touch as in use */
    if ((0 <= id) && (KD_HARD_TOUCH_MAX_NUM > id)) {
        _drv_touch_state[id] = 1;
    }

    return 0;
}

/**
 * @brief Destroy a touch driver instance
 * @param inst Double pointer to the instance to destroy
 */
void drv_touch_inst_destroy(drv_touch_inst_t** inst)
{
    int id, fd;

    /* Parameter validation */
    if (inst == NULL || *inst == NULL) {
        printf("[hal_touch]: inst not touch inst\n");
        return;
    }

    /* Verify instance type */
    if ((void*)&_drv_touch_inst_type != (*inst)->base) {
        printf("[hal_touch]: inst not touch inst\n");
        return;
    }

    /* Get instance properties */
    fd = (*inst)->fd;
    id = (*inst)->id;

    /* Close file descriptor if open */
    if (0 <= fd) {
        close(fd);
    }

    /* Mark touch as available */
    if ((0 <= id) && (KD_HARD_TOUCH_MAX_NUM > id)) {
        _drv_touch_state[id] = 0;
    }

    /* Free instance memory */
    free(*inst);
    *inst = NULL;
}

/**
 * @brief Read touch data from touch device
 * @param inst Touch instance
 * @param touch_data Buffer to store touch data
 * @param max_points Maximum number of points to read
 * @return Number of points read on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Read error
 */
int drv_touch_read(drv_touch_inst_t* inst, struct drv_touch_data* touch_data, int max_points)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1 || touch_data == NULL) {
        return -1;
    }
    if (max_points <= 0 || max_points > DRV_TOUCH_POINT_NUMBER_MAX) {
        return -1;
    }

    /* Perform read operation */
    ssize_t bytes_read = read(inst->fd, touch_data, max_points * sizeof(struct drv_touch_data));
    if (bytes_read < 0) {
        /* The device is intentionally non-blocking. No queued event is a
         * normal sample result, not a driver failure or an implicit UP. */
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -2;
    }

    /* Return number of points read */
    if (0x00 == bytes_read) {
        return 0;
    }

    return bytes_read / sizeof(struct drv_touch_data);
}

/**
 * @brief Get touch device information
 * @param inst Touch instance
 * @param info Structure to store device information
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_touch_get_info(drv_touch_inst_t* inst, struct drv_touch_info* info)
{
    /* Parameter validation */
    if (inst == NULL || info == NULL || inst->fd == -1) {
        return -1;
    }

    /* Get device information via IOCTL */
    if (ioctl(inst->fd, DRV_TOUCH_CTRL_GET_INFO, info) < 0) {
        return -2;
    }

    return 0;
}

/**
 * @brief Reset touch device
 * @param inst Touch instance
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_touch_reset(drv_touch_inst_t* inst)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1) {
        return -1;
    }

    /* Reset device via IOCTL */
    if (ioctl(inst->fd, DRV_TOUCH_CTRL_RESET, NULL) < 0) {
        return -2;
    }

    return 0;
}

/**
 * @brief Get default rotation setting
 * @param inst Touch instance
 * @param rotate Pointer to store rotation value
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_touch_get_default_rotate(drv_touch_inst_t* inst, int* rotate)
{
    /* Parameter validation */
    if (inst == NULL || rotate == NULL || inst->fd == -1) {
        return -1;
    }

    /* Get default rotation via IOCTL */
    if (ioctl(inst->fd, DRV_TOUCH_CTRL_GET_DFT_ROTATE, rotate) < 0) {
        return -2;
    }

    return 0;
}

int drv_touch_get_config(drv_touch_inst_t* inst, struct drv_touch_config_t* cfg)
{
    /* Parameter validation */
    if (inst == NULL || cfg == NULL || inst->fd == -1) {
        return -1;
    }

    /* Get default rotation via IOCTL */
    if (ioctl(inst->fd, RT_TOUCH_CTRL_GET_DEVICE_CFG, cfg) < 0) {
        return -2;
    }

    return 0;
}
