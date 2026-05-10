#pragma once

#include <stdint.h>

#define IMG_GLYPH_PUA_START  0xE000u
#define IMG_GLYPH_PUA_END    0xE0FFu
#define IMG_GLYPH_SIZE       8u
#define IMG_GLYPH_CELLS      1u  /* 8px = 1 char cell */
#define IMG_GLYPH_NAME_MAX   24u
#define IMG_GLYPH_MOUSE_CURSOR_CP (IMG_GLYPH_PUA_START + 4u)

typedef struct img_glyph_info {
    uint32_t cp;
    uint16_t source_w;
    uint16_t source_h;
    uint32_t avg_argb;
    uint32_t revision;
    uint32_t click_count;
    uint32_t last_click_tick;
    uint8_t live;
    char name[IMG_GLYPH_NAME_MAX];
} img_glyph_info_t;

/* Assign ARGB image to PUA codepoint. Scales to 8x8 if larger. Returns 0 on success. */
int img_glyph_assign(uint32_t cp, const uint32_t* pixels, uint16_t w, uint16_t h);
int img_glyph_assign_named(uint32_t cp, const uint32_t* pixels, uint16_t w, uint16_t h, const char* name);
int img_glyph_assign_pattern(uint32_t cp, const char* name);
int img_glyph_ensure_mouse_cursor(void);
int img_glyph_copy(uint32_t from_cp, uint32_t to_cp);
int img_glyph_move(uint32_t from_cp, uint32_t to_cp);
int img_glyph_rename(uint32_t cp, const char* name);
int img_glyph_set_pixel(uint32_t cp, uint16_t x, uint16_t y, uint32_t argb);

/* Clear glyph for codepoint. */
void img_glyph_clear(uint32_t cp);

/* Get glyph data if assigned. Returns 1 if found, 0 otherwise. out_w/out_h are always 8. */
int img_glyph_get(uint32_t cp, const uint32_t** out_pixels, uint16_t* out_w, uint16_t* out_h);

uint32_t img_glyph_count(void);
int img_glyph_next_free(uint32_t* out_cp);
int img_glyph_info(uint32_t cp, img_glyph_info_t* out);
int img_glyph_info_at(uint32_t assigned_index, img_glyph_info_t* out);
int img_glyph_utf8(uint32_t cp, char out[5]);
int img_glyph_click(uint32_t cp, uint32_t tick);
int img_glyph_set_live(uint32_t cp, int on);
int img_glyph_render(uint32_t cp, uint32_t tick, int hovered, uint32_t* out_pixels, uint16_t* out_w, uint16_t* out_h);
int img_glyph_write_lardd(void);
int img_glyph_selftest(void);
