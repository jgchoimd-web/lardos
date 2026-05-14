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

    if (name && strcmp(name, "mouse") == 0) {
        static const char* pointer[IMG_GLYPH_SIZE] = {
            "W.......",
            "WK......",
            "WWK.....",
            "WWWK....",
            "WWWWK...",
            "WWKKK...",
            "WK......",
            "K......."
        };
        char p = pointer[y][x];
        if (p == 'K') return ink;
        if (p == 'W') return light;
        return 0x00000000u;
    }
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

static uint8_t glyph_clamp8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static uint32_t glyph_brighten(uint32_t c, int delta)
{
    uint32_t a = c & 0xFF000000u;
    int r = (int)((c >> 16) & 0xFFu) + delta;
    int g = (int)((c >> 8) & 0xFFu) + delta;
    int b = (int)(c & 0xFFu) + delta;
    return a | ((uint32_t)glyph_clamp8(r) << 16) |
           ((uint32_t)glyph_clamp8(g) << 8) |
           (uint32_t)glyph_clamp8(b);
}

static uint32_t glyph_mix(uint32_t a, uint32_t b, uint32_t amount)
{
    uint32_t ar = (a >> 16) & 0xFFu, ag = (a >> 8) & 0xFFu, ab = a & 0xFFu;
    uint32_t br = (b >> 16) & 0xFFu, bg = (b >> 8) & 0xFFu, bb = b & 0xFFu;
    if (amount > 255u) amount = 255u;
    ar = (ar * (255u - amount) + br * amount) / 255u;
    ag = (ag * (255u - amount) + bg * amount) / 255u;
    ab = (ab * (255u - amount) + bb * amount) / 255u;
    return 0xFF000000u | (ar << 16) | (ag << 8) | ab;
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
    s_info[idx].click_count = 0;
    s_info[idx].last_click_tick = 0;
    s_info[idx].live = 1;
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
    int r;
    for (uint16_t y = 0; y < IMG_GLYPH_SIZE; y++) {
        for (uint16_t x = 0; x < IMG_GLYPH_SIZE; x++) {
            pixels[y * IMG_GLYPH_SIZE + x] = glyph_pattern_pixel(cp, name, x, y);
        }
    }
    r = img_glyph_assign_named(cp, pixels, IMG_GLYPH_SIZE, IMG_GLYPH_SIZE, name ? name : "pattern");
    if (r == 0 && name && strcmp(name, "mouse") == 0) {
        int idx = slot_index(cp);
        if (idx >= 0) s_info[idx].live = 0;
    }
    return r;
}

int img_glyph_ensure_mouse_cursor(void)
{
    img_glyph_info_t info;
    if (img_glyph_info(IMG_GLYPH_MOUSE_CURSOR_CP, &info) == 0) return 0;
    return img_glyph_assign_pattern(IMG_GLYPH_MOUSE_CURSOR_CP, "mouse");
}

int img_glyph_copy(uint32_t from_cp, uint32_t to_cp)
{
    int from = slot_index(from_cp);
    int to = slot_index(to_cp);
    if (from < 0 || to < 0) return -1;
    if (!s_assigned[from]) return -2;

    for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) s_glyph_data[to][i] = s_glyph_data[from][i];
    s_assigned[to] = 1;
    s_revision++;
    s_info[to] = s_info[from];
    s_info[to].cp = to_cp;
    s_info[to].revision = s_revision;
    return 0;
}

int img_glyph_move(uint32_t from_cp, uint32_t to_cp)
{
    int from = slot_index(from_cp);
    int to = slot_index(to_cp);
    if (from < 0 || to < 0) return -1;
    if (!s_assigned[from]) return -2;
    if (from == to) {
        s_revision++;
        s_info[from].revision = s_revision;
        return 0;
    }

    if (img_glyph_copy(from_cp, to_cp) != 0) return -3;
    s_assigned[from] = 0;
    memset(&s_info[from], 0, sizeof(s_info[from]));
    return 0;
}

int img_glyph_rename(uint32_t cp, const char* name)
{
    int idx = slot_index(cp);
    if (idx < 0) return -1;
    if (!s_assigned[idx]) return -2;
    glyph_copy_name(s_info[idx].name, name);
    s_revision++;
    s_info[idx].revision = s_revision;
    return 0;
}

int img_glyph_set_pixel(uint32_t cp, uint16_t x, uint16_t y, uint32_t argb)
{
    int idx = slot_index(cp);
    if (idx < 0 || x >= IMG_GLYPH_SIZE || y >= IMG_GLYPH_SIZE) return -1;
    if (!s_assigned[idx]) return -2;
    s_glyph_data[idx][(uint32_t)y * IMG_GLYPH_SIZE + x] = argb;
    s_revision++;
    s_info[idx].avg_argb = glyph_avg(s_glyph_data[idx], IMG_GLYPH_SIZE, IMG_GLYPH_SIZE);
    s_info[idx].revision = s_revision;
    return 0;
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

int img_glyph_click(uint32_t cp, uint32_t tick)
{
    int idx = slot_index(cp);
    if (idx < 0 || !s_assigned[idx]) return -1;
    s_info[idx].click_count++;
    s_info[idx].last_click_tick = tick;
    s_revision++;
    s_info[idx].revision = s_revision;
    return 0;
}

int img_glyph_set_live(uint32_t cp, int on)
{
    int idx = slot_index(cp);
    if (idx < 0 || !s_assigned[idx]) return -1;
    s_info[idx].live = on ? 1u : 0u;
    s_revision++;
    s_info[idx].revision = s_revision;
    return 0;
}

int img_glyph_render(uint32_t cp, uint32_t tick, int hovered, uint32_t* out_pixels, uint16_t* out_w, uint16_t* out_h)
{
    int idx = slot_index(cp);
    uint32_t click_age;
    int clicked_recent = 0;
    int pulse = 0;
    if (idx < 0 || !s_assigned[idx] || !out_pixels) return 0;

    click_age = tick - s_info[idx].last_click_tick;
    clicked_recent = s_info[idx].click_count && click_age < 24u;
    if (s_info[idx].live) {
        uint32_t phase = (tick + (cp & 31u)) & 31u;
        pulse = (int)(phase < 16u ? phase : (31u - phase)) - 4;
    }
    if (hovered) pulse += 18;
    if (clicked_recent) pulse += (int)(24u - click_age) * 3;

    for (uint16_t y = 0; y < IMG_GLYPH_SIZE; y++) {
        for (uint16_t x = 0; x < IMG_GLYPH_SIZE; x++) {
            uint32_t c = s_glyph_data[idx][y * IMG_GLYPH_SIZE + x];
            if ((c >> 24) == 0) {
                out_pixels[y * IMG_GLYPH_SIZE + x] = c;
                continue;
            }
            if (s_info[idx].live && (((uint32_t)x + (uint32_t)y + tick) & 7u) == 0u) {
                c = glyph_mix(c, 0xFFFFFFFFu, 40u);
            }
            if (pulse) c = glyph_brighten(c, pulse);
            if ((hovered || clicked_recent) && (x == 0u || y == 0u || x == IMG_GLYPH_SIZE - 1u || y == IMG_GLYPH_SIZE - 1u)) {
                c = clicked_recent ? 0xFFFFD166u : 0xFFFFFFFFu;
            }
            out_pixels[y * IMG_GLYPH_SIZE + x] = c;
        }
    }
    if (out_w) *out_w = IMG_GLYPH_SIZE;
    if (out_h) *out_h = IMG_GLYPH_SIZE;
    return 1;
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
    glyph_append(w, "TEXT Commands: glyph demo, glyph load U+E000 sample.bmp name, glyph move/copy/rename/pixel, glyph live/click/insert, glyph write.\n");
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
            glyph_append(w, " live=");
            glyph_append(w, info.live ? "on" : "off");
            glyph_append(w, " clicks=");
            glyph_append_u32(w, info.click_count);
            glyph_append(w, "\n");
        }
    }
    glyph_append(w, "END\n");
    return 0;
}

int img_glyph_selftest(void)
{
    uint32_t cp = IMG_GLYPH_PUA_END;
    uint32_t scratch[3] = { IMG_GLYPH_PUA_END, IMG_GLYPH_PUA_END - 1u, IMG_GLYPH_PUA_END - 2u };
    uint32_t backup_pixels[3][PIXELS_PER_SLOT];
    uint32_t render_pixels[PIXELS_PER_SLOT];
    uint8_t backup_assigned[3];
    img_glyph_info_t backup_info[3];
    uint32_t backup_revision;
    const uint32_t* px = NULL;
    uint16_t w = 0;
    uint16_t h = 0;
    img_glyph_info_t info;
    char utf8[5];

    for (uint32_t s = 0; s < 3u; s++) {
        int idx = slot_index(scratch[s]);
        if (idx < 0) return -1;
        for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) backup_pixels[s][i] = s_glyph_data[idx][i];
        backup_assigned[s] = s_assigned[idx];
        backup_info[s] = s_info[idx];
    }
    backup_revision = s_revision;

    if (img_glyph_assign_pattern(cp, "selftest") != 0 ||
        !img_glyph_get(cp, &px, &w, &h) ||
        w != IMG_GLYPH_SIZE || h != IMG_GLYPH_SIZE ||
        img_glyph_info(cp, &info) != 0 ||
        img_glyph_rename(cp, "edit-test") != 0 ||
        img_glyph_set_pixel(cp, 0, 0, 0xFFFF00FFu) != 0 ||
        img_glyph_copy(cp, (uint32_t)(cp - 1u)) != 0 ||
        img_glyph_move((uint32_t)(cp - 1u), (uint32_t)(cp - 2u)) != 0 ||
        img_glyph_utf8(cp, utf8) != 3 ||
        img_glyph_click(cp, 7u) != 0 ||
        img_glyph_set_live(cp, 1) != 0 ||
        img_glyph_render(cp, 9u, 1, render_pixels, &w, &h) != 1) {
        for (uint32_t s = 0; s < 3u; s++) {
            int idx = slot_index(scratch[s]);
            for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) s_glyph_data[idx][i] = backup_pixels[s][i];
            s_assigned[idx] = backup_assigned[s];
            s_info[idx] = backup_info[s];
        }
        s_revision = backup_revision;
        return -2;
    }

    for (uint32_t s = 0; s < 3u; s++) {
        int idx = slot_index(scratch[s]);
        for (uint32_t i = 0; i < PIXELS_PER_SLOT; i++) s_glyph_data[idx][i] = backup_pixels[s][i];
        s_assigned[idx] = backup_assigned[s];
        s_info[idx] = backup_info[s];
    }
    s_revision = backup_revision;
    return 0;
}
