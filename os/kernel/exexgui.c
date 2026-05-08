#include "exexgui.h"

#include "gui.h"
#include "lsh.h"
#include "version.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t enabled;
    exexgui_pane_t focus;
    uint32_t last_error;
} exexgui_state_t;

static exexgui_state_t s_exexgui;

static int streq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb)
{
    if (w == 0 || h == 0) return;
    gui_syscall_fill_rect((uint16_t)x, (uint16_t)y,
                          (uint16_t)min_u32(w, 65535u),
                          (uint16_t)min_u32(h, 65535u), argb);
}

static void pixel(uint32_t x, uint32_t y, uint32_t argb)
{
    gui_syscall_put_pixel((uint16_t)x, (uint16_t)y, argb);
}

static void text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg)
{
    gui_syscall_draw_text((uint16_t)x, (uint16_t)y, s, fg, bg);
}

static void frame(exexgui_rect_t r, uint32_t c)
{
    if (r.w < 2u || r.h < 2u) return;
    fill(r.x, r.y, r.w, 1u, c);
    fill(r.x, r.y + r.h - 1u, r.w, 1u, c);
    fill(r.x, r.y, 1u, r.h, c);
    fill(r.x + r.w - 1u, r.y, 1u, r.h, c);
}

static void focus_frame(exexgui_rect_t r)
{
    uint32_t c = 0xFF79E6D0u;
    frame(r, c);
    if (r.w > 4u && r.h > 4u) {
        exexgui_rect_t inner = { r.x + 2u, r.y + 2u, r.w - 4u, r.h - 4u };
        frame(inner, c);
    }
}

static void line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t c)
{
    int32_t dx = (int32_t)(x1 > x0 ? x1 - x0 : x0 - x1);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -(int32_t)(y1 > y0 ? y1 - y0 : y0 - y1);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    int32_t x = (int32_t)x0;
    int32_t y = (int32_t)y0;
    for (;;) {
        if (x >= 0 && y >= 0) {
            pixel((uint32_t)x, (uint32_t)y, c);
            pixel((uint32_t)x + 1u, (uint32_t)y, c);
        }
        if (x == (int32_t)x1 && y == (int32_t)y1) break;
        int32_t e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static void arrow(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    line(x0, y0, x1, y1, 0xFF0B0B0Bu);
    if (x1 <= x0) {
        line(x1, y1, x1 + 28u, y1 + 6u, 0xFF0B0B0Bu);
        line(x1, y1, x1 - 6u, y1 + 28u, 0xFF0B0B0Bu);
    } else {
        line(x1, y1, x1 - 28u, y1 + 6u, 0xFF0B0B0Bu);
        line(x1, y1, x1 + 6u, y1 + 28u, 0xFF0B0B0Bu);
    }
}

void exexgui_init(void)
{
    s_exexgui.enabled = 0;
    s_exexgui.focus = EXEXGUI_PANE_GUI;
    s_exexgui.last_error = 0;
}

int exexgui_enable(int on)
{
    s_exexgui.enabled = on ? 1u : 0u;
    s_exexgui.last_error = 0;
    return 0;
}

int exexgui_is_enabled(void)
{
    return s_exexgui.enabled ? 1 : 0;
}

const char* exexgui_focus_name(uint32_t focus)
{
    if (focus == EXEXGUI_PANE_TERM) return "term";
    if (focus == EXEXGUI_PANE_INFO) return "info";
    return "gui";
}

int exexgui_set_focus(const char* name)
{
    if (streq(name, "gui") || streq(name, "de") || streq(name, "wm")) {
        s_exexgui.focus = EXEXGUI_PANE_GUI;
    } else if (streq(name, "term") || streq(name, "terminal") || streq(name, "shell")) {
        s_exexgui.focus = EXEXGUI_PANE_TERM;
    } else if (streq(name, "info") || streq(name, "status")) {
        s_exexgui.focus = EXEXGUI_PANE_INFO;
    } else {
        s_exexgui.last_error = 1;
        return -1;
    }
    s_exexgui.enabled = 1;
    s_exexgui.last_error = 0;
    return 0;
}

void exexgui_focus_next(void)
{
    s_exexgui.focus = (exexgui_pane_t)(((uint32_t)s_exexgui.focus + 1u) % 3u);
    s_exexgui.enabled = 1;
}

int exexgui_layout_for(uint32_t screen_w, uint32_t screen_h, exexgui_layout_t* out)
{
    uint32_t margin;
    uint32_t border;
    uint32_t inner_w;
    uint32_t inner_h;
    uint32_t split_w;
    uint32_t term_h;

    if (!out || screen_w < 320u || screen_h < 200u) return -1;
    margin = screen_w >= 800u ? 18u : 8u;
    border = screen_w >= 640u ? 4u : 2u;
    if (screen_w <= margin * 2u + border || screen_h <= margin * 2u + border) return -2;

    inner_w = screen_w - margin * 2u;
    inner_h = screen_h - margin * 2u;
    split_w = (inner_w * 48u) / 100u;
    if (split_w < 260u) split_w = 260u;
    if (split_w + border + 160u > inner_w) split_w = inner_w > border + 160u ? inner_w - border - 160u : inner_w / 2u;
    term_h = (inner_h * 52u) / 100u;
    term_h = max_u32(term_h, 84u);
    if (term_h + border + 72u > inner_h) term_h = inner_h > border + 72u ? inner_h - border - 72u : inner_h / 2u;

    out->outer.x = margin;
    out->outer.y = margin;
    out->outer.w = inner_w;
    out->outer.h = inner_h;
    out->border = border;

    out->gui.x = margin + border;
    out->gui.y = margin + border;
    out->gui.w = split_w > border ? split_w - border : split_w;
    out->gui.h = inner_h > border * 2u ? inner_h - border * 2u : inner_h;

    out->term.x = margin + split_w + border;
    out->term.y = margin + border;
    out->term.w = inner_w > split_w + border * 2u ? inner_w - split_w - border * 2u : 1u;
    out->term.h = term_h > border ? term_h - border : term_h;

    out->info.x = out->term.x;
    out->info.y = margin + term_h + border;
    out->info.w = out->term.w;
    out->info.h = inner_h > term_h + border * 2u ? inner_h - term_h - border * 2u : 1u;
    return 0;
}

void exexgui_info(exexgui_info_t* out)
{
    if (!out) return;
    out->enabled = s_exexgui.enabled;
    out->focus = (uint32_t)s_exexgui.focus;
    out->last_error = s_exexgui.last_error;
    if (exexgui_layout_for(gui_syscall_get_width(), gui_syscall_get_height(), &out->layout) != 0) {
        out->layout.outer.x = out->layout.outer.y = out->layout.outer.w = out->layout.outer.h = 0;
        out->layout.gui = out->layout.outer;
        out->layout.term = out->layout.outer;
        out->layout.info = out->layout.outer;
        out->layout.border = 0;
    }
}

static void draw_terminal_tail(exexgui_rect_t r)
{
    const char* out = lsh_get_output();
    uint32_t max_lines = r.h > 42u ? (r.h - 36u) / 10u : 1u;
    uint32_t total = 1u;
    uint32_t skip = 0;
    uint32_t line_no = 0;
    uint32_t col = 0;
    uint32_t y;

    for (uint32_t i = 0; out && out[i]; i++) {
        if (out[i] == '\n') total++;
    }
    if (total > max_lines) skip = total - max_lines;
    y = r.y + 28u;
    for (uint32_t i = 0; out && out[i] && y + 8u < r.y + r.h; i++) {
        char ch = out[i];
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (line_no >= skip) y += 10u;
            line_no++;
            col = 0;
            continue;
        }
        if (line_no < skip) continue;
        if (col < (r.w > 24u ? (r.w - 24u) / 8u : 1u)) {
            char s[2];
            s[0] = (ch < 32 || ch > 126) ? '?' : ch;
            s[1] = '\0';
            text(r.x + 12u + col * 8u, y, s, 0xFFD7FBE8u, 0xFF08110Eu);
        }
        col++;
    }
}

static void draw_status(exexgui_rect_t r, const exexgui_layout_t* layout)
{
    uint32_t bg = 0xFFF7F7F2u;
    text(r.x + 14u, r.y + 24u, "INFO STATUS", 0xFF111111u, bg);
    text(r.x + 14u, r.y + 48u, "exexgui sketch layout", 0xFF111111u, bg);
    text(r.x + 14u, r.y + 68u, "left: GUI DE/WM", 0xFF111111u, bg);
    text(r.x + 14u, r.y + 88u, "top: terminal", 0xFF111111u, bg);
    text(r.x + 14u, r.y + 108u, "bottom: status", 0xFF111111u, bg);
    text(r.x + 14u, r.y + 132u, "focus:", 0xFF111111u, bg);
    text(r.x + 72u, r.y + 132u, exexgui_focus_name((uint32_t)s_exexgui.focus), 0xFF111111u, bg);
    text(r.x + 14u, r.y + 152u, LARDOS_VERSION, 0xFF111111u, bg);
    if (layout) {
        uint32_t bar_w = r.w > 42u ? r.w - 28u : 8u;
        fill(r.x + 14u, r.y + r.h - 22u, bar_w, 8u, 0xFFB7D7FFu);
    }
}

void exexgui_draw_desktop(void)
{
    exexgui_layout_t l;
    uint32_t w = gui_syscall_get_width();
    uint32_t h = gui_syscall_get_height();
    if (!s_exexgui.enabled || exexgui_layout_for(w, h, &l) != 0) return;

    fill(0, 0, w, h, 0xFFEDEDE8u);
    fill(l.outer.x, l.outer.y, l.outer.w, l.outer.h, 0xFFFFFFFFu);
    fill(l.gui.x + l.gui.w, l.outer.y, l.border, l.outer.h, 0xFF050505u);
    fill(l.term.x - l.border, l.info.y - l.border, l.term.w + l.border * 2u, l.border, 0xFF050505u);
    frame(l.outer, 0xFF050505u);

    fill(l.gui.x + 6u, l.gui.y + 6u, l.gui.w > 12u ? l.gui.w - 12u : 1u, 22u, 0xFFF7F7F2u);
    text(l.gui.x + 14u, l.gui.y + 13u, "GUI DE/WM", 0xFF111111u, 0xFFF7F7F2u);
    arrow(l.gui.x + 90u, l.gui.y + 88u, l.gui.x + 42u, l.gui.y + 20u);

    fill(l.term.x, l.term.y, l.term.w, l.term.h, 0xFF08110Eu);
    fill(l.term.x, l.term.y, l.term.w, 22u, 0xFF102A22u);
    text(l.term.x + 12u, l.term.y + 8u, "TERMINAL", 0xFFD7FBE8u, 0xFF102A22u);
    arrow(l.term.x + 90u, l.term.y + 78u, l.term.x + 42u, l.term.y + 24u);

    fill(l.info.x, l.info.y, l.info.w, l.info.h, 0xFFF7F7F2u);
    arrow(l.info.x + 76u, l.info.y + 104u, l.info.x + 10u, l.info.y + l.info.h - 18u);
}

void exexgui_draw_overlay(void)
{
    exexgui_layout_t l;
    uint32_t w = gui_syscall_get_width();
    uint32_t h = gui_syscall_get_height();
    if (!s_exexgui.enabled || exexgui_layout_for(w, h, &l) != 0) return;

    fill(l.gui.x + l.gui.w, l.outer.y, l.border, l.outer.h, 0xFF050505u);
    fill(l.term.x - l.border, l.info.y - l.border, l.term.w + l.border * 2u, l.border, 0xFF050505u);
    frame(l.outer, 0xFF050505u);
    frame(l.gui, 0xFF1D1D1Du);
    frame(l.term, 0xFF1D1D1Du);
    frame(l.info, 0xFF1D1D1Du);
    draw_terminal_tail(l.term);
    draw_status(l.info, &l);

    if (s_exexgui.focus == EXEXGUI_PANE_TERM) focus_frame(l.term);
    else if (s_exexgui.focus == EXEXGUI_PANE_INFO) focus_frame(l.info);
    else focus_frame(l.gui);
}

int exexgui_selftest(void)
{
    exexgui_state_t saved = s_exexgui;
    exexgui_layout_t l;
    exexgui_info_t info;
    exexgui_init();
    if (exexgui_enable(1) != 0) {
        s_exexgui = saved;
        return -1;
    }
    if (exexgui_layout_for(1024u, 768u, &l) != 0) {
        s_exexgui = saved;
        return -2;
    }
    if (l.gui.w < 260u || l.term.w < 160u || l.info.h < 72u) {
        s_exexgui = saved;
        return -3;
    }
    if (l.gui.x + l.gui.w >= l.term.x || l.term.y + l.term.h >= l.info.y) {
        s_exexgui = saved;
        return -4;
    }
    exexgui_focus_next();
    if (s_exexgui.focus != EXEXGUI_PANE_TERM) {
        s_exexgui = saved;
        return -5;
    }
    if (exexgui_set_focus("info") != 0 || s_exexgui.focus != EXEXGUI_PANE_INFO) {
        s_exexgui = saved;
        return -6;
    }
    if (exexgui_set_focus("bad") == 0) {
        s_exexgui = saved;
        return -7;
    }
    exexgui_info(&info);
    if (!info.enabled || info.last_error == 0) {
        s_exexgui = saved;
        return -8;
    }
    s_exexgui = saved;
    return 0;
}
