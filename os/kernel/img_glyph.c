/*
 * Image glyphs: PUA U+E000..U+E0FF as assignable picture characters.
 * The inline renderer keeps an 8x8 cell, while the registry keeps metadata so
 * the user can treat unused Unicode slots as named local picture characters.
 */
#include "img_glyph.h"
#include "fs.h"
#include "string.h"
#include <stddef.h>

#define SLOTS (IMG_GLYPH_PUA_END - IMG_GLYPH_PUA_START + 1u)
#define PIXELS_PER_SLOT (IMG_GLYPH_SIZE * IMG_GLYPH_SIZE)

static uint32_t s_glyph_data[SLOTS][PIXELS_PER_SLOT];
static uint8_t s_assigned[SLOTS];
static img_glyph_info_t s_info[SLOTS];
static uint32_t s_revision;

static int slot_index(uint32_t cp)
{
    if (cp < IMG_GLYPH_PUA_START || cp > IMG_GLYPH_PUA_END) return -1;
    return (int)(cp - IMG_GLYPH_PUA_START);
}

static uint32_t glyph_strlen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void glyph_copy_name(char* dst, const char* src)
{
    uint32_t i = 0;
    const char* fallback = "picture";
    if (!src || !src[0]) src = fallback;
    while (src[i] && i + 1u < IMG_GLYPH_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t glyph_avg(const uint32_t* pixels, uint16_t w, uint16_t h)
{
    uint64_t a = 0;
    uint64_t r = 0;
    uint64_t g = 0;
    uint64_t b = 0;
    uint32_t n = (uint32_t)w * (uint32_t)h;
    if (!pixels || !n) return 0xFF000000u;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = pixels[i];
        a += (c >> 24) & 0xFFu;
        r += (c >> 16) & 0xFFu;
        g += (c >> 8) & 0xFFu;
        b += c & 0xFFu;
    }
    return ((uint32_t)(a / n) << 24) |
           ((uint32_t)(r / n) << 16) |
           ((uint32_t)(g / n) << 8) |
           (uint32_t)(b / n);
}

static uint32_t glyph_pattern_pixel(uint32_t cp, const char* name, uint16_t x, uint16_t y)
{
    uint32_t seed = cp ^ 0x45A7u;
    uint32_t accent = 0xFF30D5C8u;
    uint32_t warm = 0xFFFFD166u;
    uint32_t ink = 0xFF101018u;
    uint32_t light = 0xFFFFFFFFu;
    uint32_t cool = 0xFF5E8CFFu;

    if (name && strcmp(name, "face") == 0) {
        if ((x == 2u || x == 5u) && y == 2u) return ink;
        if (y == 5u && x >= 2u && x <= 5u) return ink;
        if (x == 0u || x == 7u || y == 0u || y == 7u) return warm;
        return 0xFFFFE8A3u;
    }
    if (name && strcmp(name, "window") == 0) {
        if (x == 0u || x == 7u || y == 0u || y == 7u || x == 3u || y == 3u) return light;
        return ((x + y) & 1u) ? cool : accent;
    }
    if (name && strcmp(name, "spark") == 0) {
        if (x == 3u || y == 3u || x == y || x + y == 7u) return light;
        return ((x * 37u + y * 17u + seed) & 1u) ? 0xFF8A2BE2u : 0xFF1A1A2Eu;
    }
    if (x == 0u || x == 7u || y == 0u || y == 7u) return 0xFF101018u;
    if (((x + y + seed) % 3u) == 0u) return warm;
    if (((x * 2u + y + seed) % 4u) == 0u) return accent;
    return cool;
}

static void glyph_append(FsWritableFile* w, const char* s)
{
    uint32_t len = glyph_strlen(s);
    if (!w || !s || !len) return;
    fs_append(w, (const uint8_t*)s, len);
}

static void glyph_append_u32(FsWritableFile* w, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    if (v == 0) {
        glyph_append(w, "0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        char c = tmp[--n];
        fs_append(w, (const uint8_t*)&c, 1);
    }
}

static void glyph_append_hex4(FsWritableFile* w, uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    glyph_append(w, "U+");
    for (int i = 3; i >= 0; i--) {
        char c = hex[(v >> (uint32_t)(i * 4)) & 0xFu];
        fs_append(w, (const uint8_t*)&c, 1);
    }
}

int img_glyph_assign_named(uint32_t cp, const uint32_t* pixels, uint16_t w, uint16_t h, const char* name)
{
    int idx = slot_index(cp);
    if (idx < 0 || !pixels || w == 0 || h == 0) return -1;

    uint32_t* dst = s_glyph_data[idx];
    uint16_t sz = (uint16_t)IMG_GLYPH_SIZE;

    if (w == sz && h == sz) {
        for (uint32_t i = 0; i < (uint32_t)sz * sz; i++) dst[i] = pixels[i];
    } else {
        for (uint16_t dy = 0; dy < sz; dy++) {
            for (uint16_t dx = 0; dx < sz; dx++) {
                uint16_t sx = (uint32_t)dx * w / sz;
                uint16_t sy = (uint32_t)dy * h / sz;
                if (sx >= w) sx = (uint16_t)(w - 1u);
                if (sy >= h) sy = (uint16_t)(h - 1u);
                dst[dy * sz + dx] = pixels[sy * (uint32_t)w + sx];
            }
        }
    }

    s_assigned[idx] = 1;
    s_revision++;
    s_info[idx].cp = cp;
    s_info[idx].source_w = w;
    s_info[idx].source_h = h;
    s_info[idx].avg_argb = glyph_avg(pixels, w, h);
    s_info[idx].revision = s_revision;
    glyph_copy_name(s_info[idx].name, name);
    return 0;
}

int img_glyph_assign(uint32_t cp, const uint32_t* pixels, uint16_t w, uint16_t h)
{
    return img_glyph_assign_named(cp, pixels, w, h, "picture");
}

int img_glyph_assign_pattern(uint32_t cp, const char* name)
{
    uint32_t pixels[PIXELS_PER_SLOT];
    for (uint16_t y = 0; y < IMG_GLYPH_SIZE; y++) {
        for (uint16_t x = 0; x < IMG_GLYPH_SIZE; x++) {
            pixels[y * IMG_GLYPH_SIZE + x] = glyph_pattern_pixel(cp, name, x, y);
        }
    }
    return img_glyph_assign_named(cp, pixels, IMG_GLYPH_SIZE, IMG_GLYPH_SIZE, name ? name : "pattern");
}

void img_glyph_clear(uint32_t cp)
{
    int idx = slot_index(cp);
    if (idx >= 0) {
        s_assigned[idx] = 0;
        memset(&s_info[idx], 0, sizeof(s_info[idx]));
    }
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

uint32_t img_glyph_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < SLOTS; i++) if (s_assigned[i]) n++;
    return n;
}

int img_glyph_next_free(uint32_t* out_cp)
{
    if (!out_cp) return -1;
    for (uint32_t i = 0; i < SLOTS; i++) {
        if (!s_assigned[i]) {
            *out_cp = IMG_GLYPH_PUA_START + i;
            return 0;
        }
    }
    return -2;
}

int img_glyph_info(uint32_t cp, img_glyph_info_t* out)
{
    int idx = slot_index(cp);
    if (idx < 0 || !out) return -1;
    if (!s_assigned[idx]) return -2;
    *out = s_info[idx];
    return 0;
}

int img_glyph_info_at(uint32_t assigned_index, img_glyph_info_t* out)
{
    uint32_t seen = 0;
    if (!out) return -1;
    for (uint32_t i = 0; i < SLOTS; i++) {
        if (!s_assigned[i]) continue;
        if (seen == assigned_index) {
            *out = s_info[i];
            return 0;
        }
        seen++;
    }
    return -2;
}

int img_glyph_utf8(uint32_t cp, char out[5])
{
    if (!out || cp < IMG_GLYPH_PUA_START || cp > IMG_GLYPH_PUA_END) return -1;
    out[0] = (char)(0xE0u | ((cp >> 12) & 0x0Fu));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    out[3] = '\0';
    out[4] = '\0';
    return 3;
}

int img_glyph_write_lardd(void)
{
    FsWritableFile* w = fs_open_writable("glyphmap.lardd");
    uint32_t count = img_glyph_count();
    if (!w) return -1;
    w->size = 0;
    glyph_append(w, "LARDD 1\n");
    glyph_append(w, "TITLE Image Glyph Map\n");
    glyph_append(w, "TEXT Private-use Unicode slots can be owned by user pictures.\n");
    glyph_append(w, "TEXT Commands: glyph demo, glyph load U+E000 sample.bmp name, glyph auto sample.bmp name, glyph insert U+E000 notes.txt.\n");
    glyph_append(w, "SECTION Assigned\n");
    if (!count) {
        glyph_append(w, "ITEM none\n");
    } else {
        for (uint32_t i = 0; i < count; i++) {
            img_glyph_info_t info;
            if (img_glyph_info_at(i, &info) != 0) continue;
            glyph_append(w, "ITEM ");
            glyph_append_hex4(w, info.cp);
            glyph_append(w, " ");
            glyph_append(w, info.name);
            glyph_append(w, " src=");
            glyph_append_u32(w, info.source_w);
            glyph_append(w, "x");
            glyph_append_u32(w, info.source_h);
            glyph_append(w, " rev=");
            glyph_append_u32(w, info.revision);
            glyph_append(w, "\n");
        }
    }
    glyph_append(w, "END\n");
    return 0;
}

int img_glyph_selftest(void)
{
    uint32_t cp = IMG_GLYPH_PUA_END;
    int idx = slot_index(cp);
    uint32_t backup_pixels[PIXELS_PER_SLOT];
    uint8_t backup_assigned;
    img_glyph_info_t backup_info;
    uint32_t backup_revision;
    const uint32_t* px = NULL;
    uint16_t w = 0;
    uint16_t h = 0;
    img_glyph_info_t info;
    char utf8[5];

    if (idx < 0) return -1;
    for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) backup_pixels[i] = s_glyph_data[idx][i];
    backup_assigned = s_assigned[idx];
    backup_info = s_info[idx];
    backup_revision = s_revision;

    if (img_glyph_assign_pattern(cp, "selftest") != 0 ||
        !img_glyph_get(cp, &px, &w, &h) ||
        w != IMG_GLYPH_SIZE || h != IMG_GLYPH_SIZE ||
        img_glyph_info(cp, &info) != 0 ||
        img_glyph_utf8(cp, utf8) != 3) {
        for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) s_glyph_data[idx][i] = backup_pixels[i];
        s_assigned[idx] = backup_assigned;
        s_info[idx] = backup_info;
        s_revision = backup_revision;
        return -2;
    }

    for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) s_glyph_data[idx][i] = backup_pixels[i];
    s_assigned[idx] = backup_assigned;
    s_info[idx] = backup_info;
    s_revision = backup_revision;
    return 0;
}
