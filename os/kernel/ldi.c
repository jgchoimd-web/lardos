/*
 * LDI - LardOS Image format
 * v1 header: "LDIM" (4) + ver(1) + w(2) + h(2) + bpp(1) = 10 bytes
 * v1 data: BGR (bpp=24) or BGRA (bpp=32), row-major
 *
 * v2 text starts with "LDI2" and mixes vector commands with palette bitmap
 * rows. It is meant for editable UI/app icon assets, not core window chrome.
 */
#include "ldi.h"
#include "string.h"
#include <stddef.h>

static int ldi_is_ws(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int ldi_hex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static const char* ldi_skip_ws(const char* s)
{
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static int ldi_streq(const char* a, const char* b)
{
    return strcmp(a ? a : "", b ? b : "") == 0;
}

static int ldi_next_line(const uint8_t* data, uint32_t len, uint32_t* pos,
                         char* out, uint32_t cap)
{
    uint32_t n = 0;
    if (!data || !pos || !out || cap == 0 || *pos >= len) return 0;
    while (*pos < len) {
        char ch = (char)data[(*pos)++];
        if (ch == '\n') break;
        if (ch == '\r') continue;
        if (n + 1u < cap) out[n++] = ch;
    }
    out[n] = '\0';
    return 1;
}

static void ldi_split_first(char* line, char** word, char** rest)
{
    char* s = line;
    if (word) *word = line;
    if (rest) *rest = line;
    while (*s == ' ' || *s == '\t') s++;
    if (word) *word = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) {
        *s++ = '\0';
        s = (char*)ldi_skip_ws(s);
    }
    if (rest) *rest = s;
}

static uint32_t ldi_parse_u32_adv(const char** sp, uint32_t fallback)
{
    const char* s = ldi_skip_ws(sp ? *sp : "");
    uint32_t base = 10;
    uint32_t v = 0;
    int any = 0;
    if (!sp) return fallback;
    if (s[0] == '#') {
        base = 16;
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    while (*s) {
        int d = base == 16 ? ldi_hex(*s) : (*s >= '0' && *s <= '9' ? *s - '0' : -1);
        if (d < 0 || (uint32_t)d >= base) break;
        v = v * base + (uint32_t)d;
        s++;
        any = 1;
    }
    while (*s && !ldi_is_ws(*s)) s++;
    *sp = s;
    return any ? v : fallback;
}

static uint32_t ldi_parse_color_adv(const char** sp, uint32_t fallback)
{
    const char* s = ldi_skip_ws(sp ? *sp : "");
    uint32_t digits = 0;
    uint32_t v = 0;
    if (!sp) return fallback;
    if (s[0] == '#') {
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while (ldi_hex(s[digits]) >= 0) {
        v = (v << 4) | (uint32_t)ldi_hex(s[digits]);
        digits++;
    }
    if (digits == 0) return fallback;
    s += digits;
    while (*s && !ldi_is_ws(*s)) s++;
    *sp = s;
    if (digits > 0 && digits <= 6u) v |= 0xFF000000u;
    return v;
}

static void ldi_set_pixel(uint32_t* pixels, uint32_t w, uint32_t h,
                          int x, int y, uint32_t color)
{
    if (!pixels || x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) return;
    pixels[(uint32_t)y * w + (uint32_t)x] = color;
}

static void ldi_fill_rect(uint32_t* pixels, uint32_t w, uint32_t h,
                          int x, int y, int rw, int rh, uint32_t color)
{
    for (int yy = 0; yy < rh; yy++) {
        for (int xx = 0; xx < rw; xx++) ldi_set_pixel(pixels, w, h, x + xx, y + yy, color);
    }
}

static int ldi_abs_i(int v)
{
    return v < 0 ? -v : v;
}

static void ldi_draw_line(uint32_t* pixels, uint32_t w, uint32_t h,
                          int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = ldi_abs_i(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -ldi_abs_i(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2;
        ldi_set_pixel(pixels, w, h, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int ldi_text_size(const uint8_t* data, uint32_t len, uint32_t* out_w, uint32_t* out_h)
{
    uint32_t pos = 0;
    char line[160];
    char* word;
    char* rest;
    while (ldi_next_line(data, len, &pos, line, sizeof(line))) {
        ldi_split_first(line, &word, &rest);
        if (!word[0] || word[0] == '#') continue;
        if (ldi_streq(word, "SIZE")) {
            const char* p = rest;
            uint32_t w = ldi_parse_u32_adv(&p, 0);
            uint32_t h = ldi_parse_u32_adv(&p, 0);
            if (w == 0 || h == 0 || w > 2048u || h > 2048u) return -1;
            if (out_w) *out_w = w;
            if (out_h) *out_h = h;
            return 0;
        }
    }
    return -2;
}

static int ldi_decode_text_v2(const uint8_t* data, uint32_t len, ldi_result_t* out)
{
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t pos = 0;
    uint32_t palette[256];
    char line[160];
    int seen_header = 0;
    if (ldi_text_size(data, len, &w, &h) != 0) return -4;
    out->w = w;
    out->h = h;
    out->bpp = 32;
    if (!out->pixels) return 0;

    for (uint32_t i = 0; i < 256u; i++) palette[i] = 0;
    palette[(uint8_t)'.'] = 0x00000000u;
    palette[(uint8_t)' '] = 0x00000000u;
    for (uint32_t i = 0; i < w * h; i++) out->pixels[i] = 0x00000000u;

    while (ldi_next_line(data, len, &pos, line, sizeof(line))) {
        char* word;
        char* rest;
        ldi_split_first(line, &word, &rest);
        if (!word[0] || word[0] == '#') continue;
        if (ldi_streq(word, "LDI2") || ldi_streq(word, "LDIM2")) {
            seen_header = 1;
            continue;
        }
        if (!seen_header) return -5;
        if (ldi_streq(word, "SIZE")) continue;
        if (ldi_streq(word, "END")) break;
        if (ldi_streq(word, "CLEAR")) {
            const char* p = rest;
            uint32_t c = ldi_parse_color_adv(&p, 0x00000000u);
            for (uint32_t i = 0; i < w * h; i++) out->pixels[i] = c;
            continue;
        }
        if (ldi_streq(word, "PAL")) {
            const char* p = ldi_skip_ws(rest);
            uint8_t key;
            if (!p[0]) continue;
            key = (uint8_t)p[0];
            p++;
            palette[key] = ldi_parse_color_adv(&p, 0x00000000u);
            continue;
        }
        if (ldi_streq(word, "PIX")) {
            const char* p = rest;
            int x = (int)ldi_parse_u32_adv(&p, 0);
            int y = (int)ldi_parse_u32_adv(&p, 0);
            uint32_t c = ldi_parse_color_adv(&p, 0xFFFFFFFFu);
            ldi_set_pixel(out->pixels, w, h, x, y, c);
            continue;
        }
        if (ldi_streq(word, "RECT")) {
            const char* p = rest;
            int x = (int)ldi_parse_u32_adv(&p, 0);
            int y = (int)ldi_parse_u32_adv(&p, 0);
            int rw = (int)ldi_parse_u32_adv(&p, 0);
            int rh = (int)ldi_parse_u32_adv(&p, 0);
            uint32_t c = ldi_parse_color_adv(&p, 0xFFFFFFFFu);
            ldi_fill_rect(out->pixels, w, h, x, y, rw, rh, c);
            continue;
        }
        if (ldi_streq(word, "LINE")) {
            const char* p = rest;
            int x0 = (int)ldi_parse_u32_adv(&p, 0);
            int y0 = (int)ldi_parse_u32_adv(&p, 0);
            int x1 = (int)ldi_parse_u32_adv(&p, 0);
            int y1 = (int)ldi_parse_u32_adv(&p, 0);
            uint32_t c = ldi_parse_color_adv(&p, 0xFFFFFFFFu);
            ldi_draw_line(out->pixels, w, h, x0, y0, x1, y1, c);
            continue;
        }
        if (ldi_streq(word, "BITS") || ldi_streq(word, "BITMAP")) {
            const char* p = rest;
            int bx = (int)ldi_parse_u32_adv(&p, 0);
            int by = (int)ldi_parse_u32_adv(&p, 0);
            int bw = (int)ldi_parse_u32_adv(&p, 0);
            int bh = (int)ldi_parse_u32_adv(&p, 0);
            for (int row = 0; row < bh && ldi_next_line(data, len, &pos, line, sizeof(line)); row++) {
                for (int col = 0; col < bw && line[col]; col++) {
                    uint32_t c = palette[(uint8_t)line[col]];
                    if ((c >> 24) != 0) ldi_set_pixel(out->pixels, w, h, bx + col, by + row, c);
                }
            }
            continue;
        }
    }
    return seen_header ? 0 : -6;
}

int ldi_decode(const uint8_t* data, uint32_t len, ldi_result_t* out)
{
    if (!data || !out) return -1;
    if (len >= 5u && ((memcmp(data, "LDI2", 4) == 0) ||
                      (memcmp(data, "LDIM2", 5) == 0))) {
        return ldi_decode_text_v2(data, len, out);
    }
    if (len < 10) return -1;
    uint32_t magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    if (magic != LDI_MAGIC)
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

int ldi_selftest(void)
{
    static const uint8_t sample[] =
        "LDI2\n"
        "SIZE 4 4\n"
        "CLEAR 0x00000000\n"
        "RECT 0 0 4 4 0xFF102030\n"
        "LINE 0 0 3 3 0xFFFFFFFF\n"
        "PAL A 0xFFFF0000\n"
        "PAL B 0xFF00FF00\n"
        "BITS 1 1 2 2\n"
        "AB\n"
        "BA\n"
        "END\n";
    uint32_t pixels[16];
    ldi_result_t meta = { 0, 0, 0, 0 };
    ldi_result_t img = { pixels, 0, 0, 0 };
    if (ldi_decode(sample, sizeof(sample) - 1u, &meta) != 0) return -1;
    if (meta.w != 4u || meta.h != 4u || meta.bpp != 32) return -2;
    if (ldi_decode(sample, sizeof(sample) - 1u, &img) != 0) return -3;
    if (pixels[0] != 0xFFFFFFFFu) return -4;
    if (pixels[5] != 0xFFFF0000u) return -5;
    if (pixels[6] != 0xFF00FF00u) return -6;
    if (pixels[10] != 0xFFFF0000u) return -7;
    return 0;
}
