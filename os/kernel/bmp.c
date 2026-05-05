/*
 * Minimal BMP decoder - 24/32-bit uncompressed only.
 */
#include "bmp.h"
#include <stdint.h>

/* Decode BMP. If out->pixels is NULL, only fills w/h. Returns 0 on success. */
int bmp_decode(const uint8_t* data, uint32_t len, bmp_result_t* out)
{
    if (!data || !out || len < 54) return -1;
    if (data[0] != 'B' || data[1] != 'M') return -2;
    uint32_t px_off = (uint32_t)data[10] | ((uint32_t)data[11] << 8) |
                      ((uint32_t)data[12] << 16) | ((uint32_t)data[13] << 24);
    if (px_off > len) return -3;
    int32_t w = (int32_t)((uint32_t)data[18] | ((uint32_t)data[19] << 8) |
                          ((uint32_t)data[20] << 16) | ((uint32_t)data[21] << 24));
    int32_t h = (int32_t)((uint32_t)data[22] | ((uint32_t)data[23] << 8) |
                          ((uint32_t)data[24] << 16) | ((uint32_t)data[25] << 24));
    uint16_t bpp = (uint16_t)(data[28] | (data[29] << 8));
    uint32_t comp = (uint32_t)data[30] | ((uint32_t)data[31] << 8);

    if (w <= 0 || w > 2048 || (h < 0 ? -h : h) > 2048) return -4;
    if (bpp != 24 && bpp != 32) return -5;
    if (comp != 0) return -6; /* no compression */

    uint32_t abs_h = (uint32_t)(h < 0 ? -h : h);
    out->w = (uint32_t)w;
    out->h = abs_h;
    out->has_alpha = (bpp == 32);
    if (!out->pixels) return 0;

    uint32_t row_bytes = ((uint32_t)w * (bpp / 8) + 3u) & ~3u;
    int top_down = (h < 0);
    const uint8_t* src = data + px_off;

    for (uint32_t y = 0; y < abs_h; y++) {
        uint32_t dst_y = top_down ? y : (abs_h - 1 - y);
        uint32_t* row = out->pixels + dst_y * (uint32_t)w;
        const uint8_t* p = src + (top_down ? y : (abs_h - 1 - y)) * row_bytes;
        for (int32_t x = 0; x < w; x++) {
            uint8_t b = p[x * (bpp / 8)];
            uint8_t g = p[x * (bpp / 8) + 1];
            uint8_t r = p[x * (bpp / 8) + 2];
            uint8_t a = (bpp == 32) ? p[x * 4 + 3] : 255;
            row[x] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
        }
    }
    return 0;
}
