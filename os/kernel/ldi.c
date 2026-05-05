/*
 * LDI - LardOS Image format
 * Header: "LDIM" (4) + ver(1) + w(2) + h(2) + bpp(1) = 10 bytes
 * Data: BGR (bpp=24) or BGRA (bpp=32), row-major
 */
#include "ldi.h"
#include <stddef.h>

int ldi_decode(const uint8_t* data, uint32_t len, ldi_result_t* out)
{
    if (!data || !out || len < 10) return -1;
    if ((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24) != LDI_MAGIC)
        return -2;
    if (data[4] != 1) return -3; /* version */
    uint16_t w = (uint16_t)(data[5] | (data[6] << 8));
    uint16_t h = (uint16_t)(data[7] | (data[8] << 8));
    uint8_t bpp = data[9];
    if (w == 0 || h == 0 || w > 2048 || h > 2048) return -4;
    if (bpp != 24 && bpp != 32) return -5;
    uint32_t row_bytes = (uint32_t)w * (bpp / 8);
    if (len < 10 + row_bytes * (uint32_t)h) return -6;

    out->w = w;
    out->h = h;
    out->bpp = bpp;
    if (!out->pixels) return 0;

    const uint8_t* src = data + 10;
    for (uint32_t y = 0; y < (uint32_t)h; y++) {
        uint32_t* row = out->pixels + y * (uint32_t)w;
        const uint8_t* p = src + y * row_bytes;
        for (uint32_t x = 0; x < (uint32_t)w; x++) {
            uint8_t b = p[x * (bpp / 8)];
            uint8_t g = p[x * (bpp / 8) + 1];
            uint8_t r = p[x * (bpp / 8) + 2];
            uint8_t a = (bpp == 32) ? p[x * 4 + 3] : 255;
            row[x] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
        }
    }
    return 0;
}
