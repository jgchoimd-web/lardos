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
#include "guioverlay.h"
#include "lguilib.h"
#include "lassist.h"
#include "syscall.h"
#include "lib3d_demo.h"
#include "version.h"

#define LARSH_VIEW_W 160
#define LARSH_VIEW_H 120
#define GUI_GLYPH_HITS_MAX 64u
#define GUI_APP_COUNT 10
#define GUI_DESKTOP_ITEM_MAX 24
#define GUI_DOCK_ITEM_MAX 16
#define GUI_ITEM_NONE 0
#define GUI_ITEM_APP 1
#define GUI_ITEM_FOLDER 2
#define GUI_TOP_NEW_FOLDER 1
#define GUI_TOP_PIN_DESKTOP 2
#define GUI_TOP_PIN_DOCK 3

typedef struct {
    int app;
    const char* name;
    const char* icon;
    uint32_t color;
} gui_launcher_t;

static const gui_launcher_t s_desktop_launchers[] = {
    { 0, "Docs",  "D", 0xFF2E8FBAu },
    { 7, "Shell", ">", 0xFF3AA66Fu },
    { 2, "Notes", "N", 0xFFE3A447u },
    { 3, "Pix",   "P", 0xFFC86DD7u },
    { 4, "Pak",   "K", 0xFF6F8BDCu },
    { 9, "Edit",  "E", 0xFFE06A6Au },
};

static const gui_launcher_t s_dock_launchers[] = {
    { 0, "Doc",  "D", 0xFF2E8FBAu },
    { 7, "LSH",  ">", 0xFF3AA66Fu },
    { 2, "Note", "N", 0xFFE3A447u },
    { 3, "Pix",  "P", 0xFFC86DD7u },
    { 4, "Pak",  "K", 0xFF6F8BDCu },
    { 1, "Calc", "=", 0xFF48A9A6u },
    { 8, "Play", "R", 0xFF8BC34Au },
    { 5, "User", "U", 0xFFB88746u },
    { 9, "Edit", "E", 0xFFE06A6Au },
};

typedef struct {
    int used;
    int kind;
    int app;
    int x;
    int y;
    int w;
    int h;
    char name[24];
    char icon[4];
    uint32_t color;
} gui_item_t;

typedef struct {
    int app;
    int initialized;
    int opened_once;
    int visible;
    int x;
    int y;
    int w;
    int h;
    int fullscreen;
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;
    uint32_t z;
} gui_window_t;

typedef struct {
    int saved;
    char tb[256];
    uint32_t tb_len;
    uint32_t tb_cur;
    char resp[4096];
    int resp_scroll;
    int resp_total_lines;
    char calc_display[32];
    uint32_t calc_len;
    uint32_t calc_cur;
    int gallery_sel;
    int user_sandbox;
    int http_post_mode;
    int lafillo_src_mode;
    char lafillo_raw[4096];
    char lafillo_extracted[4096];
    char lafaelo_buf[8192];
    uint32_t lafaelo_len;
    uint32_t lafaelo_cur;
    int lafaelo_focus;
    int lafaelo_show_run;
} gui_app_view_t;

static gui_item_t g_desktop_items[GUI_DESKTOP_ITEM_MAX];
static gui_item_t g_dock_items[GUI_DOCK_ITEM_MAX];
static int g_desktop_item_count;
static int g_dock_item_count;
static gui_window_t g_windows[GUI_APP_COUNT];
static gui_app_view_t g_app_views[GUI_APP_COUNT];
static uint32_t g_window_z_next = 1u;
static int g_folder_count;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    uint32_t cp;
} gui_glyph_hit_t;

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

static void gui_clamp_window(void);
static void gui_apply_fullscreen(void);
static void gui_select_app(int idx);
static void gui_resp_clear(void);
static void gui_resp_append(const char* s);
static void gui_desktop_init_model(void);
static void gui_bring_window_front(int app);
static void gui_sync_active_window(void);
static void gui_save_app_view(int app);

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

    // Desktop app window
    int win_visible;
    int fullscreen;
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;
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
    uint32_t glyph_tick;
    uint32_t glyph_hover_cp;
    uint32_t glyph_last_cp;
    uint32_t glyph_last_click_tick;
    uint32_t glyph_rendered_last;
    uint32_t glyph_hit_count;
    gui_glyph_hit_t glyph_hits[GUI_GLYPH_HITS_MAX];
    uint32_t glyph_render_pixels[IMG_GLYPH_SIZE * IMG_GLYPH_SIZE];
    uint32_t cursor_enabled;
    uint32_t cursor_cp;
    uint32_t cursor_render_count;
    uint32_t cursor_fallback_count;
    uint32_t cursor_last_error;
    uint32_t cursor_render_pixels[IMG_GLYPH_SIZE * IMG_GLYPH_SIZE];

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
    uint32_t ss_idle_loops;
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
    int item_drag_area; /* 1=desktop, 2=dock */
    int item_drag_index;
    int item_drag_moved;
    int item_drag_start_x;
    int item_drag_start_y;
    int item_drag_off_x;
    int item_drag_off_y;
} gui_state_t;

#define SS_IDLE_THRESHOLD  120
#define SS_IDLE_LOOP_DIVIDER 500000u

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

static void fb_draw_image_scaled(const fb_t* f, int x, int y, const uint32_t* pixels, uint16_t w, uint16_t h, uint16_t scale)
{
    if (!pixels || scale == 0) return;
    for (uint16_t py = 0; py < h; py++) {
        for (uint16_t px = 0; px < w; px++) {
            uint32_t argb = pixels[py * (uint32_t)w + px];
            if ((argb >> 24) == 0) continue;
            for (uint16_t sy = 0; sy < scale; sy++) {
                for (uint16_t sx = 0; sx < scale; sx++) {
                    int dx = x + (int)px * (int)scale + sx;
                    int dy = y + (int)py * (int)scale + sy;
                    if (dx >= 0 && dy >= 0 && dx < (int)f->w && dy < (int)f->h) {
                        fb_putpixel(f, (uint16_t)dx, (uint16_t)dy, argb);
                    }
                }
            }
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
    int usable_top = 30;
    int usable_bottom = sh - 58;
    int usable_h = usable_bottom - usable_top;
    if (usable_h < 180) usable_h = sh > 24 ? sh - 24 : sh;
    g.win_w = sw >= 660 ? 640 : sw - 20;
    g.win_h = usable_h >= 420 ? 420 : usable_h;
    if (g.win_w < 240) g.win_w = sw > 8 ? sw - 8 : sw;
    if (g.win_h < 180) g.win_h = sh > 8 ? sh - 8 : sh;
    g.win_x = (sw - g.win_w) / 2;
    g.win_y = usable_top + (usable_h - g.win_h) / 2;
    if (g.win_x < 0) g.win_x = 0;
    if (g.win_y < 0) g.win_y = 0;
    g.fullscreen = 0;
    g.restore_x = g.win_x;
    g.restore_y = g.win_y;
    g.restore_w = g.win_w;
    g.restore_h = g.win_h;
    gui_clamp_window();
    g.win_visible = 1;
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
    g.glyph_tick = 0;
    g.glyph_hover_cp = 0;
    g.glyph_last_cp = 0;
    g.glyph_last_click_tick = 0;
    g.glyph_rendered_last = 0;
    g.glyph_hit_count = 0;
    g.cursor_enabled = 1;
    g.cursor_cp = IMG_GLYPH_MOUSE_CURSOR_CP;
    g.cursor_render_count = 0;
    g.cursor_fallback_count = 0;
    g.cursor_last_error = img_glyph_ensure_mouse_cursor() == 0 ? 0u : 2u;
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
    g.item_drag_area = 0;
    g.item_drag_index = -1;
    g.item_drag_moved = 0;
    g.item_drag_start_x = 0;
    g.item_drag_start_y = 0;
    g.item_drag_off_x = 0;
    g.item_drag_off_y = 0;
    g.lafillo_extracted[0] = '\0';

    // Default URL
    const char* def = "file://lardos.lars";
    for (g.tb_len = 0; def[g.tb_len] && g.tb_len + 1 < sizeof(g.tb); g.tb_len++) g.tb[g.tb_len] = def[g.tb_len];
    g.tb[g.tb_len] = '\0';
    g.tb_cur = g.tb_len;
    gui_desktop_init_model();
    g_windows[0].x = g.win_x;
    g_windows[0].y = g.win_y;
    g_windows[0].w = g.win_w;
    g_windows[0].h = g.win_h;
    g_windows[0].restore_x = g.restore_x;
    g_windows[0].restore_y = g.restore_y;
    g_windows[0].restore_w = g.restore_w;
    g_windows[0].restore_h = g.restore_h;
    g_windows[0].visible = 1;
    g_windows[0].opened_once = 1;
    gui_bring_window_front(0);
    gui_save_app_view(0);

    /* Screensaver: try default.ssav */
    g.ss_active = 0;
    g.ss_idle_ticks = 0;
    g.ss_idle_loops = 0;
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
    lassist_init();
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

static void gui_draw_default_cursor(const fb_t* tgt, int x, int y, uint32_t color)
{
    static const uint8_t shape[8][8] = {
        {0,0,0,1,1,0,0,0},
        {0,0,1,2,2,1,0,0},
        {0,1,3,4,4,3,1,0},
        {0,1,4,3,4,4,1,0},
        {0,1,3,4,3,4,1,0},
        {0,1,4,3,4,3,1,0},
        {0,0,1,3,3,1,0,0},
        {0,0,0,1,1,0,0,0}
    };
    uint32_t shell = color;
    uint32_t shade = 0xFFD7DFEAu;
    uint32_t button = 0xFF30D5C8u;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint8_t px = shape[row][col];
            uint32_t c = 0;
            if (!px) continue;
            if (px == 1) c = 0xFF101018u;
            else if (px == 2) c = button;
            else if (px == 3) c = shade;
            else c = shell;
            for (int sy = 0; sy < 2; sy++) {
                for (int sx = 0; sx < 2; sx++) {
                    fb_putpixel(tgt, (uint16_t)(x + col * 2 + sx), (uint16_t)(y + row * 2 + sy), c);
                }
            }
        }
    }
}

static void gui_draw_cursor_at(int x, int y, uint32_t color)
{
    const fb_t* tgt = g_have_bb ? &g_bb : &g_fb;
    uint16_t gw = 0;
    uint16_t gh = 0;
    if (g.cursor_enabled &&
        img_glyph_render(g.cursor_cp, g.glyph_tick, 0, g.cursor_render_pixels, &gw, &gh)) {
        uint16_t scale = 3;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        fb_draw_image_scaled(tgt, x, y, g.cursor_render_pixels, gw, gh, scale);
        g.cursor_render_count++;
        g.cursor_last_error = 0;
        return;
    }
    if (g.cursor_enabled) {
        g.cursor_fallback_count++;
        g.cursor_last_error = 2;
    }

    gui_draw_default_cursor(tgt, x, y, color);
}

int gui_cursor_set_unicode(uint32_t cp)
{
    if (cp < IMG_GLYPH_PUA_START || cp > IMG_GLYPH_PUA_END) {
        g.cursor_last_error = 1;
        return -1;
    }
    g.cursor_cp = cp;
    g.cursor_enabled = 1;
    g.cursor_last_error = 0;
    return 0;
}

void gui_cursor_disable(void)
{
    g.cursor_enabled = 0;
    g.cursor_last_error = 0;
}

void gui_cursor_info(gui_cursor_info_t* out)
{
    img_glyph_info_t info;
    if (!out) return;
    out->enabled = g.cursor_enabled;
    out->cp = g.cursor_cp;
    out->assigned = img_glyph_info(g.cursor_cp, &info) == 0 ? 1u : 0u;
    out->render_count = g.cursor_render_count;
    out->fallback_count = g.cursor_fallback_count;
    out->last_error = g.cursor_last_error;
}

int gui_unicode_cursor_selftest(void)
{
    uint32_t old_enabled = g.cursor_enabled;
    uint32_t old_cp = g.cursor_cp;
    uint32_t old_render_count = g.cursor_render_count;
    uint32_t old_fallback_count = g.cursor_fallback_count;
    uint32_t old_error = g.cursor_last_error;
    gui_cursor_info_t info;
    int ok = 1;
    if (img_glyph_ensure_mouse_cursor() != 0) ok = 0;
    if (gui_cursor_set_unicode(IMG_GLYPH_MOUSE_CURSOR_CP) != 0) ok = 0;
    gui_cursor_info(&info);
    if (ok && (!info.enabled || info.cp != IMG_GLYPH_MOUSE_CURSOR_CP || !info.assigned)) ok = 0;
    if (ok && gui_cursor_set_unicode(0x41u) == 0) ok = 0;
    g.cursor_enabled = old_enabled;
    g.cursor_cp = old_cp;
    g.cursor_render_count = old_render_count;
    g.cursor_fallback_count = old_fallback_count;
    g.cursor_last_error = old_error;
    return ok ? 0 : -1;
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < (rx + rw) && y < (ry + rh);
}

static void gui_title_control_rects(int* out_set_x, int* out_min_x, int* out_full_x,
                                    int* out_close_x, int* out_y, int* out_h)
{
    int close_x = g.win_x + g.win_w - 18;
    int full_x = close_x - 18;
    int min_x = full_x - 18;
    int set_x = min_x - 42;
    if (out_set_x) *out_set_x = set_x;
    if (out_min_x) *out_min_x = min_x;
    if (out_full_x) *out_full_x = full_x;
    if (out_close_x) *out_close_x = close_x;
    if (out_y) *out_y = g.win_y;
    if (out_h) *out_h = 20;
}

static const char* gui_app_name(int app)
{
    static const char* names[] = {
        "Doc Browser", "Calculator", "Notes", "Pictures", "Package",
        "User App", "Shrine", "Lard Shell", "Play", "Editor"
    };
    if (app >= 0 && app < (int)(sizeof(names) / sizeof(names[0]))) return names[app];
    return "App";
}

static void gui_copy_text(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void gui_save_app_view(int app)
{
    gui_app_view_t* v;
    if (app < 0 || app >= GUI_APP_COUNT) return;
    v = &g_app_views[app];
    gui_copy_text(v->tb, sizeof(v->tb), g.tb);
    v->tb_len = g.tb_len;
    v->tb_cur = g.tb_cur;
    gui_copy_text(v->resp, sizeof(v->resp), g.resp);
    v->resp_scroll = g.resp_scroll;
    v->resp_total_lines = g.resp_total_lines;
    gui_copy_text(v->calc_display, sizeof(v->calc_display), g.calc_display);
    v->calc_len = g.calc_len;
    v->calc_cur = g.calc_cur;
    v->gallery_sel = g.gallery_sel;
    v->user_sandbox = g.user_sandbox;
    v->http_post_mode = g.http_post_mode;
    v->lafillo_src_mode = g.lafillo_src_mode;
    gui_copy_text(v->lafillo_raw, sizeof(v->lafillo_raw), g.lafillo_raw);
    gui_copy_text(v->lafillo_extracted, sizeof(v->lafillo_extracted), g.lafillo_extracted);
    gui_copy_text(v->lafaelo_buf, sizeof(v->lafaelo_buf), g.lafaelo_buf);
    v->lafaelo_len = g.lafaelo_len;
    v->lafaelo_cur = g.lafaelo_cur;
    v->lafaelo_focus = g.lafaelo_focus;
    v->lafaelo_show_run = g.lafaelo_show_run;
    v->saved = 1;
}

static void gui_load_app_view(int app)
{
    gui_app_view_t* v;
    if (app < 0 || app >= GUI_APP_COUNT) return;
    v = &g_app_views[app];
    if (!v->saved) return;
    gui_copy_text(g.tb, sizeof(g.tb), v->tb);
    g.tb_len = v->tb_len;
    g.tb_cur = v->tb_cur;
    gui_copy_text(g.resp, sizeof(g.resp), v->resp);
    g.resp_scroll = v->resp_scroll;
    g.resp_total_lines = v->resp_total_lines;
    gui_copy_text(g.calc_display, sizeof(g.calc_display), v->calc_display);
    g.calc_len = v->calc_len;
    g.calc_cur = v->calc_cur;
    g.gallery_sel = v->gallery_sel;
    g.user_sandbox = v->user_sandbox;
    g.http_post_mode = v->http_post_mode;
    g.lafillo_src_mode = v->lafillo_src_mode;
    gui_copy_text(g.lafillo_raw, sizeof(g.lafillo_raw), v->lafillo_raw);
    gui_copy_text(g.lafillo_extracted, sizeof(g.lafillo_extracted), v->lafillo_extracted);
    gui_copy_text(g.lafaelo_buf, sizeof(g.lafaelo_buf), v->lafaelo_buf);
    g.lafaelo_len = v->lafaelo_len;
    g.lafaelo_cur = v->lafaelo_cur;
    g.lafaelo_focus = v->lafaelo_focus;
    g.lafaelo_show_run = v->lafaelo_show_run;
}

static const gui_launcher_t* gui_launcher_for_app(int app)
{
    for (uint32_t i = 0; i < sizeof(s_dock_launchers) / sizeof(s_dock_launchers[0]); i++) {
        if (s_dock_launchers[i].app == app) return &s_dock_launchers[i];
    }
    for (uint32_t i = 0; i < sizeof(s_desktop_launchers) / sizeof(s_desktop_launchers[0]); i++) {
        if (s_desktop_launchers[i].app == app) return &s_desktop_launchers[i];
    }
    return &s_dock_launchers[0];
}

static void gui_item_from_app(gui_item_t* item, int app, int x, int y)
{
    const gui_launcher_t* l = gui_launcher_for_app(app);
    if (!item) return;
    item->used = 1;
    item->kind = GUI_ITEM_APP;
    item->app = app;
    item->x = x;
    item->y = y;
    item->w = 76;
    item->h = 62;
    gui_copy_text(item->name, sizeof(item->name), l->name);
    gui_copy_text(item->icon, sizeof(item->icon), l->icon);
    item->color = l->color;
}

static void gui_item_from_folder(gui_item_t* item, int x, int y)
{
    char name[24];
    uint32_t n = (uint32_t)(g_folder_count + 1);
    uint32_t pos = 0;
    static const char prefix[] = "Folder ";
    if (!item) return;
    for (uint32_t i = 0; prefix[i] && pos + 1u < sizeof(name); i++) name[pos++] = prefix[i];
    if (n >= 10u && pos + 1u < sizeof(name)) name[pos++] = (char)('0' + (n / 10u) % 10u);
    if (pos + 1u < sizeof(name)) name[pos++] = (char)('0' + n % 10u);
    name[pos] = '\0';
    item->used = 1;
    item->kind = GUI_ITEM_FOLDER;
    item->app = -1;
    item->x = x;
    item->y = y;
    item->w = 76;
    item->h = 62;
    gui_copy_text(item->name, sizeof(item->name), name);
    gui_copy_text(item->icon, sizeof(item->icon), "F");
    item->color = 0xFFD7B45Au;
    g_folder_count++;
}

static void gui_dock_rect(int* out_x, int* out_y, int* out_w, int* out_h, int* out_cell)
{
    int count = g_dock_item_count > 0 ? g_dock_item_count : (int)(sizeof(s_dock_launchers) / sizeof(s_dock_launchers[0]));
    int cell = g_have_fb && g_fb.w < 420 ? 32 : 44;
    int margin = 8;
    int w = count * cell + margin * 2;
    int h = cell + 12;
    int sw = g_have_fb ? (int)g_fb.w : 0;
    int sh = g_have_fb ? (int)g_fb.h : 0;
    if (w > sw - 12 && count > 0) {
        cell = (sw - 24) / count;
        if (cell < 24) cell = 24;
        w = count * cell + margin * 2;
        h = cell + 10;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_x) *out_x = (sw - w) / 2;
    if (out_y) *out_y = sh - h - 6;
    if (out_cell) *out_cell = cell;
}

static void gui_apply_fullscreen(void)
{
    if (!g_have_fb || !g.fullscreen) return;
    g.win_x = 0;
    g.win_y = 0;
    g.win_w = (int)g_fb.w;
    g.win_h = (int)g_fb.h;
}

static void gui_clamp_window(void)
{
    int min_y = 0;
    int max_x;
    int max_y;
    if (!g_have_fb) return;
    if (g.fullscreen) {
        gui_apply_fullscreen();
        return;
    }
    if (g.win_w > (int)g_fb.w) g.win_w = (int)g_fb.w;
    if (g.win_h > (int)g_fb.h) g.win_h = (int)g_fb.h;
    if (g.win_w < 160 && (int)g_fb.w >= 160) g.win_w = 160;
    if (g.win_h < 160 && (int)g_fb.h >= 160) g.win_h = 160;
    max_x = (int)g_fb.w - g.win_w;
    max_y = (int)g_fb.h - g.win_h;
    if (max_x < 0) max_x = 0;
    if (max_y < min_y) max_y = min_y;
    if (g.win_x < 0) g.win_x = 0;
    if (g.win_y < min_y) g.win_y = min_y;
    if (g.win_x > max_x) g.win_x = max_x;
    if (g.win_y > max_y) g.win_y = max_y;
}

static void gui_toggle_fullscreen(void)
{
    if (!g_have_fb) return;
    if (!g.fullscreen) {
        g.restore_x = g.win_x;
        g.restore_y = g.win_y;
        g.restore_w = g.win_w;
        g.restore_h = g.win_h;
        g.fullscreen = 1;
        gui_apply_fullscreen();
        gui_sync_active_window();
    } else {
        g.fullscreen = 0;
        if (g.restore_w > 0 && g.restore_h > 0) {
            g.win_x = g.restore_x;
            g.win_y = g.restore_y;
            g.win_w = g.restore_w;
            g.win_h = g.restore_h;
        }
        gui_clamp_window();
        gui_sync_active_window();
    }
}

static void gui_settings_panel_rect(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int panel_w = 196;
    int panel_h = 108;
    int panel_x;
    int panel_y = g.win_y + 24;
    if (g.win_w < panel_w + 8) panel_w = g.win_w > 8 ? g.win_w - 8 : g.win_w;
    if (panel_w < 120 && g.win_w >= 120) panel_w = 120;
    panel_x = g.win_x + g.win_w - panel_w - 4;
    if (panel_x < g.win_x + 4) panel_x = g.win_x + 4;
    if (panel_y + panel_h > g.win_y + g.win_h - 4) {
        panel_h = g.win_y + g.win_h - 4 - panel_y;
        if (panel_h < 72) panel_h = 72;
    }
    if (out_x) *out_x = panel_x;
    if (out_y) *out_y = panel_y;
    if (out_w) *out_w = panel_w;
    if (out_h) *out_h = panel_h;
}

static void gui_default_item_rect(int index, int* out_x, int* out_y, int* out_w, int* out_h)
{
    int cols = g_have_fb && g_fb.w >= 620 ? 2 : 1;
    int col = index % cols;
    int row = index / cols;
    if (out_x) *out_x = 18 + col * 88;
    if (out_y) *out_y = 44 + row * 74;
    if (out_w) *out_w = 76;
    if (out_h) *out_h = 62;
}

static void gui_next_desktop_spot(int* out_x, int* out_y)
{
    int x, y, w, h;
    int idx = g_desktop_item_count;
    if (idx >= GUI_DESKTOP_ITEM_MAX) idx = GUI_DESKTOP_ITEM_MAX - 1;
    gui_default_item_rect(idx, &x, &y, &w, &h);
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static int gui_desktop_hit_item(int x, int y)
{
    for (int i = g_desktop_item_count - 1; i >= 0; i--) {
        gui_item_t* item = &g_desktop_items[i];
        if (item->used && in_rect(x, y, item->x, item->y, item->w, item->h)) return i;
    }
    return -1;
}

static int gui_dock_hit_item(int x, int y)
{
    int dx, dy, dw, dh, cell;
    gui_dock_rect(&dx, &dy, &dw, &dh, &cell);
    if (!in_rect(x, y, dx, dy, dw, dh)) return -1;
    for (int i = 0; i < g_dock_item_count; i++) {
        int ix = dx + 8 + i * cell;
        if (in_rect(x, y, ix, dy + 6, cell, cell)) return i;
    }
    return -1;
}

static int gui_top_action_hit(int x, int y)
{
    if (!in_rect(x, y, 0, 0, (int)g_fb.w, 24)) return 0;
    if (in_rect(x, y, 126, 3, 86, 18)) return GUI_TOP_NEW_FOLDER;
    if (in_rect(x, y, 218, 3, 70, 18)) return GUI_TOP_PIN_DESKTOP;
    if (in_rect(x, y, 294, 3, 64, 18)) return GUI_TOP_PIN_DOCK;
    return 0;
}

static void gui_draw_top_button(const fb_t* tgt, int x, const char* label, int w, int hot)
{
    uint32_t bg = hot ? 0xFF235D64u : 0xFF1C2632u;
    fb_fill_rect(tgt, (uint16_t)x, 3, (uint16_t)w, 18, bg);
    fb_fill_rect(tgt, (uint16_t)x, 3, (uint16_t)w, 1, 0xFF5B6E78u);
    fb_draw_text(tgt, (uint16_t)(x + 6), 9, label, 0xFFFFFFFFu, bg);
}

static void gui_draw_launcher(const fb_t* tgt, const gui_item_t* item, int x, int y,
                              int w, int h, int active, int label)
{
    int icon = h - (label ? 18 : 8);
    int ix = x + (w - icon) / 2;
    int iy = y + 4;
    uint32_t bg = active ? 0xFF2B3C46u : 0xFF1A222Bu;
    uint32_t border = active ? 0xFFFFD166u : 0xFF53636Au;
    uint32_t color = item ? item->color : 0xFF2E8FBAu;
    if (icon < 18) icon = 18;
    fb_fill_rect(tgt, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, bg);
    fb_fill_rect(tgt, (uint16_t)x, (uint16_t)y, (uint16_t)w, 1, border);
    fb_fill_rect(tgt, (uint16_t)x, (uint16_t)(y + h - 1), (uint16_t)w, 1, 0xFF080B0Fu);
    fb_fill_rect(tgt, (uint16_t)x, (uint16_t)y, 1, (uint16_t)h, border);
    fb_fill_rect(tgt, (uint16_t)(x + w - 1), (uint16_t)y, 1, (uint16_t)h, 0xFF080B0Fu);
    fb_fill_rect(tgt, (uint16_t)ix, (uint16_t)iy, (uint16_t)icon, (uint16_t)icon, color);
    fb_fill_rect(tgt, (uint16_t)ix, (uint16_t)iy, (uint16_t)icon, 2, 0xFFFFFFFFu);
    fb_draw_text(tgt, (uint16_t)(ix + icon / 2 - 4), (uint16_t)(iy + icon / 2 - 3),
                 item ? item->icon : "?", 0xFFFFFFFFu, color);
    if (label) {
        fb_draw_text(tgt, (uint16_t)(x + 6), (uint16_t)(y + h - 13),
                     item ? item->name : "Item", 0xFFFFFFFFu, bg);
    } else if (active) {
        fb_fill_rect(tgt, (uint16_t)(x + w / 2 - 8), (uint16_t)(y + h - 4), 16, 2, 0xFFFFD166u);
    }
}

static void gui_draw_desktop(const fb_t* tgt)
{
    int sw = (int)g_fb.w;
    int sh = (int)g_fb.h;
    int dock_x, dock_y, dock_w, dock_h, cell;
    fb_fill_rect(tgt, 0, 0, g_fb.w, g_fb.h, 0xFF10151Eu);
    for (int y = 24; y < sh; y += 28) fb_fill_rect(tgt, 0, (uint16_t)y, g_fb.w, 1, 0xFF151E29u);
    for (int x = 0; x < sw; x += 36) fb_fill_rect(tgt, (uint16_t)x, 24, 1, (uint16_t)(sh > 24 ? sh - 24 : 0), 0xFF111923u);
    fb_fill_rect(tgt, 0, 0, g_fb.w, 24, 0xFF121821u);
    fb_fill_rect(tgt, 0, 23, g_fb.w, 1, 0xFF2F8EA3u);
    fb_draw_text(tgt, 10, 8, "LARDOS DESKTOP", 0xFFFFFFFFu, 0xFF121821u);
    gui_draw_top_button(tgt, 126, "New Folder", 86, gui_top_action_hit(g.mx, g.my) == GUI_TOP_NEW_FOLDER);
    gui_draw_top_button(tgt, 218, "Pin App", 70, gui_top_action_hit(g.mx, g.my) == GUI_TOP_PIN_DESKTOP);
    gui_draw_top_button(tgt, 294, "Dock +", 64, gui_top_action_hit(g.mx, g.my) == GUI_TOP_PIN_DOCK);
    fb_draw_text(tgt, (uint16_t)(sw > 116 ? sw - 116 : 10), 8, LARDOS_VERSION, 0xFF9DEAE4u, 0xFF121821u);

    for (int i = 0; i < g_desktop_item_count; i++) {
        gui_item_t* item = &g_desktop_items[i];
        if (!item->used) continue;
        int active = item->kind == GUI_ITEM_APP && item->app == g.app_id && g_windows[item->app].visible;
        gui_draw_launcher(tgt, item, item->x, item->y, item->w, item->h, active, 1);
    }

    gui_dock_rect(&dock_x, &dock_y, &dock_w, &dock_h, &cell);
    fb_fill_rect(tgt, (uint16_t)dock_x, (uint16_t)dock_y, (uint16_t)dock_w, (uint16_t)dock_h, 0xFF151B22u);
    fb_fill_rect(tgt, (uint16_t)dock_x, (uint16_t)dock_y, (uint16_t)dock_w, 1, 0xFF5B6E78u);
    for (int i = 0; i < g_dock_item_count; i++) {
        int x = dock_x + 8 + i * cell;
        int y = dock_y + 6;
        gui_item_t* item = &g_dock_items[i];
        int active = item->kind == GUI_ITEM_APP && item->app == g.app_id && g_windows[item->app].visible;
        gui_draw_launcher(tgt, item, x, y, cell - 4, cell, active, 0);
    }
}

static void gui_default_window_rect(int app, int* out_x, int* out_y, int* out_w, int* out_h)
{
    int sw = (int)g_fb.w;
    int sh = (int)g_fb.h;
    int w = sw >= 660 ? 640 : sw - 20;
    int h = sh >= 520 ? 420 : sh - 70;
    int x;
    int y;
    if (w < 240) w = sw > 8 ? sw - 8 : sw;
    if (h < 180) h = sh > 8 ? sh - 8 : sh;
    x = (sw - w) / 2 + (app % 4) * 22 - 33;
    y = 34 + (app % 5) * 18;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > sw) x = sw - w;
    if (y + h > sh) y = sh - h;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void gui_window_ensure(int app)
{
    gui_window_t* w;
    int x, y, ww, wh;
    if (app < 0 || app >= GUI_APP_COUNT) return;
    w = &g_windows[app];
    if (w->initialized) return;
    gui_default_window_rect(app, &x, &y, &ww, &wh);
    w->app = app;
    w->initialized = 1;
    w->opened_once = 0;
    w->visible = 0;
    w->x = x;
    w->y = y;
    w->w = ww;
    w->h = wh;
    w->fullscreen = 0;
    w->restore_x = x;
    w->restore_y = y;
    w->restore_w = ww;
    w->restore_h = wh;
    w->z = 0;
}

static void gui_load_window_to_legacy(int app)
{
    gui_window_t* w;
    if (app < 0 || app >= GUI_APP_COUNT) return;
    gui_window_ensure(app);
    w = &g_windows[app];
    g.app_id = app;
    g.win_visible = w->visible;
    g.win_x = w->x;
    g.win_y = w->y;
    g.win_w = w->w;
    g.win_h = w->h;
    g.fullscreen = w->fullscreen;
    g.restore_x = w->restore_x;
    g.restore_y = w->restore_y;
    g.restore_w = w->restore_w;
    g.restore_h = w->restore_h;
}

static void gui_sync_active_window(void)
{
    gui_window_t* w;
    if (g.app_id < 0 || g.app_id >= GUI_APP_COUNT) return;
    gui_window_ensure(g.app_id);
    w = &g_windows[g.app_id];
    w->visible = g.win_visible;
    w->x = g.win_x;
    w->y = g.win_y;
    w->w = g.win_w;
    w->h = g.win_h;
    w->fullscreen = g.fullscreen;
    w->restore_x = g.restore_x;
    w->restore_y = g.restore_y;
    w->restore_w = g.restore_w;
    w->restore_h = g.restore_h;
}

static void gui_bring_window_front(int app)
{
    if (app < 0 || app >= GUI_APP_COUNT) return;
    gui_window_ensure(app);
    g_windows[app].z = ++g_window_z_next;
}

static int gui_top_window_at(int x, int y)
{
    uint32_t best_z = 0;
    int best = -1;
    for (int i = 0; i < GUI_APP_COUNT; i++) {
        gui_window_t* w = &g_windows[i];
        if (!w->initialized || !w->visible) continue;
        if (in_rect(x, y, w->x, w->y, w->w, w->h) && w->z >= best_z) {
            best_z = w->z;
            best = i;
        }
    }
    return best;
}

static int gui_top_visible_window(void)
{
    uint32_t best_z = 0;
    int best = -1;
    for (int i = 0; i < GUI_APP_COUNT; i++) {
        gui_window_t* w = &g_windows[i];
        if (!w->initialized || !w->visible) continue;
        if (w->z >= best_z) {
            best_z = w->z;
            best = i;
        }
    }
    return best;
}

static void gui_activate_top_window_or_none(void)
{
    int top = gui_top_visible_window();
    if (top >= 0) {
        gui_load_window_to_legacy(top);
    } else {
        g.win_visible = 0;
        g.dragging = 0;
        g.scroll_drag = 0;
        g.slider_drag = 0;
        g.settings_open = 0;
    }
}

static int gui_desktop_add_app(int app)
{
    int x, y;
    if (g_desktop_item_count >= GUI_DESKTOP_ITEM_MAX) return -1;
    gui_next_desktop_spot(&x, &y);
    gui_item_from_app(&g_desktop_items[g_desktop_item_count++], app, x, y);
    return 0;
}

static int gui_desktop_add_folder(void)
{
    int x, y;
    if (g_desktop_item_count >= GUI_DESKTOP_ITEM_MAX) return -1;
    gui_next_desktop_spot(&x, &y);
    gui_item_from_folder(&g_desktop_items[g_desktop_item_count++], x, y);
    return 0;
}

static int gui_dock_add_app(int app)
{
    if (g_dock_item_count >= GUI_DOCK_ITEM_MAX) return -1;
    for (int i = 0; i < g_dock_item_count; i++) {
        if (g_dock_items[i].used && g_dock_items[i].kind == GUI_ITEM_APP && g_dock_items[i].app == app) return 0;
    }
    gui_item_from_app(&g_dock_items[g_dock_item_count++], app, 0, 0);
    return 0;
}

static void gui_desktop_init_model(void)
{
    if (g_desktop_item_count == 0) {
        for (uint32_t i = 0; i < sizeof(s_desktop_launchers) / sizeof(s_desktop_launchers[0]); i++) {
            int x, y, w, h;
            gui_default_item_rect((int)i, &x, &y, &w, &h);
            gui_item_from_app(&g_desktop_items[g_desktop_item_count++], s_desktop_launchers[i].app, x, y);
        }
    }
    if (g_dock_item_count == 0) {
        for (uint32_t i = 0; i < sizeof(s_dock_launchers) / sizeof(s_dock_launchers[0]); i++) {
            gui_item_from_app(&g_dock_items[g_dock_item_count++], s_dock_launchers[i].app, 0, 0);
        }
    }
    for (int i = 0; i < GUI_APP_COUNT; i++) gui_window_ensure(i);
}

static void gui_reorder_dock_item(int index, int mouse_x)
{
    int dx, dy, dw, dh, cell;
    int target;
    gui_item_t moving;
    if (index < 0 || index >= g_dock_item_count) return;
    gui_dock_rect(&dx, &dy, &dw, &dh, &cell);
    (void)dy;
    (void)dw;
    (void)dh;
    target = (mouse_x - dx - 8) / cell;
    if (target < 0) target = 0;
    if (target >= g_dock_item_count) target = g_dock_item_count - 1;
    if (target == index) return;
    moving = g_dock_items[index];
    if (target > index) {
        for (int i = index; i < target; i++) g_dock_items[i] = g_dock_items[i + 1];
    } else {
        for (int i = index; i > target; i--) g_dock_items[i] = g_dock_items[i - 1];
    }
    g_dock_items[target] = moving;
    g.item_drag_index = target;
}

static void gui_item_activate(const gui_item_t* item)
{
    if (!item || !item->used) return;
    if (item->kind == GUI_ITEM_APP) {
        gui_select_app(item->app);
    } else if (item->kind == GUI_ITEM_FOLDER) {
        gui_select_app(2);
        gui_resp_clear();
        gui_resp_append(item->name);
        gui_resp_append("\nEmpty folder. Drag it anywhere on the desktop.\n");
        gui_save_app_view(2);
    }
}

static void gui_draw_window_preview(const fb_t* tgt, const gui_window_t* w)
{
    uint32_t frame = 0xFF0B0D10u;
    uint32_t bg = 0xFF1A222Bu;
    uint32_t title = 0xFF172126u;
    const char* preview = "";
    int px;
    int py;
    int cols;
    int rows;
    int col = 0;
    int row = 0;
    if (!w || !w->visible) return;
    fb_fill_rect(tgt, (uint16_t)w->x, (uint16_t)w->y, (uint16_t)w->w, (uint16_t)w->h, bg);
    fb_fill_rect(tgt, (uint16_t)w->x, (uint16_t)w->y, (uint16_t)w->w, 20, title);
    fb_draw_text(tgt, (uint16_t)(w->x + 8), (uint16_t)(w->y + 7), gui_app_name(w->app), 0xFFFFFFFFu, title);
    fb_fill_rect(tgt, (uint16_t)w->x, (uint16_t)w->y, (uint16_t)w->w, 1, frame);
    fb_fill_rect(tgt, (uint16_t)w->x, (uint16_t)(w->y + w->h - 1), (uint16_t)w->w, 1, frame);
    fb_fill_rect(tgt, (uint16_t)w->x, (uint16_t)w->y, 1, (uint16_t)w->h, frame);
    fb_fill_rect(tgt, (uint16_t)(w->x + w->w - 1), (uint16_t)w->y, 1, (uint16_t)w->h, frame);
    if (w->w > 80 && w->h > 80) {
        fb_fill_rect(tgt, (uint16_t)(w->x + 14), (uint16_t)(w->y + 44), (uint16_t)(w->w - 28), (uint16_t)(w->h - 62), 0xFF20252Bu);
        if (w->app >= 0 && w->app < GUI_APP_COUNT && g_app_views[w->app].saved) preview = g_app_views[w->app].resp;
        if (!preview || !preview[0]) preview = gui_app_name(w->app);
        px = w->x + 22;
        py = w->y + 56;
        cols = (w->w - 44) / 8;
        rows = (w->h - 78) / 10;
        if (cols < 4) cols = 4;
        if (rows < 1) rows = 1;
        while (*preview && row < rows) {
            char ch = *preview++;
            if (ch == '\r') continue;
            if (ch == '\n' || col >= cols) {
                row++;
                col = 0;
                if (ch == '\n') continue;
                if (row >= rows) break;
            }
            if (ch < 32 || ch > 126) ch = '?';
            fb_draw_char(tgt, (uint16_t)(px + col * 8), (uint16_t)(py + row * 10), ch, 0xFFCFE3FFu, 0xFF20252Bu);
            col++;
        }
    }
}

static void gui_draw_inactive_windows(const fb_t* tgt)
{
    uint32_t last_z = 0;
    for (;;) {
        uint32_t best_z = 0xFFFFFFFFu;
        int best = -1;
        for (int i = 0; i < GUI_APP_COUNT; i++) {
            gui_window_t* w = &g_windows[i];
            if (!w->initialized || !w->visible || i == g.app_id) continue;
            if (w->z > last_z && w->z < best_z) {
                best_z = w->z;
                best = i;
            }
        }
        if (best < 0) break;
        gui_draw_window_preview(tgt, &g_windows[best]);
        last_z = best_z;
    }
}

static void gui_view_rect(int* out_x, int* out_y, int* out_w, int* out_h)
{
    int content_y = g.win_y + 44;
    int tb_y = content_y + 118;
    int tb_h = 24;
    int view_y = tb_y + tb_h + 28;
    int view_h = (g.win_y + g.win_h) - view_y - 12;
    if (view_h < 64) view_h = 64;
    if (out_x) *out_x = g.win_x + 16;
    if (out_y) *out_y = view_y;
    if (out_w) *out_w = g.win_w - 32;
    if (out_h) *out_h = view_h;
}

static int gui_rows_for_view_h(int view_h)
{
    int rows = view_h / 10;
    return rows < 4 ? 4 : rows;
}

static int gui_max_scroll_for_rows(int rows)
{
    int max_scroll = g.resp_total_lines - rows;
    return max_scroll > 0 ? max_scroll : 0;
}

static void gui_clamp_scroll_for_rows(int rows)
{
    int max_scroll = gui_max_scroll_for_rows(rows);
    if (g.resp_scroll < 0) g.resp_scroll = 0;
    if (g.resp_scroll > max_scroll) g.resp_scroll = max_scroll;
}

static void gui_scrollbar_metrics(int sb_y, int sb_h, int rows,
                                  int* out_max_scroll, int* out_thumb_y, int* out_thumb_h)
{
    int total = g.resp_total_lines;
    int max_scroll;
    int thumb_h;
    int thumb_y = sb_y;
    if (total < 1) total = 1;
    if (rows < 1) rows = 1;
    if (total < rows) total = rows;

    max_scroll = gui_max_scroll_for_rows(rows);
    gui_clamp_scroll_for_rows(rows);

    thumb_h = (sb_h * rows) / total;
    if (thumb_h < 12) thumb_h = 12;
    if (thumb_h > sb_h) thumb_h = sb_h;
    if (max_scroll > 0 && sb_h > thumb_h) {
        thumb_y = sb_y + (g.resp_scroll * (sb_h - thumb_h)) / max_scroll;
    }

    if (out_max_scroll) *out_max_scroll = max_scroll;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
}

static void gui_scrollbar_track_rect(int sb_y, int sb_h, int* out_y, int* out_h)
{
    int cap = sb_h >= 36 ? 12 : 0;
    int y = sb_y + cap;
    int h = sb_h - cap * 2;
    if (h < 8) {
        y = sb_y;
        h = sb_h;
    }
    if (out_y) *out_y = y;
    if (out_h) *out_h = h;
}

static void gui_draw_scroll_arrow(const fb_t* tgt, int cx, int cy, int dir, uint32_t color)
{
    for (int i = 0; i < 4; i++) {
        int y = cy + (dir > 0 ? i : -i);
        int x = cx - i;
        int w = i * 2 + 1;
        if (x < 0 || y < 0) continue;
        fb_fill_rect(tgt, (uint16_t)x, (uint16_t)y, (uint16_t)w, 1, color);
    }
}

static void gui_glyph_hits_begin(void)
{
    g.glyph_hit_count = 0;
    g.glyph_hover_cp = 0;
    g.glyph_rendered_last = 0;
}

static void gui_glyph_register_hit(int x, int y, int w, int h, uint32_t cp)
{
    if (g.glyph_hit_count < GUI_GLYPH_HITS_MAX) {
        gui_glyph_hit_t* hit = &g.glyph_hits[g.glyph_hit_count++];
        hit->x = x;
        hit->y = y;
        hit->w = w;
        hit->h = h;
        hit->cp = cp;
    }
    g.glyph_rendered_last++;
    if (in_rect(g.mx, g.my, x, y, w, h)) g.glyph_hover_cp = cp;
}

static int gui_glyph_hit_at(int x, int y, uint32_t* out_cp)
{
    for (uint32_t i = 0; i < g.glyph_hit_count; i++) {
        gui_glyph_hit_t* hit = &g.glyph_hits[i];
        if (in_rect(x, y, hit->x, hit->y, hit->w, hit->h)) {
            if (out_cp) *out_cp = hit->cp;
            return 1;
        }
    }
    return 0;
}

static void gui_cp_label(uint32_t cp, char* out, uint32_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!out || cap < 7u) return;
    out[0] = 'U';
    out[1] = '+';
    for (int i = 3; i >= 0; i--) {
        out[5 - i] = hex[(cp >> (uint32_t)(i * 4)) & 0xFu];
    }
    out[6] = '\0';
}

int gui_img_glyph_interaction_selftest(void)
{
    uint32_t cp = 0;
    uint32_t old_count = g.glyph_hit_count;
    if (!g_have_fb) return -1;
    g.glyph_hit_count = 0;
    gui_glyph_register_hit(10, 10, 8, 8, IMG_GLYPH_PUA_START);
    if (!gui_glyph_hit_at(12, 12, &cp) || cp != IMG_GLYPH_PUA_START) {
        g.glyph_hit_count = old_count;
        return -2;
    }
    g.glyph_hit_count = old_count;
    return 0;
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
    gui_select_app(7);
    g.tb_focused = 1;
    g.lafaelo_focus = 0;
    g.lafaelo_show_run = 1;
    g.tb_len = 0;
    g.tb_cur = 0;
    g.tb[0] = '\0';
    gui_lsh_sync_output();
    gui_save_app_view(7);
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
    g.ss_idle_loops = 0;
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
        gui_select_app(3);
        g.gallery_sel = 6;
        gui_save_app_view(3);
    }
}

static void gui_select_app(int idx)
{
    int first_open;
    uint32_t uidx;
    gui_window_t* win;
    if (idx < 0) return;
    uidx = (uint32_t)idx;
    if (uidx >= GUI_APP_COUNT) return;
    gui_sync_active_window();
    gui_save_app_view(g.app_id);
    gui_window_ensure(idx);
    win = g_windows + uidx;
    first_open = !win->opened_once;
    win->visible = 1;
    win->opened_once = 1;
    gui_bring_window_front(idx);
    gui_load_window_to_legacy(idx);
    if (g_app_views[idx].saved) gui_load_app_view(idx);
    g.settings_open = 0;
    g.resp_scroll = 0;
    gui_clamp_window();
    gui_sync_active_window();
    if (!first_open && g_app_views[idx].saved) {
        if (idx == 7) {
            gui_lsh_sync_output();
            gui_save_app_view(idx);
        }
        return;
    }
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
        gui_lsh_sync_output();
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
    if (idx != 3) g.gallery_sel = -1;
    gui_save_app_view(idx);
}

void gui_handle_mouse(int dx, int dy, int buttons)
{
    int wheel = (int8_t)((buttons >> PS2_MOUSE_WHEEL_SHIFT) & 0xFF);
    buttons &= PS2_MOUSE_BUTTON_MASK;
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

    if (wheel != 0) {
        int wheel_win = gui_top_window_at(g.mx, g.my);
        int view_x, view_y, view_w, view_h;
        int rows;
        if (wheel_win >= 0 && wheel_win != g.app_id) gui_select_app(wheel_win);
        if (g.win_visible) {
            gui_view_rect(&view_x, &view_y, &view_w, &view_h);
            rows = gui_rows_for_view_h(view_h);
            if (in_rect(g.mx, g.my, view_x, view_y - 18, view_w, view_h + 18)) {
                g.resp_scroll -= wheel * 3;
                gui_clamp_scroll_for_rows(rows);
            }
        }
    }

    if (g.item_drag_area) {
        int moved_x = g.mx - g.item_drag_start_x;
        int moved_y = g.my - g.item_drag_start_y;
        if (moved_x < 0) moved_x = -moved_x;
        if (moved_y < 0) moved_y = -moved_y;
        if (moved_x + moved_y > 4) g.item_drag_moved = 1;
        if (l_down && g.item_drag_area == 1 && g.item_drag_index >= 0 && g.item_drag_index < g_desktop_item_count) {
            gui_item_t* item = &g_desktop_items[g.item_drag_index];
            int nx = g.mx - g.item_drag_off_x;
            int ny = g.my - g.item_drag_off_y;
            int max_x = (int)g_fb.w - item->w;
            int max_y = (int)g_fb.h - item->h;
            if (nx < 0) nx = 0;
            if (ny < 26) ny = 26;
            if (nx > max_x) nx = max_x;
            if (ny > max_y) ny = max_y;
            item->x = nx;
            item->y = ny;
        } else if (l_down && g.item_drag_area == 2 && g.item_drag_index >= 0 && g.item_drag_index < g_dock_item_count) {
            if (g.item_drag_moved) gui_reorder_dock_item(g.item_drag_index, g.mx);
        }
        if (l_released) {
            if (!g.item_drag_moved) {
                if (g.item_drag_area == 1 && g.item_drag_index >= 0 && g.item_drag_index < g_desktop_item_count) {
                    gui_item_activate(&g_desktop_items[g.item_drag_index]);
                } else if (g.item_drag_area == 2 && g.item_drag_index >= 0 && g.item_drag_index < g_dock_item_count) {
                    gui_item_activate(&g_dock_items[g.item_drag_index]);
                }
            }
            g.item_drag_area = 0;
            g.item_drag_index = -1;
            g.item_drag_moved = 0;
        }
        return;
    }

    if (l_pressed) {
        int hit_window = gui_top_window_at(g.mx, g.my);
        if (hit_window >= 0) {
            if (hit_window != g.app_id) gui_select_app(hit_window);
        } else {
            int action = gui_top_action_hit(g.mx, g.my);
            int item = -1;
            if (action == GUI_TOP_NEW_FOLDER) {
                (void)gui_desktop_add_folder();
                return;
            } else if (action == GUI_TOP_PIN_DESKTOP) {
                (void)gui_desktop_add_app(g.app_id);
                return;
            } else if (action == GUI_TOP_PIN_DOCK) {
                (void)gui_dock_add_app(g.app_id);
                return;
            }
            item = gui_dock_hit_item(g.mx, g.my);
            if (item >= 0) {
                g.item_drag_area = 2;
                g.item_drag_index = item;
                g.item_drag_moved = 0;
                g.item_drag_start_x = g.mx;
                g.item_drag_start_y = g.my;
                return;
            }
            item = gui_desktop_hit_item(g.mx, g.my);
            if (item >= 0) {
                gui_item_t* it = &g_desktop_items[item];
                g.item_drag_area = 1;
                g.item_drag_index = item;
                g.item_drag_moved = 0;
                g.item_drag_start_x = g.mx;
                g.item_drag_start_y = g.my;
                g.item_drag_off_x = g.mx - it->x;
                g.item_drag_off_y = g.my - it->y;
                return;
            }
            g.tb_focused = 0;
            g.lafaelo_focus = 0;
        }
    }

    if (!g.win_visible) {
        if (l_released) g.btn_pressed = 0;
        return;
    }

    if (l_pressed) {
        uint32_t cp = 0;
        if (gui_glyph_hit_at(g.mx, g.my, &cp)) {
            if (img_glyph_click(cp, g.glyph_tick) == 0) {
                img_glyph_write_lardd();
                g.glyph_last_cp = cp;
                g.glyph_last_click_tick = g.glyph_tick;
            }
        }
    }

    // Title controls.
    int title_h = 20;
    int close_btn_x;
    int min_btn_x;
    int full_btn_x;
    int set_btn_x;
    int set_btn_w = 40;
    int settings_capture = 0;
    int settings_panel_x = 0;
    int settings_panel_y = 0;
    int settings_panel_w = 0;
    int settings_panel_h = 0;
    gui_title_control_rects(&set_btn_x, &min_btn_x, &full_btn_x, &close_btn_x, 0, 0);
    if (l_pressed && in_rect(g.mx, g.my, close_btn_x, g.win_y + 2, 14, 14)) {
        g.win_visible = 0;
        g.settings_open = 0;
        g.tb_focused = 0;
        g.lafaelo_focus = 0;
        gui_save_app_view(g.app_id);
        gui_sync_active_window();
        gui_activate_top_window_or_none();
        return;
    }
    if (l_pressed && in_rect(g.mx, g.my, min_btn_x, g.win_y + 2, 14, 14)) {
        g.win_visible = 0;
        g.settings_open = 0;
        g.tb_focused = 0;
        g.lafaelo_focus = 0;
        gui_save_app_view(g.app_id);
        gui_sync_active_window();
        gui_activate_top_window_or_none();
        return;
    }
    if (l_pressed && in_rect(g.mx, g.my, full_btn_x, g.win_y + 2, 14, 14)) {
        gui_toggle_fullscreen();
        g.slider_drag = 0;
        return;
    }
    if (l_pressed && in_rect(g.mx, g.my, set_btn_x, g.win_y, set_btn_w, title_h)) {
        g.settings_open = 1 - g.settings_open;
        g.slider_drag = 0;
        return;
    }
    /* Slider hit-test on first click */
    if (g.settings_open) {
        gui_settings_panel_rect(&settings_panel_x, &settings_panel_y, &settings_panel_w, &settings_panel_h);
    }
    if (g.settings_open && l_pressed) {
        int panel_x = settings_panel_x;
        int panel_y = settings_panel_y;
        int track_x = panel_x + 100;
        int track_w = settings_panel_w - 106;
        int row_h = 32;
        int th = 20;
        if (track_w < 24) track_w = 24;
        if (in_rect(g.mx, g.my, settings_panel_x, settings_panel_y, settings_panel_w, settings_panel_h)) settings_capture = 1;
        if (in_rect(g.mx, g.my, track_x, panel_y + 4, track_w, th)) g.slider_drag = 1;
        else if (in_rect(g.mx, g.my, track_x, panel_y + 4 + row_h, track_w, th)) g.slider_drag = 2;
        else if (in_rect(g.mx, g.my, track_x, panel_y + 4 + row_h * 2, track_w, th)) g.slider_drag = 3;
    }
    /* Slider drag: while dragging, update values from mouse */
    if (g.settings_open && g.slider_drag) {
        int panel_x = settings_panel_x;
        int track_x = panel_x + 100;
        int track_w = settings_panel_w - 106;
        settings_capture = 1;
        if (track_w < 24) track_w = 24;
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
    if (g.settings_open && l_down &&
        in_rect(g.mx, g.my, settings_panel_x, settings_panel_y, settings_panel_w, settings_panel_h)) {
        settings_capture = 1;
    }
    if (l_released) {
        if (g.slider_drag) settings_capture = 1;
        g.slider_drag = 0;
    }
    if (settings_capture) return;

    // Title bar drag (top 20px of window, exclude settings button)
    int drag_w = set_btn_x - g.win_x - 4;
    if (drag_w < 0) drag_w = 0;
    if (!g.fullscreen && l_pressed && in_rect(g.mx, g.my, g.win_x, g.win_y, drag_w, title_h)) {
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
        gui_clamp_window();
        gui_sync_active_window();
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
                    gui_select_app(idx);
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
    int view_x_focus;
    int view_y_focus;
    int view_w_focus;
    int view_h_focus;
    gui_view_rect(&view_x_focus, &view_y_focus, &view_w_focus, &view_h_focus);
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
        int view_x;
        int view_y;
        int view_w;
        int view_h;
        int sb_w = 12;
        int sb_x;
        int sb_y;
        int sb_h;
        int track_y;
        int track_h;
        int rows;
        int max_scroll;
        int thumb_y;
        int thumb_h;
        gui_view_rect(&view_x, &view_y, &view_w, &view_h);
        sb_x = view_x + view_w - sb_w;
        sb_y = view_y;
        sb_h = view_h;
        gui_scrollbar_track_rect(sb_y, sb_h, &track_y, &track_h);
        rows = gui_rows_for_view_h(view_h);
        gui_scrollbar_metrics(track_y, track_h, rows, &max_scroll, &thumb_y, &thumb_h);
        if (l_pressed && in_rect(g.mx, g.my, sb_x, sb_y, sb_w, sb_h)) {
            if (max_scroll > 0) {
                if (g.my < track_y) {
                    if (g.resp_scroll > 0) g.resp_scroll--;
                    gui_clamp_scroll_for_rows(rows);
                } else if (g.my >= track_y + track_h) {
                    g.resp_scroll++;
                    gui_clamp_scroll_for_rows(rows);
                } else {
                    g.scroll_drag = 1;
                    if (in_rect(g.mx, g.my, sb_x, thumb_y, sb_w, thumb_h)) {
                        g.scroll_drag_off_y = g.my - thumb_y;
                    } else {
                        g.scroll_drag_off_y = thumb_h / 2;
                    }
                }
            }
        }
        if (l_released) {
            g.scroll_drag = 0;
        }
        if (g.scroll_drag && max_scroll > 0) {
            int travel = track_h - thumb_h;
            int y = g.my - track_y - g.scroll_drag_off_y;
            if (y < 0) y = 0;
            if (y > travel) y = travel;
            // Map y->scroll line
            g.resp_scroll = travel > 0 ? (y * max_scroll) / travel : 0;
            gui_clamp_scroll_for_rows(rows);
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
    if (!g.win_visible) return;
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
    if (!g.win_visible) return;
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
    g.glyph_tick++;
    lassist_tick((uint32_t)g.app_id);
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
        g.ss_idle_loops++;
        if (g.ss_idle_loops < SS_IDLE_LOOP_DIVIDER) return;
        g.ss_idle_loops = 0;
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
        lassist_draw((uint32_t)g.app_id, (uint32_t)g.mx, (uint32_t)g.my, 8u, 8u, 220u, 160u);
        screenram_flush_to_target(tgt);
        if (g_have_bb) fb_blit(&g_fb, &g_bb);
        g_syscall_target_override = 0;
        return;
    }

    // Full redraw for simplicity & correctness.
    fb_clear(tgt, g_bg);
    gui_glyph_hits_begin();
    gui_draw_desktop(tgt);
    gui_draw_inactive_windows(tgt);

    if (!g.win_visible) {
        lassist_draw((uint32_t)g.app_id, (uint32_t)g.mx, (uint32_t)g.my, 8u, 32u, 220u, 160u);
        screenram_flush_to_target(tgt);
        gui_draw_cursor_at(g.mx, g.my, 0xFFFFFFFF);
        if (g_have_bb) fb_blit(&g_fb, &g_bb);
        g_syscall_target_override = 0;
        return;
    }

    // Window frame
    uint32_t win_bg = 0xFF20252B;
    uint32_t title_bg = 0xFF172126;
    uint32_t border = 0xFF0B0D10;
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)g.win_y, (uint16_t)g.win_w, (uint16_t)g.win_h, win_bg);
    fb_fill_rect(tgt, (uint16_t)g.win_x, (uint16_t)g.win_y, (uint16_t)g.win_w, 20, title_bg);
    int close_btn_x;
    int min_btn_x;
    int full_btn_x;
    int set_btn_x;
    gui_title_control_rects(&set_btn_x, &min_btn_x, &full_btn_x, &close_btn_x, 0, 0);
    int app_title_x = g.win_x + 96;
    int app_title_cells = (set_btn_x - app_title_x - 4) / 8;
    fb_draw_text(tgt, (uint16_t)(g.win_x + 8), (uint16_t)(g.win_y + 7), "LARDOS GUI", 0xFFFFFFFF, title_bg);
    if (app_title_cells > 1) {
        fb_draw_text_cells(tgt, (uint16_t)app_title_x, (uint16_t)(g.win_y + 7), gui_app_name(g.app_id),
                           (uint16_t)app_title_cells, 0xFF9DEAE4u, title_bg);
    }
    uint32_t set_btn_bg = (g.settings_open || (g.mx >= set_btn_x && g.mx < set_btn_x + 40 && g.my >= g.win_y && g.my < g.win_y + 20)) ? 0xFF235D64 : 0xFF2A2F34;
    uint32_t min_btn_bg = (g.mx >= min_btn_x && g.mx < min_btn_x + 14 && g.my >= g.win_y + 2 && g.my < g.win_y + 16) ? 0xFF42504A : 0xFF2A2F34;
    uint32_t full_btn_bg = (g.mx >= full_btn_x && g.mx < full_btn_x + 14 && g.my >= g.win_y + 2 && g.my < g.win_y + 16) ? 0xFF42504A : 0xFF2A2F34;
    uint32_t close_btn_bg = (g.mx >= close_btn_x && g.mx < close_btn_x + 14 && g.my >= g.win_y + 2 && g.my < g.win_y + 16) ? 0xFFB94747 : 0xFF803B45;
    fb_fill_rect(tgt, (uint16_t)set_btn_x, (uint16_t)g.win_y, 40, 20, set_btn_bg);
    fb_draw_text(tgt, (uint16_t)(set_btn_x + 8), (uint16_t)(g.win_y + 6), "Set", 0xFFFFFFFF, set_btn_bg);
    fb_fill_rect(tgt, (uint16_t)min_btn_x, (uint16_t)(g.win_y + 2), 14, 14, min_btn_bg);
    fb_fill_rect(tgt, (uint16_t)(min_btn_x + 3), (uint16_t)(g.win_y + 12), 8, 1, 0xFFFFFFFFu);
    fb_fill_rect(tgt, (uint16_t)full_btn_x, (uint16_t)(g.win_y + 2), 14, 14, full_btn_bg);
    if (g.fullscreen) {
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 4), (uint16_t)(g.win_y + 5), 6, 1, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 4), (uint16_t)(g.win_y + 5), 1, 5, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 7), (uint16_t)(g.win_y + 8), 5, 1, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 11), (uint16_t)(g.win_y + 8), 1, 5, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 7), (uint16_t)(g.win_y + 12), 5, 1, 0xFFFFFFFFu);
    } else {
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 4), (uint16_t)(g.win_y + 5), 7, 1, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 4), (uint16_t)(g.win_y + 5), 1, 7, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 10), (uint16_t)(g.win_y + 5), 1, 7, 0xFFFFFFFFu);
        fb_fill_rect(tgt, (uint16_t)(full_btn_x + 4), (uint16_t)(g.win_y + 11), 7, 1, 0xFFFFFFFFu);
    }
    fb_fill_rect(tgt, (uint16_t)close_btn_x, (uint16_t)(g.win_y + 2), 14, 14, close_btn_bg);
    fb_draw_text(tgt, (uint16_t)(close_btn_x + 4), (uint16_t)(g.win_y + 6), "x", 0xFFFFFFFFu, close_btn_bg);
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
        uint32_t tab_bg = (g.app_id == t) ? 0xFF235D64 : 0xFF2A2F34;
        fb_fill_rect(tgt, (uint16_t)tx, (uint16_t)tab_y, (uint16_t)tab_w, (uint16_t)tab_h, tab_bg);
        fb_draw_text(tgt, (uint16_t)(tx + 4), (uint16_t)(tab_y + 8), tab_names[t], 0xFFFFFFFF, tab_bg);
    }

    int content_y = g.win_y + 20 + tab_h;

    fb_draw_text(tgt, (uint16_t)(g.win_x + 8), (uint16_t)(content_y + 4), "LardOS", 0xFFFFC857, win_bg);

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
            uint32_t dbg = g.btn_pressed ? 0xFFFFB84D : 0xFF2B7A78;
            fb_fill_rect(tgt, (uint16_t)(btn_x + dx[d]), (uint16_t)btn_y, (uint16_t)dww[d], (uint16_t)btn_h, dbg);
            fb_draw_text(tgt, (uint16_t)(btn_x + dx[d] + 4), (uint16_t)(btn_y + 10), label, 0xFFFFFFFF, dbg);
        }
    } else if (g.app_id == 9) {
        static const char* lafaelo_labels[] = { "Open", "Save", "Run" };
        int lfb = 56;
        for (int d = 0; d < 3; d++) {
            uint32_t dbg = g.btn_pressed ? 0xFFFFB84D : 0xFF2B7A78;
            fb_fill_rect(tgt, (uint16_t)(btn_x + d * (lfb + 4)), (uint16_t)btn_y, (uint16_t)lfb, (uint16_t)btn_h, dbg);
            fb_draw_text(tgt, (uint16_t)(btn_x + d * (lfb + 4) + 4), (uint16_t)(btn_y + 10), lafaelo_labels[d], 0xFFFFFFFF, dbg);
        }
    } else {
        const char* btn_label = (g.app_id == 1) ? "=" : (g.app_id == 2) ? "Save" : (g.app_id == 3) ? "View" : (g.app_id == 4) ? "Extract" : (g.app_id == 8) ? "Run" : "Run";
        uint32_t btn_bg = g.btn_pressed ? 0xFFFFB84D : 0xFF2B7A78;
        fb_fill_rect(tgt, (uint16_t)btn_x, (uint16_t)btn_y, (uint16_t)btn_w, (uint16_t)btn_h, btn_bg);
        fb_draw_text(tgt, (uint16_t)(btn_x + 12), (uint16_t)(btn_y + 10), btn_label, 0xFFFFFFFF, btn_bg);
        if (g.app_id == 5) {
            uint32_t sb_bg = g.user_sandbox ? 0xFF3DB86Du : 0xFF42504Au;
            fb_fill_rect(tgt, (uint16_t)(btn_x + btn_w + 8), (uint16_t)btn_y, 70, (uint16_t)btn_h, sb_bg);
            fb_draw_text(tgt, (uint16_t)(btn_x + btn_w + 12), (uint16_t)(btn_y + 10), "Sandbox", 0xFFFFFFFF, sb_bg);
        }
    }

    int tb_x = g.win_x + 16;
    int tb_y = content_y + 118;
    int tb_w = 260;
    int tb_h = 24;
    uint32_t tb_bg = 0xFF111619;
    uint32_t tb_bd = g.tb_focused ? 0xFFFFC857 : 0xFF60717C;
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

    int view_x;
    int view_y;
    int view_w;
    int view_h;
    gui_view_rect(&view_x, &view_y, &view_w, &view_h);
    int view_label_y = view_y - 14;
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
    fb_draw_text(tgt, (uint16_t)(g.win_x + 16), (uint16_t)view_label_y,
                 g.loading ? "Response: Fetching..." : view_label, 0xFFFFFFFF, win_bg);
    int cols = (view_w - 12) / 8; // leave scrollbar space
    int rows = gui_rows_for_view_h(view_h);
    if (cols < 10) cols = 10;

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
                img_glyph_assign_named(0xE000u, g.gallery_pixels, (uint16_t)br.w, (uint16_t)br.h, "sample.bmp");
                img_glyph_write_lardd();
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
                fb_draw_text(tgt, (uint16_t)view_x, (uint16_t)cap_y, "glyph U+E000 = ", 0xFFFFFFFF, win_bg);
                uint16_t gw, gh;
                int gx = view_x + 16 * 8;
                int gy = cap_y;
                int hover = in_rect(g.mx, g.my, gx, gy, IMG_GLYPH_SIZE, IMG_GLYPH_SIZE);
                if (img_glyph_render(0xE000u, g.glyph_tick, hover, g.glyph_render_pixels, &gw, &gh)) {
                    fb_draw_image(tgt, (uint16_t)gx, (uint16_t)gy, g.glyph_render_pixels, gw, gh);
                    gui_glyph_register_hit(gx, gy, gw, gh, 0xE000u);
                }
                fb_draw_text(tgt, (uint16_t)view_x, (uint16_t)(cap_y + 12), "Click glyph. LSH: glyph live U+E000 on | glyph list", 0xFFCFE3FFu, win_bg);
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
                uint16_t gw, gh;
                int gx = rx + col * 8;
                int gy = ry + row * 10;
                int hover = on_screen && in_rect(g.mx, g.my, gx, gy, IMG_GLYPH_SIZE, IMG_GLYPH_SIZE);
                if (img_glyph_render(cp, g.glyph_tick, hover, g.glyph_render_pixels, &gw, &gh)) {
                    if (on_screen) {
                        fb_draw_image(tgt, (uint16_t)gx, (uint16_t)gy, g.glyph_render_pixels, gw, gh);
                        gui_glyph_register_hit(gx, gy, gw, gh, cp);
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
    if (g.glyph_hover_cp || g.glyph_last_cp) {
        char cp_label[8];
        uint32_t cp = g.glyph_hover_cp ? g.glyph_hover_cp : g.glyph_last_cp;
        gui_cp_label(cp, cp_label, sizeof(cp_label));
        fb_draw_text(tgt, (uint16_t)(g.win_x + 112), (uint16_t)view_label_y,
                     g.glyph_hover_cp ? "Glyph hover " : "Glyph click ", 0xFFCFE3FFu, win_bg);
        fb_draw_text(tgt, (uint16_t)(g.win_x + 208), (uint16_t)view_label_y,
                     cp_label, 0xFFFFD166u, win_bg);
    }
    g.resp_total_lines = line + 1;

    gui_clamp_scroll_for_rows(rows);

    // Scrollbar
    int sb_w = 12;
    int sb_x = view_x + view_w - sb_w;
    int sb_y = view_y;
    int sb_h = view_h;
    int track_y;
    int track_h;
    uint32_t sb_bg = 0xFF111619;
    uint32_t sb_track = 0xFF22323A;
    uint32_t sb_bd = 0xFF60717C;
    uint32_t sb_th = 0xFF8FF3EA;
    uint32_t sb_disabled = 0xFF35434Au;
    uint32_t sb_arrow = 0xFFE0FFFF;
    int max_scroll;
    int thumb_h;
    int thumb_y;
    gui_scrollbar_track_rect(sb_y, sb_h, &track_y, &track_h);
    gui_scrollbar_metrics(track_y, track_h, rows, &max_scroll, &thumb_y, &thumb_h);
    fb_fill_rect(tgt, (uint16_t)sb_x, (uint16_t)sb_y, (uint16_t)sb_w, (uint16_t)sb_h, sb_bg);
    fb_fill_rect(tgt, (uint16_t)sb_x, (uint16_t)sb_y, 1, (uint16_t)sb_h, sb_bd);
    fb_fill_rect(tgt, (uint16_t)(sb_x + sb_w - 1), (uint16_t)sb_y, 1, (uint16_t)sb_h, sb_bd);
    fb_fill_rect(tgt, (uint16_t)sb_x, (uint16_t)sb_y, (uint16_t)sb_w, 1, sb_bd);
    fb_fill_rect(tgt, (uint16_t)sb_x, (uint16_t)(sb_y + sb_h - 1), (uint16_t)sb_w, 1, sb_bd);
    if (max_scroll > 0) {
        if (track_y > sb_y) {
            fb_fill_rect(tgt, (uint16_t)(sb_x + 1), (uint16_t)(sb_y + 1), (uint16_t)(sb_w - 2), (uint16_t)(track_y - sb_y - 1), 0xFF30434Bu);
            fb_fill_rect(tgt, (uint16_t)(sb_x + 1), (uint16_t)(track_y + track_h), (uint16_t)(sb_w - 2), (uint16_t)(sb_y + sb_h - track_y - track_h - 1), 0xFF30434Bu);
            gui_draw_scroll_arrow(tgt, sb_x + sb_w / 2, sb_y + 8, -1, sb_arrow);
            gui_draw_scroll_arrow(tgt, sb_x + sb_w / 2, sb_y + sb_h - 9, 1, sb_arrow);
        }
        fb_fill_rect(tgt, (uint16_t)(sb_x + 2), (uint16_t)track_y, (uint16_t)(sb_w - 4), (uint16_t)track_h, sb_track);
        fb_fill_rect(tgt, (uint16_t)(sb_x + 1), (uint16_t)thumb_y, (uint16_t)(sb_w - 2), (uint16_t)thumb_h, sb_th);
        if (thumb_h > 18) {
            int gy = thumb_y + thumb_h / 2 - 3;
            fb_fill_rect(tgt, (uint16_t)(sb_x + 3), (uint16_t)gy, (uint16_t)(sb_w - 6), 1, 0xFF1E6D70u);
            fb_fill_rect(tgt, (uint16_t)(sb_x + 3), (uint16_t)(gy + 3), (uint16_t)(sb_w - 6), 1, 0xFF1E6D70u);
        }
    } else if (sb_h > 6) {
        int rail_x = sb_x + sb_w / 2 - 1;
        fb_fill_rect(tgt, (uint16_t)rail_x, (uint16_t)(sb_y + 3), 3, (uint16_t)(sb_h - 6), sb_disabled);
        if (sb_h > 16) {
            fb_fill_rect(tgt, (uint16_t)(rail_x + 1), (uint16_t)(sb_y + 8), 1, (uint16_t)(sb_h - 16), 0xFF53636Au);
        }
    }

    guioverlay_state_t overlay = {
        (uint32_t)g.win_x, (uint32_t)g.win_y, (uint32_t)g.win_w, (uint32_t)g.win_h,
        (uint32_t)g.mx, (uint32_t)g.my, (uint32_t)g.app_id, (uint32_t)g.settings_open,
        (uint32_t)g.btn_pressed, (uint32_t)g.tb_focused, (uint32_t)g.loading,
        (uint32_t)g.http_post_mode, (uint32_t)g.user_sandbox, (uint32_t)g.fullscreen,
    };
    guioverlay_draw(&overlay);

    /* Settings overlay */
    if (g.settings_open) {
        int panel_x;
        int panel_y;
        int panel_w;
        int panel_h;
        gui_settings_panel_rect(&panel_x, &panel_y, &panel_w, &panel_h);
        uint32_t panel_bg = 0xFF20252B;
        fb_fill_rect(tgt, (uint16_t)panel_x, (uint16_t)panel_y, (uint16_t)panel_w, (uint16_t)panel_h, panel_bg);
        fb_fill_rect(tgt, (uint16_t)panel_x, (uint16_t)panel_y, (uint16_t)panel_w, 1, 0xFFFFB84D);
        fb_fill_rect(tgt, (uint16_t)panel_x, (uint16_t)(panel_y + panel_h - 1), (uint16_t)panel_w, 1, 0xFF0B0D10);
        int track_x = panel_x + 100;
        int track_w = panel_w - 106;
        int row_h = 32;
        if (track_w < 24) track_w = 24;
        fb_draw_text(tgt, (uint16_t)(panel_x + 8), (uint16_t)(panel_y + 8), "Brightness", 0xFFFFFFFF, panel_bg);
        fb_fill_rect(tgt, (uint16_t)track_x, (uint16_t)(panel_y + 12), (uint16_t)track_w, 8, 0xFF111619);
        int br_pos = (g.brightness - 50) * track_w / 100;
        if (br_pos < 0) br_pos = 0;
        if (br_pos > track_w - 6) br_pos = track_w - 6;
        fb_fill_rect(tgt, (uint16_t)(track_x + br_pos), (uint16_t)(panel_y + 10), 6, 12, 0xFF7BE0D6);
        fb_draw_text(tgt, (uint16_t)(panel_x + 8), (uint16_t)(panel_y + 8 + row_h), "Volume", 0xFFFFFFFF, panel_bg);
        fb_fill_rect(tgt, (uint16_t)track_x, (uint16_t)(panel_y + 12 + row_h), (uint16_t)track_w, 8, 0xFF111619);
        int vol_pos = g.volume * track_w / 100;
        if (vol_pos > track_w - 6) vol_pos = track_w - 6;
        fb_fill_rect(tgt, (uint16_t)(track_x + vol_pos), (uint16_t)(panel_y + 10 + row_h), 6, 12, 0xFF7BE0D6);
        fb_draw_text(tgt, (uint16_t)(panel_x + 8), (uint16_t)(panel_y + 8 + row_h * 2), "Quality", 0xFFFFFFFF, panel_bg);
        fb_fill_rect(tgt, (uint16_t)track_x, (uint16_t)(panel_y + 12 + row_h * 2), (uint16_t)track_w, 8, 0xFF111619);
        int q_pos = g.quality * track_w / 2;
        if (q_pos > track_w - 6) q_pos = track_w - 6;
        fb_fill_rect(tgt, (uint16_t)(track_x + q_pos), (uint16_t)(panel_y + 10 + row_h * 2), 6, 12, 0xFF7BE0D6);
    }

    lassist_draw((uint32_t)g.app_id, (uint32_t)g.mx, (uint32_t)g.my,
                 (uint32_t)g.win_x, (uint32_t)g.win_y, (uint32_t)g.win_w, (uint32_t)g.win_h);

    screenram_flush_to_target(tgt);
    // Cursor last: its hotspot may reach the bottom/right edge while the art clips past it.
    gui_draw_cursor_at(g.mx, g.my, 0xFFFFFFFF);

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
