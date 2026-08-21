#include <stdint.h>

#include "screensaver_asset.h"

#include "generated/screensaver_background.inc"

const lv_image_dsc_t dshanpi_screensaver_background = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .w = 640,
        .h = 480,
        .stride = 640 * 4,
    },
    .data_size = sizeof(screensaver_background_map),
    .data = screensaver_background_map,
};
