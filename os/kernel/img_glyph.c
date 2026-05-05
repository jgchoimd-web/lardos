/*
 * Image glyphs: PUA U+E000..U+E01F as assignable picture characters (TempleOS-style).
 */
#include "img_glyph.h"
#include <stddef.h>

#define SLOTS (IMG_GLYPH_PUA_END - IMG_GLYPH_PUA_START + 1u)
#define PIXELS_PER_SLOT (IMG_GLYPH_SIZE * IMG_GLYPH_SIZE)

static uint32_t s_glyph_data[SLOTS][PIXELS_PER_SLOT];
static uint8_t s_assigned[SLOTS];

static int slot_index(uint32_t cp)
{
    if (cp < IMG_GLYPH_PUA_START || cp > IMG_GLYPH_PUA_END) return -1;
    return (int)(cp - IMG_GLYPH_PUA_START);
}

int img_glyph_assign(uint32_t cp, const uint32_t* pixels, uint16_t w, uint16_t h)
{
    int idx = slot_index(cp);
    if (idx < 0 || !pixels || w == 0 || h == 0) return -1;

    uint32_t* dst = s_glyph_data[idx];
    uint16_t sz = (uint16_t)IMG_GLYPH_SIZE;

    if (w == sz && h == sz) {
        for (uint32_t i = 0; i < (uint32_t)sz * sz; i++) dst[i] = pixels[i];
    } else {
        /* Simple nearest-neighbor scale down */
        for (uint16_t dy = 0; dy < sz; dy++) {
            for (uint16_t dx = 0; dx < sz; dx++) {
                uint16_t sx = (uint32_t)dx * w / sz;
                uint16_t sy = (uint32_t)dy * h / sz;
                if (sx >= w) sx = w - 1;
                if (sy >= h) sy = h - 1;
                dst[dy * sz + dx] = pixels[sy * (uint32_t)w + sx];
            }
        }
    }
    s_assigned[idx] = 1;
    return 0;
}

void img_glyph_clear(uint32_t cp)
{
    int idx = slot_index(cp);
    if (idx >= 0) s_assigned[idx] = 0;
}

int img_glyph_get(uint32_t cp, const uint32_t** out_pixels, uint16_t* out_w, uint16_t* out_h)
{
    int idx = slot_index(cp);
    if (idx < 0 || !s_assigned[idx] || !out_pixels) return 0;
    *out_pixels = s_glyph_data[idx];
    if (out_w) *out_w = IMG_GLYPH_SIZE;
    if (out_h) *out_h = IMG_GLYPH_SIZE;
    return 1;
}
