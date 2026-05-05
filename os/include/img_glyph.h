#pragma once

#include <stdint.h>

#define IMG_GLYPH_PUA_START  0xE000u
#define IMG_GLYPH_PUA_END    0xE01Fu
#define IMG_GLYPH_SIZE       8u
#define IMG_GLYPH_CELLS      1u  /* 8px = 1 char cell */

/* Assign ARGB image to PUA codepoint. Scales to 8x8 if larger. Returns 0 on success. */
int img_glyph_assign(uint32_t cp, const uint32_t* pixels, uint16_t w, uint16_t h);

/* Clear glyph for codepoint. */
void img_glyph_clear(uint32_t cp);

/* Get glyph data if assigned. Returns 1 if found, 0 otherwise. out_w/out_h are always 8. */
int img_glyph_get(uint32_t cp, const uint32_t** out_pixels, uint16_t* out_w, uint16_t* out_h);
