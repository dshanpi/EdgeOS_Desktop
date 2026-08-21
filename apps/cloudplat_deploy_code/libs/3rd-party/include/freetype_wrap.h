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

#include <ft2build.h>
#include FT_FREETYPE_H

enum freetype_wrap_error_code {
    FREETYPE_WRAP_ERR_NONE                 = 0,
    FREETYPE_WRAP_ERR_OPEN_FONT_FAILED     = -1,
    FREETYPE_WRAP_ERR_INIT_FREETYPE_FAILED = -2,
    FREETYPE_WRAP_ERR_NEW_MANAGER_FAILED   = -3,
    FREETYPE_WRAP_ERR_NEW_MAPCACHE_FAILED  = -4,
    FREETYPE_WRAP_ERR_NEW_IMGCACHE_FAILED  = -5,
};

typedef void (*freetype_wrap_draw_bitmap)(void* ctx, int color, int x, int y, FT_Bitmap* bitmap);

// static void inline draw_bitmap(image_t *img, int color, int x, int y, FT_Bitmap *bitmap) {
//     FT_Int i, j, p, q;
//     FT_Int x_max = x + bitmap->width;
//     FT_Int y_max = y + bitmap->rows;

//     for (i = x, p = 0; i < x_max; i++, p++) {
//         for (j = y, q = 0; j < y_max; j++, q++) {
//             if (i < 0 || j < 0 || i >= img->w || j >= img->h)
//                 continue;

//             if(bitmap->buffer[q * bitmap->width + p]) {
//                 imlib_set_pixel(img, i, j, color);
//             }
//         }
//     }
// }

int freetype_wrap_draw_string(int x_off, int y_off, int char_size, const char* str, int color, const char* font_path,
                              freetype_wrap_draw_bitmap draw, void* draw_ctx);

void freetype_wrap_deinit(void);
