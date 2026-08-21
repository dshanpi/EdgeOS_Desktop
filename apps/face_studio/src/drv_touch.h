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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

/* The Face Studio only needs the basic touch API below. Keep the optional
 * CanMV configuration type opaque so this standalone AI executable does not
 * depend on the full MicroPython HAL include tree. */
struct drv_touch_config_t;

#define KD_HARD_TOUCH_MAX_NUM (10)

/* Touch event definitions */
#define DRV_TOUCH_EVENT_NONE (0) /* Touch none */
#define DRV_TOUCH_EVENT_UP   (1) /* Touch up event */
#define DRV_TOUCH_EVENT_DOWN (2) /* Touch down event */
#define DRV_TOUCH_EVENT_MOVE (3) /* Touch move event */

/* Touch rotation definitions */
#define DRV_TOUCH_ROTATE_DEGREE_0   (0)
#define DRV_TOUCH_ROTATE_DEGREE_90  (1)
#define DRV_TOUCH_ROTATE_DEGREE_180 (2)
#define DRV_TOUCH_ROTATE_DEGREE_270 (3)
#define DRV_TOUCH_ROTATE_SWAP_XY    (4)

/* Maximum number of touch points */
#define DRV_TOUCH_POINT_NUMBER_MAX (10)
#define DRV_TOUCH_TIMEOUT_MS       (1000)

/* Touch data structure */
struct drv_touch_data {
    uint8_t  event; /* The touch event of the data */
    uint8_t  track_id; /* Track id of point */
    uint8_t  width; /* Point of width */
    uint16_t x_coordinate; /* Point of x coordinate */
    uint16_t y_coordinate; /* Point of y coordinate */
    uint32_t timestamp; /* The timestamp when the data was received */
};

/* Touch information structure */
struct drv_touch_info {
    uint8_t  type; /* The touch type */
    uint8_t  vendor; /* Vendor of touchs */
    uint8_t  point_num; /* Support point num */
    uint32_t range_x; /* X coordinate range */
    uint32_t range_y; /* Y coordinate range */
};

/* Touch instance structure */
typedef struct _drv_touch_inst {
    void* base;

    int                   id, fd;
    struct drv_touch_info info;
} drv_touch_inst_t;

/**
 * @brief Create a touch driver instance
 * @param id Touch interface ID (0 to KD_HARD_TOUCH_MAX_NUM-1)
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Invalid touch ID
 *         -3: Memory allocation failed
 */
int drv_touch_inst_create(int id, drv_touch_inst_t** inst);

/**
 * @brief Destroy a touch driver instance
 * @param inst Double pointer to the instance to destroy
 */
void drv_touch_inst_destroy(drv_touch_inst_t** inst);

/**
 * @brief Read touch data from touch device
 * @param inst Touch instance
 * @param touch_data Buffer to store touch data
 * @param max_points Maximum number of points to read
 * @return Number of points read on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Read error
 */
int drv_touch_read(drv_touch_inst_t* inst, struct drv_touch_data* touch_data, int max_points);

/**
 * @brief Get touch device information
 * @param inst Touch instance
 * @param info Structure to store device information
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_touch_get_info(drv_touch_inst_t* inst, struct drv_touch_info* info);

/**
 * @brief Reset touch device
 * @param inst Touch instance
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_touch_reset(drv_touch_inst_t* inst);

/**
 * @brief Get default rotation setting
 * @param inst Touch instance
 * @param rotate Pointer to store rotation value
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_touch_get_default_rotate(drv_touch_inst_t* inst, int* rotate);

int drv_touch_get_config(drv_touch_inst_t* inst, struct drv_touch_config_t* cfg);

/* Helper macros for getting instance attributes */
#ifndef MEMBER_TYPE
#  ifdef __cplusplus
     // C++ equivalent
#    define MEMBER_TYPE(struct_type, member) decltype(((struct_type*)0)->member)
#  else
     // C extension
#    define MEMBER_TYPE(struct_type, member) typeof(((struct_type*)0)->member)
#  endif
#endif

#define DRV_TOUCH_GET_ATTR_TEMPLATE(struct_type, member)                                                                       \
    static inline __attribute__((always_inline)) MEMBER_TYPE(struct_type, member) drv_touch_##get_##member(struct_type* inst)  \
    {                                                                                                                          \
        if (!inst) {                                                                                                           \
            return -1;                                                                                                         \
        }                                                                                                                      \
        return inst->member;                                                                                                   \
    }

// int drv_touch_get_id(drv_touch_inst_t *inst);
DRV_TOUCH_GET_ATTR_TEMPLATE(drv_touch_inst_t, id)

// int drv_touch_get_fd(drv_touch_inst_t *inst);
DRV_TOUCH_GET_ATTR_TEMPLATE(drv_touch_inst_t, fd)

#ifdef __cplusplus
}
#endif
