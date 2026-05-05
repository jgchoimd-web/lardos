/*
 * SSAV - LardOS Screensaver format decoder.
 */
#include "ssav.h"
#include <stddef.h>

int ssav_decode(const uint8_t* data, uint32_t len, ssav_t* out)
{
    if (!data || !out || len < 16) return -1;
    uint32_t mag = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    if (mag != SSAV_MAGIC) return -2;
    if (data[4] != 1) return -3;

    out->delay_cs = data[5];
    uint16_t frame_count = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
    out->w = (uint16_t)(data[10] | (data[11] << 8));
    out->h = (uint16_t)(data[12] | (data[13] << 8));
    out->bpp = data[14];
    if (out->w == 0 || out->h == 0 || out->w > SSAV_MAX_W || out->h > SSAV_MAX_H) return -4;
    if (out->bpp != 24 && out->bpp != 32) return -5;
    if (frame_count == 0 || frame_count > SSAV_MAX_FRAMES) return -6;
    out->frame_count = frame_count;

    uint32_t bytes_per_frame = (uint32_t)out->w * out->h * (out->bpp / 8);
    if (len < 16 + bytes_per_frame * (uint32_t)frame_count) return -7;
    out->frame_data = data + 16;
    return 0;
}

int ssav_decode_frame(const ssav_t* s, uint16_t frame, uint32_t* out)
{
    if (!s || !out || frame >= s->frame_count) return -1;
    uint32_t bytes_per_px = s->bpp / 8;
    uint32_t stride = (uint32_t)s->w * s->h * bytes_per_px;
    const uint8_t* src = s->frame_data + (uint32_t)frame * stride;

    for (uint32_t i = 0; i < (uint32_t)s->w * s->h; i++) {
        uint8_t b = src[i * bytes_per_px];
        uint8_t g = src[i * bytes_per_px + 1];
        uint8_t r = src[i * bytes_per_px + 2];
        uint8_t a = (s->bpp == 32) ? src[i * 4 + 3] : 255;
        out[i] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    }
    return 0;
}
