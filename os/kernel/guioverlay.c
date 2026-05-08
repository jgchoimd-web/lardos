#include "guioverlay.h"

#include "gui.h"
#include "version.h"

#include <stdint.h>

static const char* const s_tab_names[] = {
    "Doc", "Calc", "Note", "Pix", "Pak", "User", "LSS", "LSH", "Play", "Edit",
};

static const char* const s_tab_short[] = {
    "D", "C", "N", "P", "K", "U", "S", "H", "R", "E",
};

static const char* const s_app_titles[] = {
    "Doc Browser", "Calculator", "Notes", "Gallery", "LAR Package",
    "User Run", "Shrine", "Lard Shell", "Play", "Editor",
};

static const char* const s_app_hints[] = {
    "local LARS, HTTP GET/POST", "integer scratchpad", "RAM notes",
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

static const char* app_title(uint32_t app)
{
    if (app >= sizeof(s_app_titles) / sizeof(s_app_titles[0])) return "LardOS";
    return s_app_titles[app];
}

static const char* app_hint(uint32_t app)
{
    if (app >= sizeof(s_app_hints) / sizeof(s_app_hints[0])) return "ready";
    return s_app_hints[app];
}

static int overlay_layout_ok(const guioverlay_state_t* s)
{
    if (!s) return 0;
    if (s->win_w < 160u || s->win_h < 240u) return 0;
    if (s->win_w / 10u < 16u) return 0;
    if (s->win_w <= 32u || s->win_h <= 190u) return 0;
    return 1;
}

static void draw_shadow(const guioverlay_state_t* s)
{
    uint32_t sw = gui_syscall_get_width();
    uint32_t sh = gui_syscall_get_height();
    uint32_t x = s->win_x;
    uint32_t y = s->win_y;
    uint32_t w = s->win_w;
    uint32_t h = s->win_h;
    if (x + w + 5u < sw) fill(x + w, y + 5u, 5u, h, 0x33000000u);
    if (y + h + 5u < sh) fill(x + 5u, y + h, w, 5u, 0x33000000u);
}

static void draw_title(const guioverlay_state_t* s)
{
    uint32_t title_bg = 0xFF304060u;
    uint32_t set_x = s->win_x + s->win_w - 52u;
    if (s->win_w > 150u) text(s->win_x + 8u, s->win_y + 6u, "LardOS GUI", 0xFFFFFFFFu, title_bg);
    if (s->win_w > 300u && s->win_x + 112u + 96u < set_x) {
        text(s->win_x + 112u, s->win_y + 6u, app_title(s->app_id), 0xFFE7F0FFu, title_bg);
    }
    frame(s->win_x, s->win_y, s->win_w, s->win_h, 0xFF05070Cu);
}

static void draw_tabs(const guioverlay_state_t* s)
{
    uint32_t tab_y = s->win_y + 20u;
    uint32_t tab_h = 24u;
    uint32_t tab_w = s->win_w / 10u;
    if (tab_w == 0) return;
    for (uint32_t t = 0; t < 10u; t++) {
        uint32_t tx = s->win_x + t * tab_w;
        uint32_t tw = (t == 9u) ? (s->win_x + s->win_w - tx) : tab_w;
        int hover = in_rect(s->mouse_x, s->mouse_y, tx, tab_y, tw, tab_h);
        uint32_t tab_bg = (s->app_id == t) ? 0xFF3E5F82u : (hover ? 0xFF343A50u : 0xFF282838u);
        const char* label = tw >= 34u ? s_tab_names[t] : s_tab_short[t];
        uint32_t label_x = tx + (tw >= 34u ? 4u : (tw > 8u ? (tw - 8u) / 2u : 0u));
        fill(tx, tab_y, tw, tab_h, tab_bg);
        if (s->app_id == t) fill(tx, tab_y + tab_h - 3u, tw, 3u, 0xFF72D6FFu);
        text(label_x, tab_y + 8u, label, 0xFFFFFFFFu, tab_bg);
    }
}

static void draw_content_badge(const guioverlay_state_t* s)
{
    uint32_t bg = 0xFF202840u;
    uint32_t y = s->win_y + 44u;
    fill(s->win_x + 8u, y + 2u, s->win_w > 16u ? s->win_w - 16u : s->win_w, 18u, bg);
    text(s->win_x + 16u, y + 8u, app_title(s->app_id), 0xFFFFFFFFu, bg);
    if (s->win_w > 420u) text(s->win_x + 176u, y + 8u, app_hint(s->app_id), 0xFFAFC2D8u, bg);
    if (s->win_w > 520u) text(s->win_x + s->win_w - 112u, y + 8u, LARDOS_VERSION, 0xFFAFC2D8u, bg);
}

static void button_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int hover, int pressed)
{
    uint32_t c = hover ? 0xFFD5E5FFu : 0xFF203060u;
    if (pressed && hover) c = 0xFFFFFFFFu;
    frame(x, y, w, h, c);
    if (hover && w > 4u && h > 4u) frame(x + 2u, y + 2u, w - 4u, h - 4u, 0x6680A0FFu);
}

static void draw_button_feedback(const guioverlay_state_t* s)
{
    uint32_t content_y = s->win_y + 44u;
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
    uint32_t content_y = s->win_y + 44u;
    uint32_t tb_x = s->win_x + 16u;
    uint32_t tb_y = content_y + 118u;
    uint32_t tb_w = 260u;
    uint32_t view_x = s->win_x + 16u;
    uint32_t view_y = s->win_y + 180u;
    uint32_t view_w = s->win_w > 32u ? s->win_w - 32u : 0u;
    uint32_t view_h = s->win_h > 190u ? s->win_h - 190u : 0u;
    frame(tb_x, tb_y, tb_w, 24u, s->textbox_focused ? 0xFFFFFF00u : 0xFF5A6B86u);
    if (view_w > 4u && view_h > 4u) frame(view_x - 2u, view_y - 2u, view_w + 4u, view_h + 4u, 0xFF5A6B86u);
    if (s->loading && view_w > 96u) text(view_x + view_w - 88u, view_y - 12u, "FETCHING", 0xFFFFFF99u, 0xFF202840u);
}

void guioverlay_draw(const guioverlay_state_t* state)
{
    if (!overlay_layout_ok(state)) return;
    draw_shadow(state);
    draw_title(state);
    draw_tabs(state);
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
    if (!overlay_layout_ok(&s)) return -1;
    s.win_w = 150u;
    if (overlay_layout_ok(&s)) return -2;
    s.win_w = 640u;
    s.win_h = 200u;
    if (overlay_layout_ok(&s)) return -3;
    return 0;
}
