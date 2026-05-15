#include "guioverlay.h"

#include "gui.h"
#include "lguilib.h"
#include "rxe.h"
#include "sysrxe.h"
#include "version.h"

#include <stdint.h>

#define TITLE_BTN_INSET 2u
#define TITLE_BTN_SIZE 14u
#define TITLE_BTN_GAP 4u
#define TITLE_SET_W 36u

static const char* const s_app_titles[] = {
    "Doc Browser", "Calculator", "Notes", "Gallery", "LAR Package",
    "User Run", "Shrine", "Lard Shell", "Play", "Editor",
};

static const char* const s_app_hints[] = {
    "local LARS, HTTP GET/POST/HEAD", "integer scratchpad", "RAM notes",
    "images and scenes", "native archive tools", "sandboxable user task",
    "LSS programs", "system commands", "KR BASIC", "Lafaelo text",
};

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb)
{
    if (w == 0 || h == 0) return;
    gui_syscall_fill_rect((uint16_t)x, (uint16_t)y,
                          (uint16_t)min_u32(w, 65535u),
                          (uint16_t)min_u32(h, 65535u), argb);
}

static void text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg)
{
    gui_syscall_draw_text((uint16_t)x, (uint16_t)y, s, fg, bg);
}

static void frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    if (w < 2u || h < 2u) return;
    fill(x, y, w, 1u, c);
    fill(x, y + h - 1u, w, 1u, c);
    fill(x, y, 1u, h, c);
    fill(x + w - 1u, y, 1u, h, c);
}

static int in_rect(uint32_t x, uint32_t y, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static const lguilib_theme_t* overlay_theme(void)
{
    return lguilib_active_theme();
}

static const char* app_title(uint32_t app)
{
    const sysrxe_app_t* sx = sysrxe_get_by_app((int)app);
    const rxe_app_t* rx = rxe_get_by_app((int)app);
    if (sx) return sx->name;
    if (rx) return rx->name;
    if (app >= sizeof(s_app_titles) / sizeof(s_app_titles[0])) return "LardOS";
    return s_app_titles[app];
}

static const char* app_hint(uint32_t app)
{
    const sysrxe_app_t* sx = sysrxe_get_by_app((int)app);
    const rxe_app_t* rx = rxe_get_by_app((int)app);
    if (sx) return sx->file;
    if (rx) return rx->file;
    if (app >= sizeof(s_app_hints) / sizeof(s_app_hints[0])) return "ready";
    return s_app_hints[app];
}

static uint32_t solid(uint32_t c)
{
    return (c & 0x00FFFFFFu) | 0xFF000000u;
}

static uint32_t dim(uint32_t c, uint32_t div)
{
    uint32_t r;
    uint32_t g;
    uint32_t b;
    if (div == 0u) div = 1u;
    c = solid(c);
    r = ((c >> 16) & 0xFFu) / div;
    g = ((c >> 8) & 0xFFu) / div;
    b = (c & 0xFFu) / div;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static uint32_t app_accent(uint32_t app)
{
    const sysrxe_app_t* sx = sysrxe_get_by_app((int)app);
    const rxe_app_t* rx = rxe_get_by_app((int)app);
    if (sx) return solid(sx->color);
    if (rx) return solid(rx->color);
    switch (app) {
    case 0: return 0xFF2E8FBAu;
    case 1: return 0xFF48A9A6u;
    case 2: return 0xFFE3A447u;
    case 3: return 0xFFC86DD7u;
    case 4: return 0xFF6F8BDCu;
    case 5: return 0xFFB88746u;
    case 6: return 0xFF7AC86Du;
    case 7: return 0xFF3AA66Fu;
    case 8: return 0xFF8BC34Au;
    case 9: return 0xFFE06A6Au;
    default: return 0xFF57B8A6u;
    }
}

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int eq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static const char* layout_surface(const char* layout, const char* fallback)
{
    if (!layout || !layout[0] || eq_ci(layout, "auto")) return fallback;
    if (eq_ci(layout, "document") || eq_ci(layout, "doc")) return "DOCUMENT";
    if (eq_ci(layout, "terminal") || eq_ci(layout, "shell")) return "TERMINAL";
    if (eq_ci(layout, "note") || eq_ci(layout, "notes")) return "NOTES";
    if (eq_ci(layout, "gallery") || eq_ci(layout, "image")) return "GALLERY";
    if (eq_ci(layout, "package") || eq_ci(layout, "pak")) return "PACKAGE";
    if (eq_ci(layout, "game")) return "GAME";
    if (eq_ci(layout, "editor") || eq_ci(layout, "edit")) return "EDITOR";
    if (eq_ci(layout, "system") || eq_ci(layout, "sys")) return "SYSRXE";
    if (eq_ci(layout, "exec") || eq_ci(layout, "panel") || eq_ci(layout, "tool")) return "RXE EXEC";
    return fallback;
}

static const char* app_surface(uint32_t app)
{
    const sysrxe_app_t* sx = sysrxe_get_by_app((int)app);
    const rxe_app_t* rx = rxe_get_by_app((int)app);
    if (sx && sx->type == SYSRXE_TYPE_GAME) return layout_surface(sx->layout, "SYSRXE GAME");
    if (rx && rx->type == SYSRXE_TYPE_GAME) return layout_surface(rx->layout, "RXE GAME");
    if (sx) return layout_surface(sx->layout, "SYSRXE");
    if (rx) return layout_surface(rx->layout, "RXE EXEC");
    switch (app) {
    case 0: return "DOCUMENT";
    case 2: return "NOTES";
    case 3: return "GALLERY";
    case 4: return "PACKAGE";
    case 6: return "SYSTEM";
    case 7: return "TERMINAL";
    case 8: return "PLAY";
    case 9: return "EDITOR";
    default: return "TOOL";
    }
}

static int overlay_layout_ok(const guioverlay_state_t* s)
{
    if (!s) return 0;
    if (s->win_w < 160u || s->win_h < 240u) return 0;
    if (s->win_w <= 32u || s->win_h <= 190u) return 0;
    return 1;
}

static void draw_shadow(const guioverlay_state_t* s)
{
    const lguilib_theme_t* th = overlay_theme();
    uint32_t sw = gui_syscall_get_width();
    uint32_t sh = gui_syscall_get_height();
    uint32_t x = s->win_x;
    uint32_t y = s->win_y;
    uint32_t w = s->win_w;
    uint32_t h = s->win_h;
    if (x + w + 7u < sw) {
        fill(x + w, y + 7u, 3u, h, th->shadow);
        fill(x + w + 3u, y + 12u, 4u, h > 8u ? h - 8u : h, 0x22000000u);
    }
    if (y + h + 7u < sh) {
        fill(x + 7u, y + h, w, 3u, th->shadow);
        fill(x + 12u, y + h + 3u, w > 8u ? w - 8u : w, 4u, 0x22000000u);
    }
}

static void draw_title(const guioverlay_state_t* s)
{
    const lguilib_theme_t* th = overlay_theme();
    uint32_t title_bg = th->title_bg;
    uint32_t btn_y = s->win_y + TITLE_BTN_INSET;
    uint32_t close_x = s->win_x + s->win_w - TITLE_BTN_GAP - TITLE_BTN_SIZE;
    uint32_t full_x = close_x - TITLE_BTN_GAP - TITLE_BTN_SIZE;
    uint32_t min_x = full_x - TITLE_BTN_GAP - TITLE_BTN_SIZE;
    uint32_t set_x = min_x - TITLE_BTN_GAP - TITLE_SET_W;
    uint32_t min_set_x = s->win_x + TITLE_BTN_GAP;
    if (set_x < min_set_x) set_x = min_set_x;
    fill(s->win_x, s->win_y, s->win_w, 20u, title_bg);
    fill(s->win_x, s->win_y + 19u, s->win_w, 1u, th->tab_accent);
    fill(s->win_x + 1u, s->win_y + 1u, s->win_w > 2u ? s->win_w - 2u : 1u, 1u, 0xFF334048u);
    if (s->win_w > 120u) {
        uint32_t set_bg = s->settings_open ? th->tab_active : th->tab_idle;
        fill(set_x, btn_y, TITLE_SET_W, TITLE_BTN_SIZE, set_bg);
        text(set_x + 6u, s->win_y + 6u, "Set", th->title_fg, set_bg);
        fill(min_x, btn_y, TITLE_BTN_SIZE, TITLE_BTN_SIZE, th->tab_idle);
        fill(min_x + 3u, s->win_y + 12u, 8u, 1u, th->title_fg);
        fill(full_x, btn_y, TITLE_BTN_SIZE, TITLE_BTN_SIZE, th->tab_idle);
        if (s->fullscreen) {
            fill(full_x + 4u, s->win_y + 5u, 6u, 1u, th->title_fg);
            fill(full_x + 4u, s->win_y + 5u, 1u, 5u, th->title_fg);
            fill(full_x + 7u, s->win_y + 8u, 5u, 1u, th->title_fg);
            fill(full_x + 11u, s->win_y + 8u, 1u, 5u, th->title_fg);
            fill(full_x + 7u, s->win_y + 12u, 5u, 1u, th->title_fg);
        } else {
            fill(full_x + 4u, s->win_y + 5u, 7u, 1u, th->title_fg);
            fill(full_x + 4u, s->win_y + 5u, 1u, 7u, th->title_fg);
            fill(full_x + 10u, s->win_y + 5u, 1u, 7u, th->title_fg);
            fill(full_x + 4u, s->win_y + 11u, 7u, 1u, th->title_fg);
        }
        fill(close_x, btn_y, TITLE_BTN_SIZE, TITLE_BTN_SIZE, 0xFF803B45u);
        text(close_x + 4u, s->win_y + 6u, "x", th->title_fg, 0xFF803B45u);
    }
    if (s->win_w > 150u) text(s->win_x + 8u, s->win_y + 6u, app_title(s->app_id), th->title_fg, title_bg);
    if (s->win_w > 360u && s->win_x + 176u + 96u < set_x) {
        text(s->win_x + 176u, s->win_y + 6u, LARDOS_VERSION, th->title_accent, title_bg);
    }
    frame(s->win_x, s->win_y, s->win_w, s->win_h, th->border);
    if (s->win_w > 4u && s->win_h > 4u) frame(s->win_x + 1u, s->win_y + 1u, s->win_w - 2u, s->win_h - 2u, 0xFF384149u);
}

static void draw_content_badge(const guioverlay_state_t* s)
{
    const lguilib_theme_t* th = overlay_theme();
    uint32_t accent = app_accent(s->app_id);
    uint32_t bg = dim(accent, 5u);
    uint32_t y = s->win_y + 24u;
    fill(s->win_x + 8u, y + 2u, s->win_w > 16u ? s->win_w - 16u : s->win_w, 18u, bg);
    fill(s->win_x + 8u, y + 2u, 4u, 18u, accent);
    text(s->win_x + 16u, y + 8u, app_surface(s->app_id), th->title_fg, bg);
    if (s->win_w > 320u) text(s->win_x + 120u, y + 8u, app_title(s->app_id), th->title_fg, bg);
    if (s->win_w > 500u) text(s->win_x + 264u, y + 8u, app_hint(s->app_id), th->hint_fg, bg);
    if (s->win_w > 520u) text(s->win_x + s->win_w - 112u, y + 8u, LARDOS_VERSION, th->hint_fg, bg);
}

static void button_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int hover, int pressed)
{
    const lguilib_theme_t* th = overlay_theme();
    uint32_t c = hover ? th->button_hover : th->button_border;
    if (pressed && hover) c = th->title_fg;
    frame(x, y, w, h, c);
    if (w > 4u && h > 4u) fill(x + 2u, y + 2u, w - 4u, 2u, hover ? th->tab_accent : 0xFF2B4948u);
    if (hover && w > 4u && h > 4u) frame(x + 2u, y + 2u, w - 4u, h - 4u, th->button_inner);
}

static void draw_button_feedback(const guioverlay_state_t* s)
{
    uint32_t content_y = s->win_y + 24u;
    uint32_t btn_x = s->win_x + 16u;
    uint32_t btn_y = content_y + 36u;
    uint32_t btn_h = 28u;
    if (s->app_id == 0u) {
        uint32_t dx[] = { 0u, 52u, 120u, 176u, 236u };
        uint32_t ww[] = { 48u, 64u, 52u, 56u, 50u };
        for (uint32_t i = 0; i < 5u; i++) {
            uint32_t x = btn_x + dx[i];
            int hover = in_rect(s->mouse_x, s->mouse_y, x, btn_y, ww[i], btn_h);
            button_frame(x, btn_y, ww[i], btn_h, hover, s->button_pressed);
        }
    } else if (s->app_id == 9u) {
        for (uint32_t i = 0; i < 3u; i++) {
            uint32_t x = btn_x + i * 60u;
            int hover = in_rect(s->mouse_x, s->mouse_y, x, btn_y, 56u, btn_h);
            button_frame(x, btn_y, 56u, btn_h, hover, s->button_pressed);
        }
    } else {
        int hover = in_rect(s->mouse_x, s->mouse_y, btn_x, btn_y, 120u, btn_h);
        button_frame(btn_x, btn_y, 120u, btn_h, hover, s->button_pressed);
        if (s->app_id == 5u) {
            uint32_t sx = btn_x + 128u;
            int shover = in_rect(s->mouse_x, s->mouse_y, sx, btn_y, 70u, btn_h);
            frame(sx, btn_y, 70u, btn_h, shover ? 0xFFD5FFD5u : 0xFF203020u);
            if (s->user_sandbox && 70u > 4u) frame(sx + 2u, btn_y + 2u, 66u, btn_h - 4u, 0x8860FF60u);
        }
    }
}

static void draw_fields(const guioverlay_state_t* s)
{
    const lguilib_theme_t* th = overlay_theme();
    uint32_t content_y = s->win_y + 24u;
    uint32_t tb_x = s->win_x + 16u;
    uint32_t tb_y = content_y + 118u;
    uint32_t tb_w = 260u;
    uint32_t tb_h = 24u;
    uint32_t view_x = s->win_x + 16u;
    uint32_t view_y = tb_y + tb_h + 28u;
    uint32_t view_w = s->win_w > 32u ? s->win_w - 32u : 0u;
    uint32_t bottom = s->win_y + s->win_h;
    uint32_t view_h = bottom > view_y + 12u ? bottom - view_y - 12u : 64u;
    if (view_h < 64u) view_h = 64u;
    frame(tb_x, tb_y, tb_w, tb_h, s->textbox_focused ? th->tab_accent : th->output_frame);
    if (view_w > 4u && view_h > 4u) {
        frame(view_x - 2u, view_y - 2u, view_w + 4u, view_h + 4u, th->output_frame);
        fill(view_x - 2u, view_y - 2u, view_w + 4u, 2u, th->tab_accent);
    }
    if (s->loading && view_w > 96u) text(view_x + view_w - 88u, view_y - 12u, "FETCHING", th->hint_fg, th->panel_bg);
}

void guioverlay_draw(const guioverlay_state_t* state)
{
    if (!overlay_layout_ok(state)) return;
    draw_shadow(state);
    draw_title(state);
    draw_content_badge(state);
    draw_button_feedback(state);
    draw_fields(state);
}

int guioverlay_selftest(void)
{
    guioverlay_state_t s;
    s.win_x = 12u;
    s.win_y = 12u;
    s.win_w = 640u;
    s.win_h = 420u;
    s.mouse_x = 0u;
    s.mouse_y = 0u;
    s.app_id = 0u;
    s.settings_open = 0u;
    s.button_pressed = 0u;
    s.textbox_focused = 0u;
    s.loading = 0u;
    s.http_post_mode = 0u;
    s.user_sandbox = 0u;
    s.fullscreen = 0u;
    if (!overlay_layout_ok(&s)) return -1;
    s.win_w = 150u;
    if (overlay_layout_ok(&s)) return -2;
    s.win_w = 640u;
    s.win_h = 200u;
    if (overlay_layout_ok(&s)) return -3;
    if (lguilib_selftest() != 0) return -4;
    return 0;
}
