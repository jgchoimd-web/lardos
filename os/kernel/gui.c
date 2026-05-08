#include <stdint.h>
#include <stddef.h>
#include "gui.h"
#include "bootinfo.h"
#include "fs.h"
#include "bmp.h"
#include "img_glyph.h"
#include "ssav.h"
#include "unicode.h"
#include "syscall.h"
#include "usermode.h"
#include "lss.h"
#include "lsh.h"
#include "larsh.h"
#include "lar.h"
#include "ps2.h"
#include "kr_basic.h"
#include "exgui.h"
#include "exexgui.h"
#include "guioverlay.h"
#include "lguilib.h"
#include "syscall.h"
#include "lib3d_demo.h"

#define LARSH_VIEW_W 160
#define LARSH_VIEW_H 120

typedef struct {
    uint32_t* fb;
    uint16_t w;
    uint16_t h;
    uint16_t pitch_bytes;
    uint8_t bpp;
} fb_t;

static fb_t g_fb;
static int g_have_fb;
static uint32_t g_bg;

// Simple backbuffer (assumes <= 1024x768x32).
static uint32_t g_backbuf[1024u * 768u];
static fb_t g_bb;
static int g_have_bb;
static const fb_t* g_syscall_target_override;

#define SCREENRAM_MAX_BYTES 8192u
#define SCREENRAM_DEFAULT_W 64u
#define SCREENRAM_DEFAULT_H 16u

static uint8_t g_screenram_shadow[SCREENRAM_MAX_BYTES];
static uint8_t g_screenram_backup[SCREENRAM_MAX_BYTES];
static uint32_t g_screenram_enabled;
static uint32_t g_screenram_x;
static uint32_t g_screenram_y;
static uint32_t g_screenram_w;
static uint32_t g_screenram_h;
static uint32_t g_screenram_capacity;
static uint32_t g_screenram_used;
static uint32_t g_screenram_last_error;

typedef struct {
    int mx;
    int my;
    int buttons;
    int prev_buttons;

    // Simple single window
    int win_x;
    int win_y;
    int win_w;
    int win_h;
    int dragging;
    int drag_off_x;
    int drag_off_y;

    // Button inside window
    int btn_pressed;
    int btn_clicks;

    // Textbox
    int tb_focused;
    char tb[256];
    uint32_t tb_len;
    uint32_t tb_cur; // caret position (0..tb_len)
    int caret_on;
    uint32_t caret_tick;

    int submit_pending;
    char resp[4096];
    int loading;
    int resp_scroll;
    int resp_total_lines;
    int scroll_drag;
    int scroll_drag_off_y;

    int app_id;
    char calc_display[32];
    uint32_t calc_len;
    uint32_t calc_cur;
    int gallery_sel;
    uint32_t gallery_pixels[128 * 128];

    /* LARSH */
    larsh_scene_t larsh_scene;
    uint32_t larsh_tick;
    int larsh_loaded;
    int larsh_playing;
    uint32_t larsh_pixels[LARSH_VIEW_W * LARSH_VIEW_H];

    int user_sandbox;  /* User tab: run with sandbox */

    /* Lafillo: View Source, Save */
    int http_post_mode;
    int lafillo_src_mode;
    char lafillo_raw[4096];
    char lafillo_extracted[4096];

    /* Lafaelo: code editor */
    char lafaelo_buf[8192];
    uint32_t lafaelo_len;
    uint32_t lafaelo_cur;
    int lafaelo_focus;   /* 0=path input, 1=editor content */
    int lafaelo_show_run; /* 1=show Run output (g.resp), 0=show editor */

    /* Screensaver */
    int ss_active;
    uint32_t ss_idle_ticks;
    ssav_t ss_ssav;
    const FsFile* ss_file;
    uint16_t ss_frame;
    uint32_t ss_anim_tick;
    int ss_bx, ss_by, ss_bdx, ss_bdy;
    float ss_angle;
    uint32_t ss_frame_buf[SSAV_MAX_PIXELS];

    /* Settings */
    int settings_open;
    int brightness;   /* 50-150, 100=normal */
    int volume;      /* 0-100 */
    int quality;     /* 0=low 1=med 2=high contrast */
    int slider_drag; /* 0=none 1=bright 2=vol 3=quality */
} gui_state_t;

#define SS_IDLE_THRESHOLD  300   /* ~5 sec at 60 ticks/sec */

static gui_state_t g;

static int fb_from_bootinfo(fb_t* out)
{
    const bootinfo_t* bi = (const bootinfo_t*)(uintptr_t)BOOTINFO_PADDR;
    if (bi->magic != 0x464E4942u || bi->version != 1) {
        return -1;
    }
    if ((bi->fb_bpp != 24 && bi->fb_bpp != 32) || bi->fb_addr_lo == 0) {
        return -2;
    }
    out->fb = (uint32_t*)(uintptr_t)bi->fb_addr_lo;
    out->w = bi->fb_width;
    out->h = bi->fb_height;
    out->pitch_bytes = bi->fb_pitch;
    out->bpp = bi->fb_bpp;
    return 0;
}

static void fb_putpixel(const fb_t* f, uint16_t x, uint16_t y, uint32_t argb);

static uint32_t screenram_rect_capacity(uint32_t w, uint32_t h)
{
    uint64_t cap = (uint64_t)w * (uint64_t)h * 3u;
    if (cap > SCREENRAM_MAX_BYTES) cap = SCREENRAM_MAX_BYTES;
    return (uint32_t)cap;
}

static void screenram_default_rect(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h)
{
    uint32_t rw = SCREENRAM_DEFAULT_W;
    uint32_t rh = SCREENRAM_DEFAULT_H;
    uint32_t sw = g_have_fb ? g_fb.w : 0;
    uint32_t sh = g_have_fb ? g_fb.h : 0;
    if (sw == 0 || sh == 0) {
        *x = *y = *w = *h = 0;
        return;
    }
    if (rw > sw) rw = sw;
    if (rh > sh) rh = sh;
    *x = sw - rw;
    *y = sh - rh;
    *w = rw;
    *h = rh;
}

static int screenram_rect_valid(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!g_have_fb || !g_fb.fb || w == 0 || h == 0) return 0;
    if (x >= g_fb.w || y >= g_fb.h) return 0;
    if (w > (uint32_t)g_fb.w - x || h > (uint32_t)g_fb.h - y) return 0;
    return screenram_rect_capacity(w, h) > 0;
}

static void screenram_flush_to_target(const fb_t* tgt)
{
    if (!tgt || !g_screenram_enabled || g_screenram_capacity == 0) return;
    for (uint32_t i = 0; i < g_screenram_capacity; i += 3u) {
        uint32_t px = i / 3u;
        uint32_t x = g_screenram_x + (px % g_screenram_w);
        uint32_t y = g_screenram_y + (px / g_screenram_w);
        uint32_t r = g_screenram_shadow[i];
        uint32_t gch = (i + 1u < g_screenram_capacity) ? g_screenram_shadow[i + 1u] : 0u;
        uint32_t b = (i + 2u < g_screenram_capacity) ? g_screenram_shadow[i + 2u] : 0u;
        fb_putpixel(tgt, (uint16_t)x, (uint16_t)y, 0xFF000000u | (r << 16) | (gch << 8) | b);
    }
}

static int screenram_configure(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t old_used = g_screenram_used;
    if (!screenram_rect_valid(x, y, w, h)) {
        g_screenram_last_error = 1;
        return -1;
    }
    g_screenram_x = x;
    g_screenram_y = y;
    g_screenram_w = w;
    g_screenram_h = h;
    g_screenram_capacity = screenram_rect_capacity(w, h);
    if (old_used > g_screenram_capacity) old_used = g_screenram_capacity;
    g_screenram_used = old_used;
    g_screenram_last_error = 0;
    return 0;
}

static void fb_blit(const fb_t* dst, const fb_t* src)
{
    uint16_t w = src->w < dst->w ? src->w : dst->w;
    uint16_t h = src->h < dst->h ? src->h : dst->h;
    int br = g.brightness;
    if (br < 50) br = 50;
    if (br > 150) br = 150;
    int q = g.quality;
    if (q < 0) q = 0;
    if (q > 2) q = 2;
    float bright = (float)br / 100.f;
    float contrast = (q == 0) ? 0.85f : (q == 1) ? 1.f : 1.2f;
    for (uint16_t y = 0; y < h; y++) {
        uint32_t* srow = (uint32_t*)((uintptr_t)src->fb + (uintptr_t)src->pitch_bytes * y);
        for (uint16_t x = 0; x < w; x++) {
            uint32_t p = srow[x];
            uint32_t a = (p >> 24) & 0xFF;
            int r = (p >> 16) & 0xFF;
            int g_ = (p >> 8) & 0xFF;
            int b = p & 0xFF;
            r = (int)(((float)r - 128.f) * contrast + 128.f) * (int)(bright * 256) >> 8;
            g_ = (int)(((float)g_ - 128.f) * contrast + 128.f) * (int)(bright * 256) >> 8;
            b = (int)(((float)b - 128.f) * contrast + 128.f) * (int)(bright * 256) >> 8;
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g_ < 0) g_ = 0;
            if (g_ > 255) g_ = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            fb_putpixel(dst, x, y, ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g_ << 8) | (uint32_t)b);
        }
    }
}

static void fb_putpixel(const fb_t* f, uint16_t x, uint16_t y, uint32_t argb)
{
    if (x >= f->w || y >= f->h) return;
    uint8_t* row = (uint8_t*)f->fb + (uintptr_t)f->pitch_bytes * y;
    if (f->bpp == 32) {
        ((uint32_t*)row)[x] = argb;
    } else if (f->bpp == 24) {
        uint8_t* p = row + (uint32_t)x * 3u;
        p[0] = (uint8_t)(argb & 0xFFu);
        p[1] = (uint8_t)((argb >> 8) & 0xFFu);
        p[2] = (uint8_t)((argb >> 16) & 0xFFu);
    }
}

static void fb_clear(const fb_t* f, uint32_t argb)
{
    for (uint16_t y = 0; y < f->h; y++) {
        for (uint16_t x = 0; x < f->w; x++) {
            fb_putpixel(f, x, y, argb);
        }
    }
}

static void fb_fill_rect(const fb_t* f, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t argb)
{
    for (uint16_t yy = 0; yy < h; yy++) {
        for (uint16_t xx = 0; xx < w; xx++) {
            fb_putpixel(f, (uint16_t)(x + xx), (uint16_t)(y + yy), argb);
        }
    }
}

// Tiny 8x8 font for ASCII 32..127 (only a subset here: digits/letters/space/period/colon/slash)
// For now we keep it minimal so the first GUI milestone is visible.
static const uint8_t font8x8[96][8] = {
    // 0x20 ' '
    {0, 0, 0, 0, 0, 0, 0, 0},
    // 0x21 '!' .. 0x2E '.' (leave empty)
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    // 0x2F '/'
    {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    // 0x30 '0'
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00}, // 1
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 9
    // 0x3A ':'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    // 0x3B ';' .. 0x40 '@' (empty)
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    // 0x41 'A'
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, // E
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, // F
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, // G
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // I
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, // J
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // K
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // L
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // M
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // N
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P
    {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00}, // Q
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00}, // R
    {0x3C,0x66,0x30,0x18,0x0C,0x66,0x3C,0x00}, // S
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // V
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // X
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // Y
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // Z
};

static int glyph_is_empty(const uint8_t* g)
{
    for (int i = 0; i < 8; i++) {
        if (g[i] != 0) return 0;
    }
    return 1;
}

static const uint8_t* fb_glyph_for_char(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - ('a' - 'A'));
    }
    if (ch < 32 || ch > 126) {
        ch = '?';
    }

    const uint8_t* g = font8x8[(uint8_t)ch - 32];
    if (ch == ' ' || !glyph_is_empty(g)) {
        return g;
    }

    switch (ch) {
    case '!': { static const uint8_t v[8] = {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}; return v; }
    case '"': { static const uint8_t v[8] = {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}; return v; }
    case '#': { static const uint8_t v[8] = {0x24,0x24,0x7E,0x24,0x7E,0x24,0x24,0x00}; return v; }
    case '$': { static const uint8_t v[8] = {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}; return v; }
    case '%': { static const uint8_t v[8] = {0x62,0x66,0x0C,0x18,0x30,0x66,0x46,0x00}; return v; }
    case '&': { static const uint8_t v[8] = {0x38,0x6C,0x38,0x70,0xDE,0xCC,0x76,0x00}; return v; }
    case '\'': { static const uint8_t v[8] = {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}; return v; }
    case '(': { static const uint8_t v[8] = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}; return v; }
    case ')': { static const uint8_t v[8] = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}; return v; }
    case '*': { static const uint8_t v[8] = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}; return v; }
    case '+': { static const uint8_t v[8] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}; return v; }
    case ',': { static const uint8_t v[8] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}; return v; }
    case '-': { static const uint8_t v[8] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}; return v; }
    case '.': { static const uint8_t v[8] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}; return v; }
    case ';': { static const uint8_t v[8] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}; return v; }
    case '<': { static const uint8_t v[8] = {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}; return v; }
    case '=': { static const uint8_t v[8] = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}; return v; }
    case '>': { static const uint8_t v[8] = {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}; return v; }
    case '?': { static const uint8_t v[8] = {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}; return v; }
    case '@': { static const uint8_t v[8] = {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00}; return v; }
    case '[': { static const uint8_t v[8] = {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}; return v; }
    case '\\': { static const uint8_t v[8] = {0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00}; return v; }
    case ']': { static const uint8_t v[8] = {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}; return v; }
    case '^': { static const uint8_t v[8] = {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}; return v; }
    case '_': { static const uint8_t v[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00}; return v; }
    case '`': { static const uint8_t v[8] = {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}; return v; }
    case '{': { static const uint8_t v[8] = {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}; return v; }
    case '|': { static const uint8_t v[8] = {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}; return v; }
    case '}': { static const uint8_t v[8] = {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}; return v; }
    case '~': { static const uint8_t v[8] = {0x00,0x00,0x32,0x4C,0x00,0x00,0x00,0x00}; return v; }
    default: { static const uint8_t v[8] = {0x3C,0x42,0x06,0x0C,0x18,0x00,0x18,0x00}; return v; }
    }
}

static void fb_draw_char(const fb_t* f, uint16_t x, uint16_t y, char ch, uint32_t fg, uint32_t bg)
{
    const uint8_t* g = fb_glyph_for_char(ch);
    for (uint16_t row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        for (uint16_t col = 0; col < 8; col++) {
            uint32_t c = (bits & (1u << (7 - col))) ? fg : bg;
            fb_putpixel(f, (uint16_t)(x + col), (uint16_t)(y + row), c);
        }
    }
}

static void fb_draw_text_cells(const fb_t* f, uint16_t x, uint16_t y, const char* s,
                               uint16_t max_cells, uint32_t fg, uint32_t bg)
{
    const char* p = s ? s : "";
    uint16_t cell = 0;
    while (*p && cell < max_cells) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        if (cp < 32 || cp > 126) cp = '?';
        fb_draw_char(f, (uint16_t)(x + cell * 8), y, (char)cp, fg, bg);
        cell++;
    }
}

static void fb_draw_text(const fb_t* f, uint16_t x, uint16_t y, const char* s, uint32_t fg, uint32_t bg)
{
    fb_draw_text_cells(f, x, y, s, 65535u, fg, bg);
}

static void fb_draw_image(const fb_t* f, uint16_t x, uint16_t y, const uint32_t* pixels, uint16_t w, uint16_t h)
{
    for (uint16_t py = 0; py < h; py++) {
        for (uint16_t px = 0; px < w; px++) {
            uint32_t argb = pixels[py * (uint32_t)w + px];
            if ((argb >> 24) != 0) fb_putpixel(f, (uint16_t)(x + px), (uint16_t)(y + py), argb);
        }
    }
}

int gui_init(void)
{
    g_have_fb = (fb_from_bootinfo(&g_fb) == 0);
    if (!g_have_fb) return -1;
    g_bg = 0xFF101020;
    lguilib_init();
    const FsFile* lguilib_file = fs_open("default.lguilib");
    if (lguilib_file) (void)lguilib_load_active(lguilib_file->data, lguilib_file->size);
    exgui_init();
    exexgui_init();
    if (g_fb.w <= 1024 && g_fb.h <= 768) {
        g_bb.fb = g_backbuf;
        g_bb.w = g_fb.w;
        g_bb.h = g_fb.h;
        g_bb.pitch_bytes = (uint16_t)(g_fb.w * 4);
        g_bb.bpp = 32;
        g_have_bb = 1;
        fb_clear(&g_bb, g_bg);
        fb_blit(&g_fb, &g_bb);
    } else {
        g_have_bb = 0;
        fb_clear(&g_fb, g_bg);
    }

    // Mouse in center
    g.mx = g_fb.w / 2;
    g.my = g_fb.h / 2;
    g.buttons = 0;
    g.prev_buttons = 0;

    // Window
    int sw = (int)g_fb.w;
    int sh = (int)g_fb.h;
    g.win_w = sw >= 660 ? 640 : sw - 20;
    g.win_h = sh >= 460 ? 420 : sh - 20;
    if (g.win_w < 240) g.win_w = sw > 8 ? sw - 8 : sw;
    if (g.win_h < 180) g.win_h = sh > 8 ? sh - 8 : sh;
    g.win_x = (sw - g.win_w) / 2;
    g.win_y = (sh - g.win_h) / 2;
    if (g.win_x < 0) g.win_x = 0;
    if (g.win_y < 0) g.win_y = 0;
    g.dragging = 0;
    g.btn_pressed = 0;
    g.btn_clicks = 0;
    g.tb_focused = 0;
    g.tb_len = 0;
    g.tb[0] = '\0';
    g.tb_cur = 0;
    g.caret_on = 1;
    g.caret_tick = 0;
    g.submit_pending = 0;
    g.resp[0] = '\0';
    g.loading = 0;
    g.resp_scroll = 0;
    g.resp_total_lines = 0;
    g.scroll_drag = 0;
    g.scroll_drag_off_y = 0;
    g.http_post_mode = 0;
    g.app_id = 0;
    g.calc_display[0] = '0';
    g.calc_display[1] = '\0';
    g.calc_len = 1;
    g.calc_cur = 1;
    g.gallery_sel = -1;
    g.larsh_loaded = 0;
    g.larsh_playing = 0;
    g.larsh_tick = 0;
    g.user_sandbox = 0;
    g.lafillo_src_mode = 0;
    g.lafillo_raw[0] = '\0';
    g.settings_open = 0;
    g.brightness = 100;
    g.volume = 80;
    g.quality = 1;
    g.slider_drag = 0;
    g.lafillo_extracted[0] = '\0';

    // Default URL
    const char* def = "file://lardos.lars";
    for (g.tb_len = 0; def[g.tb_len] && g.tb_len + 1 < sizeof(g.tb); g.tb_len++) g.tb[g.tb_len] = def[g.tb_len];
    g.tb[g.tb_len] = '\0';
    g.tb_cur = g.tb_len;

    /* Screensaver: try default.ssav */
    g.ss_active = 0;
    g.ss_idle_ticks = 0;
    g.ss_file = NULL;
    g.ss_frame = 0;
    g.ss_anim_tick = 0;
    g.ss_bx = 64;
    g.ss_by = 64;
    g.ss_bdx = 4;
    g.ss_bdy = 3;
    const FsFile* sf = fs_open("default.ssav");
    if (sf && ssav_decode(sf->data, sf->size, &g.ss_ssav) == 0) {
        g.ss_file = sf;
    }
    screenram_default_rect(&g_screenram_x, &g_screenram_y, &g_screenram_w, &g_screenram_h);
    g_screenram_capacity = screenram_rect_capacity(g_screenram_w, g_screenram_h);
    g_screenram_used = 0;
    g_screenram_enabled = 0;
    g_screenram_last_error = 0;
    return 0;
}

int gui_screenram_enable(int on)
{
    if (on) {
        if (!g_screenram_capacity) {
            uint32_t x, y, w, h;
            screenram_default_rect(&x, &y, &w, &h);
            if (screenram_configure(x, y, w, h) != 0) return -1;
        }
        g_screenram_enabled = 1;
    } else {
        g_screenram_enabled = 0;
    }
    g_screenram_last_error = 0;
    return 0;
}

int gui_screenram_set_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (screenram_configure(x, y, w, h) != 0) return -1;
    g_screenram_enabled = 1;
    return 0;
}

static int screenram_corner_is(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

int gui_screenram_set_corner(const char* corner, uint32_t w, uint32_t h)
{
    uint32_t x = 0;
    uint32_t y = 0;
    if (!corner || !corner[0]) corner = "br";
    if (w == 0) w = SCREENRAM_DEFAULT_W;
    if (h == 0) h = SCREENRAM_DEFAULT_H;
    if (!g_have_fb || !g_fb.fb) {
        g_screenram_last_error = 1;
        return -1;
    }
    if (w > g_fb.w) w = g_fb.w;
    if (h > g_fb.h) h = g_fb.h;
    if (screenram_corner_is(corner, "tr")) {
        x = (uint32_t)g_fb.w - w;
        y = 0;
    } else if (screenram_corner_is(corner, "bl")) {
        x = 0;
        y = (uint32_t)g_fb.h - h;
    } else if (screenram_corner_is(corner, "tl")) {
        x = 0;
        y = 0;
    } else {
        x = (uint32_t)g_fb.w - w;
        y = (uint32_t)g_fb.h - h;
    }
    return gui_screenram_set_rect(x, y, w, h);
}

int gui_screenram_write(uint32_t offset, const uint8_t* data, uint32_t len)
{
    if (!g_screenram_enabled || !data || offset > g_screenram_capacity) {
        g_screenram_last_error = 2;
        return -1;
    }
    if (len > g_screenram_capacity - offset) len = g_screenram_capacity - offset;
    for (uint32_t i = 0; i < len; i++) g_screenram_shadow[offset + i] = data[i];
    if (offset + len > g_screenram_used) g_screenram_used = offset + len;
    g_screenram_last_error = 0;
    return (int)len;
}

int gui_screenram_read(uint32_t offset, uint8_t* data, uint32_t len)
{
    if (!g_screenram_enabled || !data || offset > g_screenram_capacity) {
        g_screenram_last_error = 2;
        return -1;
    }
    if (len > g_screenram_capacity - offset) len = g_screenram_capacity - offset;
    for (uint32_t i = 0; i < len; i++) data[i] = g_screenram_shadow[offset + i];
    g_screenram_last_error = 0;
    return (int)len;
}

void gui_screenram_clear(void)
{
    for (uint32_t i = 0; i < g_screenram_capacity; i++) g_screenram_shadow[i] = 0;
    g_screenram_used = 0;
    g_screenram_last_error = 0;
}

void gui_screenram_info(gui_screenram_info_t* out)
{
    if (!out) return;
    out->enabled = g_screenram_enabled;
    out->x = g_screenram_x;
    out->y = g_screenram_y;
    out->w = g_screenram_w;
    out->h = g_screenram_h;
    out->capacity = g_screenram_capacity;
    out->used = g_screenram_used;
    out->max_capacity = SCREENRAM_MAX_BYTES;
    out->last_error = g_screenram_last_error;
}

int gui_screenram_selftest(void)
{
    gui_screenram_info_t old;
    int ok = 1;
    uint8_t got[64];
    uint32_t old_cap;
    uint32_t old_used;
    uint32_t old_enabled;

    gui_screenram_info(&old);
    old_cap = old.capacity;
    old_used = old.used;
    old_enabled = old.enabled;
    for (uint32_t i = 0; i < old_cap && i < SCREENRAM_MAX_BYTES; i++) {
        g_screenram_backup[i] = g_screenram_shadow[i];
    }

    if (gui_screenram_set_corner("br", 16u, 8u) != 0) ok = 0;
    if (ok) {
        for (uint32_t i = 0; i < sizeof(got); i++) got[i] = (uint8_t)(0x5Au ^ i);
        if (gui_screenram_write(0, got, sizeof(got)) != (int)sizeof(got)) ok = 0;
        for (uint32_t i = 0; i < sizeof(got); i++) got[i] = 0;
        if (gui_screenram_read(0, got, sizeof(got)) != (int)sizeof(got)) ok = 0;
        for (uint32_t i = 0; i < sizeof(got); i++) {
            if (got[i] != (uint8_t)(0x5Au ^ i)) ok = 0;
        }
    }

    g_screenram_enabled = old_enabled;
    g_screenram_x = old.x;
    g_screenram_y = old.y;
    g_screenram_w = old.w;
    g_screenram_h = old.h;
    g_screenram_capacity = old_cap;
    g_screenram_used = old_used;
    for (uint32_t i = 0; i < old_cap && i < SCREENRAM_MAX_BYTES; i++) {
        g_screenram_shadow[i] = g_screenram_backup[i];
    }
    g_screenram_last_error = ok ? 0u : 3u;
    return ok ? 0 : -1;
}

static void gui_draw_cursor_at(int x, int y, uint32_t color)
{
    // Simple 8x12 block cursor.
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (int)g_fb.w - 8) x = (int)g_fb.w - 8;
    if (y > (int)g_fb.h - 12) y = (int)g_fb.h - 12;
    const fb_t* tgt = g_have_bb ? &g_bb : &g_fb;
    fb_fill_rect(tgt, (uint16_t)x, (uint16_t)y, 8, 12, color);
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < (rx + rw) && y < (ry + rh);
}

static void gui_apply_exexgui_layout(void)
{
    exexgui_layout_t l;
    int pad = 6;
    int label_h = 30;
    int ww;
    int wh;
    if (!g_have_fb || !exexgui_is_enabled()) return;
    if (exexgui_layout_for(g_fb.w, g_fb.h, &l) != 0) return;

    ww = (int)l.gui.w - pad * 2;
    wh = (int)l.gui.h - label_h - pad;
    if (ww < 220) ww = (int)l.gui.w > 8 ? (int)l.gui.w - 8 : (int)l.gui.w;
    if (wh < 170) wh = (int)l.gui.h > 8 ? (int)l.gui.h - 8 : (int)l.gui.h;

    g.win_x = (int)l.gui.x + pad;
    g.win_y = (int)l.gui.y + label_h;
    g.win_w = ww;
    g.win_h = wh;
    if (g.win_x < (int)l.gui.x) g.win_x = (int)l.gui.x;
    if (g.win_y < (int)l.gui.y) g.win_y = (int)l.gui.y;
    if (g.win_x + g.win_w > (int)(l.gui.x + l.gui.w)) g.win_w = (int)(l.gui.x + l.gui.w) - g.win_x;
    if (g.win_y + g.win_h > (int)(l.gui.y + l.gui.h)) g.win_h = (int)(l.gui.y + l.gui.h) - g.win_y;
    if (g.win_w < 1) g.win_w = 1;
    if (g.win_h < 1) g.win_h = 1;
    g.dragging = 0;
}

static void gui_resp_clear(void)
{
    g.resp[0] = '\0';
    g.resp_scroll = 0;
}

static void gui_resp_append_n(const char* s, uint32_t len)
{
    uint32_t pos = 0;
    while (pos + 1 < sizeof(g.resp) && g.resp[pos]) pos++;
    for (uint32_t i = 0; i < len && pos + 1 < sizeof(g.resp); i++) {
        g.resp[pos++] = s[i];
    }
    g.resp[pos] = '\0';
}

static void gui_resp_append(const char* s)
{
    uint32_t len = 0;
    if (!s) return;
    while (s[len]) len++;
    gui_resp_append_n(s, len);
}

static void gui_resp_append_u32(uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    if (v == 0) {
        gui_resp_append("0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        char ch = tmp[--n];
        gui_resp_append_n(&ch, 1);
    }
}

static void gui_tb_set(const char* s)
{
    uint32_t i = 0;
    if (!s) s = "";
    while (s[i] && i + 1 < sizeof(g.tb)) {
        g.tb[i] = s[i];
        i++;
    }
    g.tb[i] = '\0';
    g.tb_len = i;
    g.tb_cur = i;
}

static void gui_lsh_sync_output(void)
{
    const char* out = lsh_get_output();
    uint32_t i = 0;
    while (out[i] && i + 1 < sizeof(g.resp)) {
        g.resp[i] = out[i];
        i++;
    }
    g.resp[i] = '\0';
}

void gui_activate_ring0_shortcut(void)
{
    gui_activity();
    if (g.ss_active) g.ss_active = 0;
    lsh_enter_sum_shortcut();
    if (!g_have_fb) return;
    g.app_id = 7;
    g.tb_focused = 1;
    g.lafaelo_focus = 0;
    g.lafaelo_show_run = 1;
    g.tb_len = 0;
    g.tb_cur = 0;
    g.tb[0] = '\0';
    gui_lsh_sync_output();
}

static void gui_lar_list_cb(const lar_entry_t* entry, void* user)
{
    (void)user;
    gui_resp_append("  ");
    gui_resp_append_n(entry->name, entry->name_len);
    gui_resp_append("  ");
    gui_resp_append_u32(entry->unpacked_size);
    gui_resp_append(entry->method == LAR_METHOD_STORE ? " bytes stored\n" : " bytes unsupported\n");
}

static void gui_lar_show_bundle(void)
{
    const FsFile* f = fs_open("bundle.lar");
    gui_resp_clear();
    gui_resp_append("LAR1 bundle.lar\n");
    gui_resp_append("Enter a member name, then Extract.\n\n");
    if (!f) {
        gui_resp_append("bundle.lar not found.\n");
        return;
    }
    if (lar_list(f->data, f->size, gui_lar_list_cb, NULL) != 0) {
        gui_resp_append("Invalid LAR archive.\n");
    }
}

static void gui_lar_extract_selected(void)
{
    const char* name = g.tb[0] ? g.tb : "hello.txt";
    const FsFile* f = fs_open("bundle.lar");
    FsWritableFile* w = fs_open_writable("lar_extract.txt");
    gui_resp_clear();
    if (!f || !w) {
        gui_resp_append("LAR storage not available.\n");
        return;
    }
    uint32_t out_len = w->cap > 0 ? w->cap - 1 : 0;
    int r = lar_extract(f->data, f->size, name, w->data, &out_len);
    if (r != 0) {
        gui_resp_append("Extract failed: ");
        gui_resp_append(name);
        gui_resp_append("\n");
        return;
    }
    w->size = out_len;
    w->data[out_len] = 0;
    fs_mark_dirty();
    gui_resp_append("Extracted ");
    gui_resp_append(name);
    gui_resp_append(" -> lar_extract.txt\n\n");
    gui_resp_append_n((const char*)w->data, out_len);
}

void gui_activity(void)
{
    g.ss_idle_ticks = 0;
}

/* Load and play LARSH file. Switches to Gallery tab with demo.larsh selected. */
void gui_larsh_play(const char* path)
{
    const FsFile* f = fs_open(path);
    FsWritableFile* w = fs_open_writable(path);
    const uint8_t* d = f ? f->data : (w ? w->data : NULL);
    uint32_t sz = f ? f->size : (w ? w->size : 0);
    if (d && sz > 0 && larsh_parse((const char*)d, sz, &g.larsh_scene) == 0) {
        g.larsh_loaded = 1;
        g.larsh_playing = 1;
        g.larsh_tick = 0;
        g.app_id = 3;
        g.gallery_sel = 6;
    }
}

void gui_handle_mouse(int dx, int dy, int buttons)
{
    if (!g_have_fb) return;
    gui_activity();
    if (g.ss_active) {
        g.ss_active = 0;
        return;
    }

    g.prev_buttons = g.buttons;
    g.buttons = buttons;

    g.mx += dx;
    g.my += dy;
    if (g.mx < 0) g.mx = 0;
    if (g.my < 0) g.my = 0;
    if (g.mx > (int)g_fb.w - 1) g.mx = (int)g_fb.w - 1;
    if (g.my > (int)g_fb.h - 1) g.my = (int)g_fb.h - 1;

    int l_down = (g.buttons & 0x1) != 0;
    int l_prev = (g.prev_buttons & 0x1) != 0;
    int l_pressed = l_down && !l_prev;
    int l_released = !l_down && l_prev;

    gui_apply_exexgui_layout();
    if (l_pressed && exexgui_is_enabled()) {
        exexgui_layout_t xl;
        if (exexgui_layout_for(g_fb.w, g_fb.h, &xl) == 0) {
            if (in_rect(g.mx, g.my, (int)xl.term.x, (int)xl.term.y, (int)xl.term.w, (int)xl.term.h)) {
                exexgui_set_focus("term");
                g.app_id = 7;
                g.tb_focused = 1;
                g.lafaelo_focus = 0;
                gui_lsh_sync_output();
            } else if (in_rect(g.mx, g.my, (int)xl.info.x, (int)xl.info.y, (int)xl.info.w, (int)xl.info.h)) {
                exexgui_set_focus("info");
            } else if (in_rect(g.mx, g.my, (int)xl.gui.x, (int)xl.gui.y, (int)xl.gui.w, (int)xl.gui.h)) {
                exexgui_set_focus("gui");
            }
        }
    }

    // Settings button (top-right of title bar)
    int set_btn_x = g.win_x + g.win_w - 52;
    int set_btn_w = 48;
    int title_h = 20;
    if (l_pressed && in_rect(g.mx, g.my, set_btn_x, g.win_y, set_btn_w, title_h)) {
        g.settings_open = 1 - g.settings_open;
        g.slider_drag = 0;
    }
    /* Slider hit-test on first click */
    if (g.settings_open && l_pressed) {
        int panel_x = g.win_x + g.win_w - 200;
        int panel_y = g.win_y + title_h + 4;
        int track_x = panel_x + 100;
        int track_w = 90;
        int row_h = 32;
        int th = 20;
        if (in_rect(g.mx, g.my, track_x, panel_y + 4, track_w, th)) g.slider_drag = 1;
        else if (in_rect(g.mx, g.my, track_x, panel_y + 4 + row_h, track_w, th)) g.slider_drag = 2;
        else if (in_rect(g.mx, g.my, track_x, panel_y + 4 + row_h * 2, track_w, th)) g.slider_drag = 3;
    }
    /* Slider drag: while dragging, update values from mouse */
    if (g.settings_open && g.slider_drag) {
        int panel_x = g.win_x + g.win_w - 200;
        int track_x = panel_x + 100;
        int track_w = 90;
        if (g.slider_drag == 1) {
            int v = (g.mx - track_x) * 100 / track_w;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            g.brightness = 50 + v;
        } else if (g.slider_drag == 2) {
            int v = (g.mx - track_x) * 100 / track_w;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            g.volume = v;
        } else if (g.slider_drag == 3) {
            int v = (g.mx - track_x) * 100 / track_w;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            g.quality = (v < 34) ? 0 : (v < 67) ? 1 : 2;
        }
    }
    if (l_released) g.slider_drag = 0;

    // Title bar drag (top 20px of window, exclude settings button)
    if (!exexgui_is_enabled() &&
        l_pressed && in_rect(g.mx, g.my, g.win_x, g.win_y, g.win_w - set_btn_w - 4, title_h)) {
        g.dragging = 1;
        g.drag_off_x = g.mx - g.win_x;
        g.drag_off_y = g.my - g.win_y;
    }
    if (!l_down) {
        g.dragging = 0;
    }
    if (g.dragging) {
        g.win_x = g.mx - g.drag_off_x;
        g.win_y = g.my - g.drag_off_y;
        if (g.win_x < 0) g.win_x = 0;
        if (g.win_y < 0) g.win_y = 0;
        if (g.win_x > (int)g_fb.w - g.win_w) g.win_x = (int)g_fb.w - g.win_w;
        if (g.win_y > (int)g_fb.h - g.win_h) g.win_y = (int)g_fb.h - g.win_h;
    }

    /* Tab bar: Lafillo | Calc | Notes | Gallery | Zip | User | LSS | LSH | 놀이터 | Lafaelo */
    {
        int tab_y = g.win_y + 20;
        int tab_h = 24;
        int tab_w = g.win_w / 10;
        if (l_pressed) {
            if (g.my >= tab_y && g.my < tab_y + tab_h && g.mx >= g.win_x && g.mx < g.win_x + g.win_w) {
                int idx = (g.mx - g.win_x) / tab_w;
                if (idx < 10) {
                    g.app_id = idx;
                    if (idx == 2) {
                        FsWritableFile* w = fs_open_writable("notes.txt");
                        if (w && w->size < sizeof(g.resp)) {
                            for (uint32_t i = 0; i < w->size; i++) g.resp[i] = (char)w->data[i];
                            g.resp[w->size] = '\0';
                        }
                    } else if (idx == 3) {
                        const char* glist = "0: hello.txt\n1: readme.txt\n2: sample.bmp\n3: notes.txt\n4: lfs_info.txt\n5: lafillo_saved.txt\n6: demo.larsh";
                        uint32_t gi = 0;
                        while (glist[gi] && gi + 1 < sizeof(g.resp)) { g.resp[gi] = glist[gi]; gi++; }
                        g.resp[gi] = '\0';
                    } else if (idx == 4) {
                        gui_tb_set("hello.txt");
                        gui_lar_show_bundle();
                    } else if (idx == 5) {
                        const char* z = "Click Run to execute user-mode program.";
                        uint32_t i = 0;
                        while (z[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = z[i]; i++; }
                        g.resp[i] = '\0';
                    } else if (idx == 6) {
                        const char* z = "LSS - Run Shrine programs. Click Run for hello.shrine.";
                        uint32_t i = 0;
                        while (z[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = z[i]; i++; }
                        g.resp[i] = '\0';
                    } else if (idx == 7) {
                        const char* out = lsh_get_output();
                        uint32_t i = 0;
                        while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
                        g.resp[i] = '\0';
                        g.tb_len = 0;
                        g.tb_cur = 0;
                        g.tb[0] = '\0';
                    } else if (idx == 8) {
                        const char* z = "print \"hello\" or repeat 3x poop end";
                        uint32_t i = 0;
                        while (z[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = z[i]; i++; }
                        g.resp[i] = '\0';
                        g.tb_len = 0;
                        g.tb_cur = 0;
                        g.tb[0] = '\0';
                    } else if (idx == 9) {
                        const char* z = "Lafaelo code editor. Path: file to open/save.";
                        uint32_t i = 0;
                        while (z[i] && i + 1 < sizeof(g.lafaelo_buf)) { g.lafaelo_buf[i] = z[i]; i++; }
                        g.lafaelo_buf[i] = '\0';
                        g.lafaelo_len = i;
                        g.lafaelo_cur = i;
                        g.lafaelo_focus = 0;
                        g.lafaelo_show_run = 0;
                        g.tb_len = 0;
                        g.tb_cur = 0;
                        g.tb[0] = '\0';
                    }
                    g.gallery_sel = -1;
                }
            }
        }
    }

    // Button inside window
    int content_y_m = g.win_y + 44;
    int btn_x = g.win_x + 16;
    int btn_y = content_y_m + 36;
    int btn_w = 120;
    int btn_h = 28;
    int lafillo_w[] = { 48, 64, 52, 56, 50 };
    int lafaelo_btn_w = 56;
    if (l_pressed) {
        if (g.app_id == 0) {
            int x = btn_x;
            for (int d = 0; d < 5; d++) {
                if (in_rect(g.mx, g.my, x, btn_y, lafillo_w[d], btn_h)) g.btn_pressed = 1;
                x += lafillo_w[d] + 4;
            }
        } else if (g.app_id == 9) {
            if (in_rect(g.mx, g.my, btn_x, btn_y, lafaelo_btn_w, btn_h)) g.btn_pressed = 1;
            else if (in_rect(g.mx, g.my, btn_x + lafaelo_btn_w + 4, btn_y, lafaelo_btn_w, btn_h)) g.btn_pressed = 1;
            else if (in_rect(g.mx, g.my, btn_x + (lafaelo_btn_w + 4) * 2, btn_y, lafaelo_btn_w, btn_h)) g.btn_pressed = 1;
        } else if (in_rect(g.mx, g.my, btn_x, btn_y, btn_w, btn_h)) {
            g.btn_pressed = 1;
        } else if (g.app_id == 5 && in_rect(g.mx, g.my, btn_x + btn_w + 8, btn_y, 70, btn_h)) {
            g.btn_pressed = 1;
        }
    }
    if (l_released) {
        if (g.btn_pressed && g.app_id == 0) {
            int x = btn_x;
            int hit = -1;
            for (int d = 0; d < 5; d++) {
                if (in_rect(g.mx, g.my, x, btn_y, lafillo_w[d], btn_h)) {
                    hit = d;
                    break;
                }
                x += lafillo_w[d] + 4;
            }
            if (hit == 0) {
                g.submit_pending = 1;
            } else if (hit == 1) {
                g.submit_pending = 1;
            } else if (hit == 2) {
                g.http_post_mode = 1 - g.http_post_mode;
            } else if (hit == 3) {
                FsWritableFile* w = fs_open_writable("lafillo_saved.txt");
                if (w) {
                    uint32_t n = 0;
                    while (g.resp[n] && n < sizeof(g.resp) - 1) n++;
                    fs_write(w, 0, (const uint8_t*)g.resp, n);
                }
            } else if (hit == 4) {
                g.lafillo_src_mode = 1 - g.lafillo_src_mode;
                g.resp_scroll = 0;
                if (g.lafillo_src_mode) {
                    uint32_t i = 0;
                    while (g.lafillo_raw[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = g.lafillo_raw[i]; i++; }
                    g.resp[i] = '\0';
                } else {
                    uint32_t i = 0;
                    while (g.lafillo_extracted[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = g.lafillo_extracted[i]; i++; }
                    g.resp[i] = '\0';
                }
            }
        } else if (g.btn_pressed && g.app_id == 9) {
            int lfb = lafaelo_btn_w;
            if (in_rect(g.mx, g.my, btn_x, btn_y, lfb, btn_h)) {
                /* Open: load file from g.tb into lafaelo_buf */
                const FsFile* f = fs_open(g.tb);
                FsWritableFile* w = fs_open_writable(g.tb);
                const uint8_t* d = f ? f->data : (w ? w->data : NULL);
                uint32_t sz = f ? f->size : (w ? w->size : 0);
                if (d && sz > 0 && sz < sizeof(g.lafaelo_buf)) {
                    for (uint32_t i = 0; i < sz; i++) g.lafaelo_buf[i] = (char)d[i];
                    g.lafaelo_buf[sz] = '\0';
                    g.lafaelo_len = sz;
                    g.lafaelo_cur = sz;
                    g.lafaelo_show_run = 0;
                }
            } else if (in_rect(g.mx, g.my, btn_x + lfb + 4, btn_y, lfb, btn_h)) {
                /* Save: write lafaelo_buf to g.tb */
                FsWritableFile* w = fs_open_writable(g.tb);
                if (w) fs_write(w, 0, (const uint8_t*)g.lafaelo_buf, g.lafaelo_len);
            } else if (in_rect(g.mx, g.my, btn_x + (lfb + 4) * 2, btn_y, lfb, btn_h)) {
                /* Run: execute lafaelo_buf via LSH */
                lsh_exec(g.lafaelo_buf);
                const char* out = lsh_get_output();
                uint32_t i = 0;
                while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
                g.resp[i] = '\0';
                g.lafaelo_show_run = 1;
            }
        } else if (g.btn_pressed && g.app_id == 5 && in_rect(g.mx, g.my, btn_x + btn_w + 8, btn_y, 70, btn_h)) {
            g.user_sandbox = 1 - g.user_sandbox;
        } else if (g.btn_pressed && in_rect(g.mx, g.my, btn_x, btn_y, btn_w, btn_h)) {
            if (g.app_id == 1) {
                /* Calc: evaluate */
                uint32_t i = 0, a = 0, b = 0;
                char op = '+';
                while (i < g.calc_len && g.calc_display[i] >= '0' && g.calc_display[i] <= '9')
                    a = a * 10u + (uint32_t)(g.calc_display[i++] - '0');
                if (i > 0 && i < g.calc_len && (g.calc_display[i] == '+' || g.calc_display[i] == '-' || g.calc_display[i] == '*' || g.calc_display[i] == '/')) {
                    op = g.calc_display[i++];
                    while (i < g.calc_len && g.calc_display[i] >= '0' && g.calc_display[i] <= '9')
                        b = b * 10u + (uint32_t)(g.calc_display[i++] - '0');
                    int64_t r = (int64_t)a;
                    if (op == '+') r += b; else if (op == '-') r -= b; else if (op == '*') r *= b; else if (op == '/' && b) r /= b;
                    uint32_t n = 0;
                    if (r < 0) { g.resp[n++] = '-'; r = -r; }
                    if (r == 0) g.resp[n++] = '0';
                    else { char tmp[16]; int t = 0; while (r) { tmp[t++] = (char)('0' + (r % 10)); r /= 10; } while (t--) g.resp[n++] = tmp[t]; }
                    g.resp[n] = '\0';
                }
            } else if (g.app_id == 2) {
                /* Notes: save to fs */
                FsWritableFile* w = fs_open_writable("notes.txt");
                if (w) {
                    uint32_t n = 0;
                    while (g.resp[n] && n < sizeof(g.resp) - 1) n++;
                    fs_write(w, 0, (const uint8_t*)g.resp, n);
                }
            } else if (g.app_id == 3) {
                g.gallery_sel = (g.gallery_sel + 1) % 7;
                const char* names[] = { "hello.txt", "readme.txt", "sample.bmp", "notes.txt", "lfs_info.txt", "lafillo_saved.txt", "demo.larsh" };
                if (g.gallery_sel == 6) {
                    const FsFile* f = fs_open("demo.larsh");
                    FsWritableFile* w = fs_open_writable("demo.larsh");
                    const uint8_t* d = f ? f->data : (w ? w->data : NULL);
                    uint32_t sz = f ? f->size : (w ? w->size : 0);
                    if (d && sz > 0 && larsh_parse((const char*)d, sz, &g.larsh_scene) == 0) {
                        g.larsh_loaded = 1;
                        g.larsh_playing = 1;
                        g.larsh_tick = 0;
                    }
                } else if (g.gallery_sel != 2 && g.gallery_sel < 6) {
                    const FsFile* f = fs_open(names[g.gallery_sel]);
                    FsWritableFile* w = fs_open_writable(names[g.gallery_sel]);
                    const uint8_t* d = f ? f->data : (w ? w->data : NULL);
                    uint32_t sz = f ? f->size : (w ? w->size : 0);
                    if (d && sz < sizeof(g.resp)) {
                        for (uint32_t i = 0; i < sz; i++) g.resp[i] = (char)d[i];
                        g.resp[sz] = '\0';
                    }
                }
            } else if (g.app_id == 4) {
                gui_lar_extract_selected();
            } else if (g.app_id == 5) {
                if (in_rect(g.mx, g.my, btn_x + btn_w + 8, btn_y, 70, btn_h)) {
                    g.user_sandbox = 1 - g.user_sandbox;
                } else {
                    syscall_clear_output();
                    if (g.user_sandbox) syscall_set_sandbox(1);
                    usermode_run();
                    if (g.user_sandbox) syscall_set_sandbox(0);
                    const char* out = syscall_get_output();
                    uint32_t i = 0;
                    while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
                    g.resp[i] = '\0';
                }
            } else if (g.app_id == 6) {
                syscall_clear_output();
                int r = lss_run("hello.shrine");
                const char* out = syscall_get_output();
                uint32_t i = 0;
                if (r != 0) {
                    const char* err = "LSS run failed.";
                    while (err[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = err[i]; i++; }
                } else {
                    while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
                }
                g.resp[i] = '\0';
            } else if (g.app_id == 7) {
                lsh_exec(g.tb);
                const char* out = lsh_get_output();
                uint32_t i = 0;
                while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
                g.resp[i] = '\0';
                g.tb_len = 0;
                g.tb_cur = 0;
                g.tb[0] = '\0';
            } else if (g.app_id == 8) {
                g.resp[0] = '\0';
                kr_basic_run(g.tb, g.resp, sizeof(g.resp));
            }
        }
        g.btn_pressed = 0;
    }

    // Textbox focus (path input)
    int content_y = g.win_y + 44;
    int tb_x = g.win_x + 16;
    int tb_y = content_y + 118;
    int tb_w = 260;
    int tb_h = 24;
    int view_x_focus = g.win_x + 16;
    int view_y_focus = content_y_m + 168;
    int view_w_focus = g.win_w - 32;
    int view_h_focus = g.win_h - 190;
    if (l_pressed) {
        if (g.app_id == 9) {
            if (in_rect(g.mx, g.my, tb_x, tb_y, tb_w, tb_h)) {
                g.tb_focused = 1;
                g.lafaelo_focus = 0;
                g.lafaelo_show_run = 0;
            } else if (in_rect(g.mx, g.my, view_x_focus, view_y_focus, view_w_focus, view_h_focus)) {
                g.tb_focused = 0;
                g.lafaelo_focus = 1;
                g.lafaelo_show_run = 0;
            } else {
                g.tb_focused = 0;
                g.lafaelo_focus = 0;
            }
        } else {
            g.tb_focused = in_rect(g.mx, g.my, tb_x, tb_y, tb_w, tb_h) ? 1 : 0;
            g.lafaelo_focus = 0;
        }
    }

    // Scrollbar drag in response view (only when textbox not focused)
    if (!g.tb_focused) {
        int view_x = g.win_x + 16;
        int view_y = content_y_m + 168;
        int view_w = g.win_w - 32;
        int view_h = g.win_h - 190;
        int sb_w = 10;
        int sb_x = view_x + view_w - sb_w;
        int sb_y = view_y;
        int sb_h = view_h;
        if (l_pressed && in_rect(g.mx, g.my, sb_x, sb_y, sb_w, sb_h)) {
            g.scroll_drag = 1;
            g.scroll_drag_off_y = g.my - sb_y;
        }
        if (l_released) {
            g.scroll_drag = 0;
        }
        if (g.scroll_drag && g.resp_total_lines > 0) {
            int max_scroll = g.resp_total_lines > 1 ? (g.resp_total_lines - 1) : 0;
            int y = g.my - sb_y;
            if (y < 0) y = 0;
            if (y > sb_h - 1) y = sb_h - 1;
            // Map y->scroll line
            g.resp_scroll = (y * max_scroll) / (sb_h - 1);
        }
    }
}

void gui_handle_key(char ch)
{
    gui_activity();
    if (g.ss_active) {
        g.ss_active = 0;
        return;
    }
    if (!g_have_fb) return;
    if (!g.tb_focused) return;

    char* edit_buf;
    uint32_t* edit_len;
    uint32_t* edit_cur;
    uint32_t edit_cap;
    if (g.app_id == 1) {
        edit_buf = g.calc_display;
        edit_len = &g.calc_len;
        edit_cur = &g.calc_cur;
        edit_cap = sizeof(g.calc_display);
    } else if (g.app_id == 9 && g.lafaelo_focus) {
        edit_buf = g.lafaelo_buf;
        edit_len = &g.lafaelo_len;
        edit_cur = &g.lafaelo_cur;
        edit_cap = sizeof(g.lafaelo_buf);
    } else {
        edit_buf = g.tb;
        edit_len = &g.tb_len;
        edit_cur = &g.tb_cur;
        edit_cap = sizeof(g.tb);
    }

    if (ch == '\b') {
        if (*edit_cur > 0 && *edit_len) {
            for (uint32_t i = *edit_cur - 1; i + 1 < *edit_len; i++) edit_buf[i] = edit_buf[i + 1];
            (*edit_len)--;
            (*edit_cur)--;
            edit_buf[*edit_len] = '\0';
        }
        return;
    }
    if (ch == '\n') {
        if (g.app_id == 9 && g.lafaelo_focus) {
            if (g.lafaelo_len + 1 < sizeof(g.lafaelo_buf)) {
                for (uint32_t i = g.lafaelo_len; i > g.lafaelo_cur; i--) g.lafaelo_buf[i] = g.lafaelo_buf[i - 1];
                g.lafaelo_buf[g.lafaelo_cur++] = '\n';
                g.lafaelo_len++;
                g.lafaelo_buf[g.lafaelo_len] = '\0';
            }
            return;
        }
        /* JSDoc-style expansion for slash-star-star + Enter -> doc block template. */
        if (g.app_id != 1 && g.tb_len >= 3 &&
            g.tb[g.tb_len - 3] == '/' && g.tb[g.tb_len - 2] == '*' && g.tb[g.tb_len - 1] == '*') {
            static const char doc_tpl[] = "\n * \n * @param \n * @return \n */";
            const uint32_t tpl_len = sizeof(doc_tpl) - 1;
            uint32_t cap = sizeof(g.tb);
            if (g.tb_len + tpl_len < cap) {
                for (uint32_t i = 0; i < tpl_len; i++)
                    g.tb[g.tb_len + i] = doc_tpl[i];
                g.tb_len += tpl_len;
                g.tb[g.tb_len] = '\0';
                g.tb_cur = g.tb_len;
            }
            return;
        }
        if (g.app_id == 0) g.submit_pending = 1;
        else if (g.app_id == 4) {
            gui_lar_extract_selected();
        }
        else if (g.app_id == 8) {
            g.resp[0] = '\0';
            kr_basic_run(g.tb, g.resp, sizeof(g.resp));
        } else if (g.app_id == 7) {
            lsh_exec(g.tb);
            const char* out = lsh_get_output();
            uint32_t i = 0;
            while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
            g.resp[i] = '\0';
            g.tb_len = 0;
            g.tb_cur = 0;
            g.tb[0] = '\0';
        } else if (g.app_id == 2) {
            /* Notes: append line */
            uint32_t rlen = 0;
            while (g.resp[rlen] && rlen < sizeof(g.resp) - 1) rlen++;
            if (rlen + g.tb_len + 2 < sizeof(g.resp)) {
                for (uint32_t i = 0; i < g.tb_len; i++) g.resp[rlen + i] = g.tb[i];
                g.resp[rlen + g.tb_len] = '\n';
                g.resp[rlen + g.tb_len + 1] = '\0';
                g.tb_len = 0;
                g.tb_cur = 0;
                g.tb[0] = '\0';
            }
        }
        return;
    }
    if (ch == '\t') return;

    if (ch >= ' ' && ch <= '~') {
        if (*edit_len + 1 < edit_cap) {
            for (uint32_t i = *edit_len; i > *edit_cur; i--) edit_buf[i] = edit_buf[i - 1];
            edit_buf[(*edit_cur)++] = ch;
            (*edit_len)++;
            edit_buf[*edit_len] = '\0';
        }
    }
}

void gui_handle_key_nav(int kind)
{
    if (!g_have_fb) return;
    gui_activity();
    if (g.ss_active) {
        g.ss_active = 0;
        return;
    }
    if (kind == PS2K_F10) {
        gui_activate_ring0_shortcut();
        return;
    }
    uint32_t* cur = (g.app_id == 1) ? &g.calc_cur : (g.app_id == 9 && g.lafaelo_focus) ? &g.lafaelo_cur : &g.tb_cur;
    uint32_t len = (g.app_id == 1) ? g.calc_len : (g.app_id == 9 && g.lafaelo_focus) ? g.lafaelo_len : g.tb_len;
    if (!g.tb_focused && !(g.app_id == 9 && g.lafaelo_focus)) {
        // kind values come from ps2_key_kind_t in include/ps2.h
        if (kind == 4) { // UP
            if (g.resp_scroll > 0) g.resp_scroll--;
            return;
        }
        if (kind == 5) { // DOWN
            g.resp_scroll++;
            return;
        }
        if (kind == 8) { // PGUP
            g.resp_scroll -= 6;
            if (g.resp_scroll < 0) g.resp_scroll = 0;
            return;
        }
        if (kind == 9) { // PGDN
            g.resp_scroll += 6;
            return;
        }
        if (kind == 6) { // HOME
            g.resp_scroll = 0;
            return;
        }
        // END: just jump a lot
        if (kind == 7) { // END
            g.resp_scroll += 1000;
            return;
        }
        return;
    }

    switch (kind) {
        case 2: if (*cur) (*cur)--; break;
        case 3: if (*cur < len) (*cur)++; break;
        case 6: *cur = 0; break;
        case 7: *cur = len; break;
        case 10:
            if (g.app_id == 1) {
                if (g.calc_cur < g.calc_len) {
                    for (uint32_t i = g.calc_cur; i + 1 < g.calc_len; i++) g.calc_display[i] = g.calc_display[i + 1];
                    g.calc_len--;
                    g.calc_display[g.calc_len] = '\0';
                }
            } else if (g.app_id == 9 && g.lafaelo_focus) {
                if (g.lafaelo_cur < g.lafaelo_len) {
                    for (uint32_t i = g.lafaelo_cur; i + 1 < g.lafaelo_len; i++) g.lafaelo_buf[i] = g.lafaelo_buf[i + 1];
                    g.lafaelo_len--;
                    g.lafaelo_buf[g.lafaelo_len] = '\0';
                }
            } else {
                if (g.tb_cur < g.tb_len) {
                    for (uint32_t i = g.tb_cur; i + 1 < g.tb_len; i++) g.tb[i] = g.tb[i + 1];
                    g.tb_len--;
                    g.tb[g.tb_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
}

void gui_tick(void)
{
    if (!g_have_fb) return;
    if (lsh_poll_background() && g.app_id == 7) {
        const char* out = lsh_get_output();
        uint32_t i = 0;
        while (out[i] && i + 1 < sizeof(g.resp)) { g.resp[i] = out[i]; i++; }
        g.resp[i] = '\0';
    }
    g.caret_tick++;
    if ((g.caret_tick & 0x1FFFFu) == 0) {
        g.caret_on = !g.caret_on;
    }
    if (!g.ss_active) {
        g.ss_idle_ticks++;
        if (g.ss_idle_ticks >= SS_IDLE_THRESHOLD) {
            g.ss_active = 1;
            g.ss_frame = 0;
            g.ss_anim_tick = 0;
        }
    }
}

int gui_screensaver_active(void)
{
    return g.ss_active ? 1 : 0;
}

static int gui_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static uint32_t gui_copy_trim(char* out, uint32_t cap, const char* src, uint32_t start, uint32_t end)
{
    if (!out || cap == 0 || !src) return 0;
    while (start < end && gui_is_space(src[start])) start++;
    while (end > start && gui_is_space(src[end - 1u])) end--;
    uint32_t n = 0;
    while (start < end && n + 1u < cap) {
        out[n++] = src[start++];
    }
    out[n] = '\0';
    return n;
}

int gui_take_submit(gui_http_request_t* out)
{
    if (!g_have_fb) return 0;
    if (!g.submit_pending) return 0;
    g.submit_pending = 0;
    if (!out) return 1;

    out->method[0] = g.http_post_mode ? 'P' : 'G';
    out->method[1] = g.http_post_mode ? 'O' : 'E';
    out->method[2] = g.http_post_mode ? 'S' : 'T';
    out->method[3] = g.http_post_mode ? 'T' : '\0';
    out->method[4] = '\0';
    out->body[0] = '\0';
    out->body_len = 0;

    uint32_t split = g.tb_len;
    int have_split = 0;
    if (g.http_post_mode) {
        for (uint32_t i = 0; i < g.tb_len; i++) {
            if (g.tb[i] == '|') {
                split = i;
                have_split = 1;
                break;
            }
        }
        if (!have_split) {
            for (uint32_t i = 0; i < g.tb_len; i++) {
                if (gui_is_space(g.tb[i])) {
                    split = i;
                    have_split = 1;
                    break;
                }
            }
        }
    }

    gui_copy_trim(out->url, GUI_HTTP_URL_MAX, g.tb, 0, split);
    if (g.http_post_mode && have_split && split + 1u < g.tb_len) {
        out->body_len = gui_copy_trim(out->body, GUI_HTTP_BODY_MAX, g.tb, split + 1u, g.tb_len);
    }
    return 1;
}

void gui_http_set_post_mode(int on)
{
    g.http_post_mode = on ? 1 : 0;
}

int gui_http_post_mode(void)
{
    return g.http_post_mode ? 1 : 0;
}

int gui_post_check(gui_post_info_t* out)
{
    if (!g_have_fb || !g_fb.fb) return -1;
    gui_apply_exexgui_layout();
    if (out) {
        out->width = g_fb.w;
        out->height = g_fb.h;
        out->changed_samples = 0;
        out->window_inside = (g.win_x >= 0 && g.win_y >= 0 &&
                              g.win_x + g.win_w <= (int)g_fb.w &&
                              g.win_y + g.win_h <= (int)g_fb.h);
        out->response_view_ok = (g.win_w >= 320 && g.win_h >= 240 &&
                                 g.win_h - 190 >= 40 && g.win_w - 32 >= 160);
        out->chrome_ok = (guioverlay_selftest() == 0 &&
                          g.win_w / 10 >= 16 &&
                          g.win_w - 32 >= 160 &&
                          g.win_h >= 240);
        uint32_t step_x = g_fb.w >= 64 ? (uint32_t)g_fb.w / 32u : 1u;
        uint32_t step_y = g_fb.h >= 64 ? (uint32_t)g_fb.h / 24u : 1u;
        if (step_x == 0) step_x = 1;
        if (step_y == 0) step_y = 1;
        for (uint32_t y = 0; y < g_fb.h; y += step_y) {
            uint32_t row = y * (uint32_t)(g_fb.pitch_bytes / 4u);
            for (uint32_t x = 0; x < g_fb.w; x += step_x) {
                if (g_fb.fb[row + x] != g_bg) out->changed_samples++;
            }
        }
    }
    return 0;
}

void gui_set_response(const char* text)
{
    if (!g_have_fb) return;
    if (!text) text = "";
    // Copy and truncate to resp buffer.
    uint32_t i = 0;
    for (; text[i] && i + 1 < sizeof(g.resp); i++) g.resp[i] = text[i];
    g.resp[i] = '\0';
    g.resp_scroll = 0;
}

void gui_set_loading(int on)
{
    if (!g_have_fb) return;
    g.loading = on ? 1 : 0;
}

void gui_lafillo_set_content(const char* extracted, const char* raw)
{
    if (!g_have_fb) return;
    uint32_t i;
    for (i = 0; extracted[i] && i + 1 < sizeof(g.lafillo_extracted); i++) g.lafillo_extracted[i] = extracted[i];
    g.lafillo_extracted[i] = '\0';
    for (i = 0; raw[i] && i + 1 < sizeof(g.lafillo_raw); i++) g.lafillo_raw[i] = raw[i];
    g.lafillo_raw[i] = '\0';
    g.lafillo_src_mode = 0;
    for (i = 0; g.lafillo_extracted[i] && i + 1 < sizeof(g.resp); i++) g.resp[i] = g.lafillo_extracted[i];
    g.resp[i] = '\0';
    g.resp_scroll = 0;
}

static void fb_fill_circle(const fb_t* f, int cx, int cy, int r, uint32_t color)
{
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || y < 0 || x >= (int)f->w || y >= (int)f->h) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r * r)
                fb_putpixel(f, (uint16_t)x, (uint16_t)y, color);
        }
    }
}

static void render_screensaver(void)
{
    fb_t* tgt = g_have_bb ? &g_bb : &g_fb;
    uint16_t sw = g_fb.w, sh = g_fb.h;

    if (g.ss_file) {
        /* SSAV from file */
        ssav_decode_frame(&g.ss_ssav, g.ss_frame, g.ss_frame_buf);
        uint16_t fw = g.ss_ssav.w, fh = g.ss_ssav.h;
        int scale = 1;
        if ((int)fw * scale < (int)sw && (int)fh * scale < (int)sh) {
            scale = (int)sw / (int)fw;
            if ((int)sh / (int)fh < scale) scale = (int)sh / (int)fh;
        }
        fb_clear(tgt, 0xFF0A0A12);
        int offx = ((int)sw - (int)fw * scale) / 2;
        int offy = ((int)sh - (int)fh * scale) / 2;
        for (uint16_t py = 0; py < fh; py++) {
            for (uint16_t px = 0; px < fw; px++) {
                uint32_t c = g.ss_frame_buf[py * (uint32_t)fw + px];
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_putpixel(tgt, (uint16_t)(offx + (int)px * scale + sx), (uint16_t)(offy + (int)py * scale + sy), c);
            }
        }
        g.ss_anim_tick++;
        uint32_t ticks_per_frame = g.ss_ssav.delay_cs ? (uint32_t)g.ss_ssav.delay_cs + 1u : 4u;
        if (g.ss_anim_tick >= ticks_per_frame && g.ss_ssav.frame_count > 1) {
            g.ss_anim_tick = 0;
            g.ss_frame = (uint16_t)((g.ss_frame + 1) % g.ss_ssav.frame_count);
        }
    } else {
        /* Built-in: 3D cube when resolution allows, else bouncing ball */
        g.ss_angle += 0.02f;
        if (g.ss_angle > 6.283185f) g.ss_angle -= 6.283185f;
        if (sw >= 164 && sh >= 124) {
            /* 3D rotating cube */
            fb_clear(tgt, 0xFF0A0A18);
            lib3d_demo_render((uint16_t)((sw - 160) / 2), (uint16_t)((sh - 120) / 2), g.ss_angle);
        } else {
            /* Bouncing ball */
            fb_clear(tgt, 0xFF0A0A18);
            g.ss_bx += g.ss_bdx;
            g.ss_by += g.ss_bdy;
            if (g.ss_bx < 20 || g.ss_bx > (int)sw - 20) { g.ss_bdx = -g.ss_bdx; g.ss_bx += g.ss_bdx; }
            if (g.ss_by < 20 || g.ss_by > (int)sh - 20) { g.ss_bdy = -g.ss_bdy; g.ss_by += g.ss_bdy; }
            fb_fill_circle(tgt, g.ss_bx, g.ss_by, 24, 0xFF4080FF);
            fb_fill_circle(tgt, g.ss_bx, g.ss_by, 18, 0xFF60A0FF);
        }
    }
}

void gui_render(void)
{
    if (!g_have_fb) return;

    fb_t* tgt = g_have_bb ? &g_bb : &g_fb;
    g_syscall_target_override = tgt;
    if (g.ss_active) {
        render_screensaver();
        screenram_flush_to_target(tgt);
        if (g_have_bb) fb_blit(&g_fb, &g_bb);
        g_syscall_target_override = 0;
        return;
    }

    // Full redraw for simplicity & correctness.
    fb_clear(tgt, g_bg);
    exgui_draw_desktop();
    exexgui_draw_desktop();
    gui_apply_exexgui_layout();

    // Window frame
    uint32_t win_bg = 0xFF202840;
    uint32_t title_bg = 0xFF304060;
    uint32_t border = 0xFF000000;
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)g.win_y, (uint16_t)g.win_w, (uint16_t)g.win_h, win_bg);
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)g.win_y, (uint16_t)g.win_w, 20, title_bg);
    int set_btn_x = g.win_x + g.win_w - 52;
    uint32_t set_btn_bg = (g.settings_open || (g.mx >= set_btn_x && g.mx < set_btn_x + 48 && g.my >= g.win_y && g.my < g.win_y + 20)) ? 0xFF506090 : 0xFF405070;
    fb_fill_rect(tgt, (uint16_t)set_btn_x, (uint16_t)g.win_y, 48, 20, set_btn_bg);
    fb_draw_text(tgt, (uint16_t)(set_btn_x + 8), (uint16_t)(g.win_y + 6), "Set", 0xFFFFFFFF, set_btn_bg);
    // crude border
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)g.win_y, (uint16_t)g.win_w, 1, border);
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)(g.win_y + g.win_h - 1), (uint16_t)g.win_w, 1, border);
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)g.win_y, 1, (uint16_t)g.win_h, border);
    fb_fill_rect(tgt, (uint16_t)(g.win_x + g.win_w - 1), (uint16_t)g.win_y, 1, (uint16_t)g.win_h, border);

    /* Tab bar */
    static const char* tab_names[] = { "Doc", "Calc", "Note", "Pix", "Pak", "User", "LSS", "LSH", "Play", "Edit" };
    int tab_y = g.win_y + 20;
    int tab_h = 24;
    int tab_w = g.win_w / 10;
    for (int t = 0; t < 10; t++) {
        int tx = g.win_x + t * tab_w;
        uint32_t tab_bg = (g.app_id == t) ? 0xFF405070 : 0xFF282838;
        fb_fill_rect(tgt, (uint16_t)tx, (uint16_t)tab_y, (uint16_t)tab_w, (uint16_t)tab_h, tab_bg);
        fb_draw_text(tgt, (uint16_t)(tx + 4), (uint16_t)(tab_y + 8), tab_names[t], 0xFFFFFFFF, tab_bg);
    }

    int content_y = g.win_y + 20 + tab_h;

    fb_draw_text(tgt, (uint16_t)(g.win_x + 8), (uint16_t)(content_y + 4), "lardos", 0xFFFFFFFF, win_bg);

    // Button
    int btn_x = g.win_x + 16;
    int btn_y = content_y + 36;
    int btn_w = 120;
    int btn_h = 28;
    if (g.app_id == 0) {
        static const char* lafillo_labels[] = { "Go", "Refresh", "", "Save", "Src" };
        int dx[] = { 0, 52, 120, 176, 236 };
        int dww[] = { 48, 64, 52, 56, 50 };
        for (int d = 0; d < 5; d++) {
            const char* label = (d == 2) ? (g.http_post_mode ? "POST" : "GET") : lafillo_labels[d];
            uint32_t dbg = g.btn_pressed ? 0xFF80A0FF : 0xFF5070D0;
            fb_fill_rect(tgt, (uint16_t)(btn_x + dx[d]), (uint16_t)btn_y, (uint16_t)dww[d], (uint16_t)btn_h, dbg);
            fb_draw_text(tgt, (uint16_t)(btn_x + dx[d] + 4), (uint16_t)(btn_y + 10), label, 0xFFFFFFFF, dbg);
        }
    } else if (g.app_id == 9) {
        static const char* lafaelo_labels[] = { "Open", "Save", "Run" };
        int lfb = 56;
        for (int d = 0; d < 3; d++) {
            uint32_t dbg = g.btn_pressed ? 0xFF80A0FF : 0xFF5070D0;
            fb_fill_rect(tgt, (uint16_t)(btn_x + d * (lfb + 4)), (uint16_t)btn_y, (uint16_t)lfb, (uint16_t)btn_h, dbg);
            fb_draw_text(tgt, (uint16_t)(btn_x + d * (lfb + 4) + 4), (uint16_t)(btn_y + 10), lafaelo_labels[d], 0xFFFFFFFF, dbg);
        }
    } else {
        const char* btn_label = (g.app_id == 1) ? "=" : (g.app_id == 2) ? "Save" : (g.app_id == 3) ? "View" : (g.app_id == 4) ? "Extract" : (g.app_id == 8) ? "Run" : "Run";
        uint32_t btn_bg = g.btn_pressed ? 0xFF80A0FF : 0xFF5070D0;
        fb_fill_rect(tgt, (uint16_t)btn_x, (uint16_t)btn_y, (uint16_t)btn_w, (uint16_t)btn_h, btn_bg);
        fb_draw_text(tgt, (uint16_t)(btn_x + 12), (uint16_t)(btn_y + 10), btn_label, 0xFFFFFFFF, btn_bg);
        if (g.app_id == 5) {
            uint32_t sb_bg = g.user_sandbox ? 0xFF60A060 : 0xFF405040;
            fb_fill_rect(tgt, (uint16_t)(btn_x + btn_w + 8), (uint16_t)btn_y, 70, (uint16_t)btn_h, sb_bg);
            fb_draw_text(tgt, (uint16_t)(btn_x + btn_w + 12), (uint16_t)(btn_y + 10), "Sandbox", 0xFFFFFFFF, sb_bg);
        }
    }

    int tb_x = g.win_x + 16;
    int tb_y = content_y + 118;
    int tb_w = 260;
    int tb_h = 24;
    uint32_t tb_bg = 0xFF101020;
    uint32_t tb_bd = g.tb_focused ? 0xFFFFFF00 : 0xFF000000;
    fb_fill_rect(tgt, (uint16_t)tb_x, (uint16_t)tb_y, (uint16_t)tb_w, (uint16_t)tb_h, tb_bg);
    fb_fill_rect(tgt, (uint16_t)tb_x, (uint16_t)tb_y, (uint16_t)tb_w, 1, tb_bd);
    fb_fill_rect(tgt, (uint16_t)tb_x, (uint16_t)(tb_y + tb_h - 1), (uint16_t)tb_w, 1, tb_bd);
    fb_fill_rect(tgt, (uint16_t)tb_x, (uint16_t)tb_y, 1, (uint16_t)tb_h, tb_bd);
    fb_fill_rect(tgt, (uint16_t)(tb_x + tb_w - 1), (uint16_t)tb_y, 1, (uint16_t)tb_h, tb_bd);
    const char* input_text = (g.app_id == 1) ? g.calc_display : g.tb;
    uint32_t input_cur = (g.app_id == 1) ? g.calc_cur : g.tb_cur;
    const char* input_label = "URL:";
    if (g.app_id == 0 && g.http_post_mode) input_label = "URL|Body:";
    if (g.app_id == 1) input_label = "Expr:";
    else if (g.app_id == 2) input_label = "Add line:";
    else if (g.app_id == 4) input_label = "File:";
    else if (g.app_id == 7) input_label = lsh_in_sum_mode() ? "SUM:" : "Cmd:";
    else if (g.app_id == 8) input_label = "Code:";
    else if (g.app_id == 9) input_label = "Path:";
    fb_draw_text_cells(tgt, (uint16_t)(tb_x + 6), (uint16_t)(tb_y + 8), input_text,
                       (uint16_t)((tb_w - 12) / 8), 0xFFFFFFFF, tb_bg);
    fb_draw_text(tgt, (uint16_t)(g.win_x + 16), (uint16_t)(tb_y - 12), input_label, 0xFFFFFFFF, win_bg);

    int view_x = g.win_x + 16;
    int view_y = g.win_y + 180;
    int view_w = g.win_w - 32;
    int view_h = g.win_h - 190;
    if ((g.tb_focused || (g.app_id == 9 && g.lafaelo_focus && !g.lafaelo_show_run)) && g.caret_on) {
        uint16_t cx, cy;
        if (g.app_id == 9 && g.lafaelo_focus) {
            int line = 0, col = 0;
            for (uint32_t i = 0; i < g.lafaelo_cur && g.lafaelo_buf[i]; i++) {
                if (g.lafaelo_buf[i] == '\n') { line++; col = 0; } else col++;
            }
            int row = line - g.resp_scroll;
            cx = (uint16_t)(view_x + col * 8);
            cy = (uint16_t)(view_y + row * 10);
            if (cx < (uint16_t)view_x) cx = (uint16_t)view_x;
            if (cx > (uint16_t)(view_x + view_w - 10)) cx = (uint16_t)(view_x + view_w - 10);
            if (cy < (uint16_t)view_y) cy = (uint16_t)view_y;
            if (cy > (uint16_t)(view_y + view_h - 12)) cy = (uint16_t)(view_y + view_h - 12);
            fb_fill_rect(tgt, cx, cy, 1, 10, 0xFFFFFFFF);
        } else {
            cx = (uint16_t)(tb_x + 6 + (int)input_cur * 8);
            cy = (uint16_t)(tb_y + 6);
            if (cx > (uint16_t)(tb_x + tb_w - 2)) cx = (uint16_t)(tb_x + tb_w - 2);
            fb_fill_rect(tgt, cx, cy, 1, 12, 0xFFFFFFFF);
        }
    }

    const char* view_label = "Response:";
    if (g.app_id == 1) view_label = "Result:";
    else if (g.app_id == 2) view_label = "Notes:";
    else if (g.app_id == 3) view_label = "Gallery:";
    else if (g.app_id == 4) view_label = "LAR:";
    else if (g.app_id == 5) view_label = "Output:";
    else if (g.app_id == 6) view_label = "LSS (Shrine):";
    else if (g.app_id == 7) view_label = lsh_in_sum_mode() ? "SUM:" : "LSH:";
    else if (g.app_id == 8) view_label = "Output:";
    else if (g.app_id == 9) view_label = "Editor:";
    fb_draw_text(tgt, (uint16_t)(g.win_x + 16), (uint16_t)(g.win_y + 168),
                 g.loading ? "Response: Fetching..." : view_label, 0xFFFFFFFF, win_bg);
    int cols = (view_w - 12) / 8; // leave scrollbar space
    int rows = view_h / 10;
    if (cols < 10) cols = 10;
    if (rows < 4) rows = 4;

    uint16_t rx = (uint16_t)view_x;
    uint16_t ry = (uint16_t)view_y;
    uint16_t col = 0;
    uint16_t row = 0;
    int line = 0;
    g.resp_total_lines = 0;

    /* LARSH: advance and render when viewing demo.larsh */
    if (g.app_id == 3 && g.gallery_sel == 6 && g.larsh_loaded && g.larsh_playing) {
        larsh_render_frame(&g.larsh_scene, g.larsh_tick, g.larsh_pixels, LARSH_VIEW_W, LARSH_VIEW_H);
        g.larsh_tick++;
        int scale = 1;
        if ((int)LARSH_VIEW_W * scale > view_w - 12) scale = (view_w - 12) / (int)LARSH_VIEW_W;
        if ((int)LARSH_VIEW_H * scale > view_h) scale = view_h / (int)LARSH_VIEW_H;
        if (scale < 1) scale = 1;
        for (uint32_t py = 0; py < LARSH_VIEW_H; py++) {
            for (uint32_t px = 0; px < LARSH_VIEW_W; px++) {
                uint32_t c = g.larsh_pixels[py * LARSH_VIEW_W + px];
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_putpixel(tgt, (uint16_t)(view_x + (int)px * scale + sx), (uint16_t)(view_y + (int)py * scale + sy), c);
            }
        }
    }

    /* Gallery: draw BMP when viewing sample.bmp; assign to U+E000 for inline use */
    if (g.app_id == 3 && g.gallery_sel == 2) {
        const FsFile* bf = fs_open("sample.bmp");
        if (bf && bf->size >= 54) {
            bmp_result_t br = { g.gallery_pixels, 0, 0, 0 };
            if (bmp_decode(bf->data, bf->size, &br) == 0 && br.w > 0 && br.h > 0) {
                img_glyph_assign(0xE000u, g.gallery_pixels, (uint16_t)br.w, (uint16_t)br.h);
                int scale = 8;
                if ((int)br.w * scale > view_w - 12) scale = (view_w - 12) / (int)br.w;
                if ((int)br.h * scale > view_h) scale = view_h / (int)br.h;
                if (scale < 1) scale = 1;
                for (uint32_t py = 0; py < br.h; py++) {
                    for (uint32_t px = 0; px < br.w; px++) {
                        uint32_t c = g.gallery_pixels[py * br.w + px];
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++)
                                fb_putpixel(tgt, (uint16_t)(view_x + (int)px * scale + sx), (uint16_t)(view_y + (int)py * scale + sy), c);
                    }
                }
                int cap_y = view_y + (int)br.h * scale + 6;
                fb_draw_text(tgt, (uint16_t)view_x, (uint16_t)cap_y, "U+E000 = ", 0xFFFFFFFF, win_bg);
                const uint32_t* gpx;
                uint16_t gw, gh;
                if (img_glyph_get(0xE000u, &gpx, &gw, &gh)) {
                    fb_draw_image(tgt, (uint16_t)(view_x + 9 * 8), (uint16_t)cap_y, gpx, gw, gh);
                }
            }
        }
    }

    int draw_resp = 1;
    if (g.app_id == 3 && (g.gallery_sel == 2 || (g.gallery_sel == 6 && g.larsh_loaded))) draw_resp = 0;
    if (draw_resp) {
        const char* ptr = (g.app_id == 9 && !g.lafaelo_show_run) ? g.lafaelo_buf : g.resp;
        while (*ptr) {
            uint32_t cp = utf8_next(&ptr);
            if (cp == 0) break;
            if (cp == '\r') continue;
            if (cp == '\n' || col >= (uint16_t)cols) {
                line++;
                col = 0;
                if (cp == '\n') continue;
            }
            int visible_row = line - g.resp_scroll;
            int on_screen = visible_row >= 0 && visible_row < rows;
            if (on_screen) row = (uint16_t)visible_row;

            if (cp >= IMG_GLYPH_PUA_START && cp <= IMG_GLYPH_PUA_END) {
                const uint32_t* px;
                uint16_t gw, gh;
                if (img_glyph_get(cp, &px, &gw, &gh)) {
                    if (on_screen) {
                        fb_draw_image(tgt, (uint16_t)(rx + col * 8), (uint16_t)(ry + row * 10), px, gw, gh);
                    }
                    col++;
                    continue;
                }
            }

            if (cp < 32 || cp > 127) cp = '?';
            if (on_screen) {
                fb_draw_char(tgt, (uint16_t)(rx + col * 8), (uint16_t)(ry + row * 10), (char)cp, 0xFFFFFFFF, win_bg);
            }
            col++;
        }
    }
    g.resp_total_lines = line + 1;

    // Clamp scroll
    if (g.resp_scroll < 0) g.resp_scroll = 0;
    if (g.resp_scroll > g.resp_total_lines) g.resp_scroll = g.resp_total_lines;

    // Scrollbar
    int sb_w = 10;
    int sb_x = view_x + view_w - sb_w;
    int sb_y = view_y;
    int sb_h = view_h;
    uint32_t sb_bg = 0xFF101018;
    uint32_t sb_bd = 0xFF000000;
    uint32_t sb_th = 0xFF8090A0;
    fb_fill_rect(tgt, (uint16_t)sb_x, (uint16_t)sb_y, (uint16_t)sb_w, (uint16_t)sb_h, sb_bg);
    fb_fill_rect(tgt, (uint16_t)sb_x, (uint16_t)sb_y, 1, (uint16_t)sb_h, sb_bd);
    fb_fill_rect(tgt, (uint16_t)(sb_x + sb_w - 1), (uint16_t)sb_y, 1, (uint16_t)sb_h, sb_bd);

    int max_scroll = g.resp_total_lines > 1 ? (g.resp_total_lines - 1) : 0;
    int thumb_h = sb_h / 4;
    if (thumb_h < 12) thumb_h = 12;
    if (thumb_h > sb_h) thumb_h = sb_h;
    int thumb_y = sb_y;
    if (max_scroll > 0 && sb_h > thumb_h) {
        thumb_y = sb_y + (g.resp_scroll * (sb_h - thumb_h)) / max_scroll;
    }
    fb_fill_rect(tgt, (uint16_t)(sb_x + 1), (uint16_t)thumb_y, (uint16_t)(sb_w - 2), (uint16_t)thumb_h, sb_th);

    guioverlay_state_t overlay = {
        (uint32_t)g.win_x, (uint32_t)g.win_y, (uint32_t)g.win_w, (uint32_t)g.win_h,
        (uint32_t)g.mx, (uint32_t)g.my, (uint32_t)g.app_id, (uint32_t)g.settings_open,
        (uint32_t)g.btn_pressed, (uint32_t)g.tb_focused, (uint32_t)g.loading,
        (uint32_t)g.http_post_mode, (uint32_t)g.user_sandbox,
    };
    guioverlay_draw(&overlay);

    /* Settings overlay */
    if (g.settings_open) {
        int panel_x = g.win_x + g.win_w - 200;
        int panel_y = g.win_y + 24;
        int panel_w = 196;
        int panel_h = 108;
        uint32_t panel_bg = 0xFF282840;
        fb_fill_rect(tgt, (uint16_t)panel_x, (uint16_t)panel_y, (uint16_t)panel_w, (uint16_t)panel_h, panel_bg);
        fb_fill_rect(tgt, (uint16_t)panel_x, (uint16_t)panel_y, (uint16_t)panel_w, 1, 0xFF000000);
        fb_fill_rect(tgt, (uint16_t)panel_x, (uint16_t)(panel_y + panel_h - 1), (uint16_t)panel_w, 1, 0xFF000000);
        int track_x = panel_x + 100;
        int track_w = 90;
        int row_h = 32;
        fb_draw_text(tgt, (uint16_t)(panel_x + 8), (uint16_t)(panel_y + 8), "Brightness", 0xFFFFFFFF, panel_bg);
        fb_fill_rect(tgt, (uint16_t)track_x, (uint16_t)(panel_y + 12), (uint16_t)track_w, 8, 0xFF101018);
        int br_pos = (g.brightness - 50) * track_w / 100;
        if (br_pos < 0) br_pos = 0;
        if (br_pos > track_w - 6) br_pos = track_w - 6;
        fb_fill_rect(tgt, (uint16_t)(track_x + br_pos), (uint16_t)(panel_y + 10), 6, 12, 0xFF8090A0);
        fb_draw_text(tgt, (uint16_t)(panel_x + 8), (uint16_t)(panel_y + 8 + row_h), "Volume", 0xFFFFFFFF, panel_bg);
        fb_fill_rect(tgt, (uint16_t)track_x, (uint16_t)(panel_y + 12 + row_h), (uint16_t)track_w, 8, 0xFF101018);
        int vol_pos = g.volume * track_w / 100;
        if (vol_pos > track_w - 6) vol_pos = track_w - 6;
        fb_fill_rect(tgt, (uint16_t)(track_x + vol_pos), (uint16_t)(panel_y + 10 + row_h), 6, 12, 0xFF8090A0);
        fb_draw_text(tgt, (uint16_t)(panel_x + 8), (uint16_t)(panel_y + 8 + row_h * 2), "Quality", 0xFFFFFFFF, panel_bg);
        fb_fill_rect(tgt, (uint16_t)track_x, (uint16_t)(panel_y + 12 + row_h * 2), (uint16_t)track_w, 8, 0xFF101018);
        int q_pos = g.quality * track_w / 2;
        if (q_pos > track_w - 6) q_pos = track_w - 6;
        fb_fill_rect(tgt, (uint16_t)(track_x + q_pos), (uint16_t)(panel_y + 10 + row_h * 2), 6, 12, 0xFF8090A0);
    }

    exgui_draw_overlay();
    exexgui_draw_overlay();

    // Cursor last
    gui_draw_cursor_at(g.mx, g.my, 0xFFFFFFFF);
    screenram_flush_to_target(tgt);

    if (g_have_bb) {
        fb_blit(&g_fb, &g_bb);
    }
    g_syscall_target_override = 0;
}

void gui_demo(void)
{
    if (gui_init() != 0) return;
    gui_render();
}

/* Syscall target. Override set at start of gui_render so lib3d etc. draw to current buffer. */
static const fb_t* gui_syscall_target(void)
{
    if (g_syscall_target_override) return g_syscall_target_override;
    return g_have_fb ? &g_fb : 0;
}

void gui_syscall_put_pixel(uint16_t x, uint16_t y, uint32_t argb)
{
    const fb_t* t = gui_syscall_target();
    if (t) fb_putpixel(t, x, y, argb);
}

void gui_syscall_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t argb)
{
    const fb_t* t = gui_syscall_target();
    if (t) fb_fill_rect(t, x, y, w, h, argb);
}

void gui_syscall_draw_text(uint16_t x, uint16_t y, const char* s, uint32_t fg, uint32_t bg)
{
    const fb_t* t = gui_syscall_target();
    if (t && s) fb_draw_text(t, x, y, s, fg, bg);
}

void gui_syscall_clear(uint32_t argb)
{
    const fb_t* t = gui_syscall_target();
    if (t) fb_clear(t, argb);
}

uint16_t gui_syscall_get_width(void)
{
    return g_have_fb ? g_fb.w : 0;
}

uint16_t gui_syscall_get_height(void)
{
    return g_have_fb ? g_fb.h : 0;
}
