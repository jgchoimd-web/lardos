#include "exgui.h"

#include "gui.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t enabled;
    exgui_style_t style;
    exgui_layout_t layout;
    uint32_t focused;
    uint32_t tick;
    uint32_t last_error;
} exgui_state_t;

static exgui_state_t s_exgui;

static const char* const s_window_names[] = {
    "LardOS Legacy",
    "Files",
    "Terminal",
    "Settings",
};

static uint32_t window_count(void)
{
    return sizeof(s_window_names) / sizeof(s_window_names[0]);
}

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

static uint32_t clamp_x(uint32_t x)
{
    uint32_t w = gui_syscall_get_width();
    return x >= w && w > 0 ? w - 1u : x;
}

static uint32_t clamp_y(uint32_t y)
{
    uint32_t h = gui_syscall_get_height();
    return y >= h && h > 0 ? h - 1u : y;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb)
{
    if (w == 0 || h == 0) return;
    x = clamp_x(x);
    y = clamp_y(y);
    gui_syscall_fill_rect((uint16_t)x, (uint16_t)y,
                          (uint16_t)min_u32(w, 65535u),
                          (uint16_t)min_u32(h, 65535u), argb);
}

static void text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg)
{
    gui_syscall_draw_text((uint16_t)clamp_x(x), (uint16_t)clamp_y(y), s, fg, bg);
}

static void frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    if (w < 2u || h < 2u) return;
    fill(x, y, w, 1, c);
    fill(x, y + h - 1u, w, 1, c);
    fill(x, y, 1, h, c);
    fill(x + w - 1u, y, 1, h, c);
}

static uint32_t accent(void)
{
    if (s_exgui.style == EXGUI_STYLE_LINUX) return 0xFFFFB84Du;
    if (s_exgui.style == EXGUI_STYLE_MAC) return 0xFF2BA7A0u;
    return 0xFF3DB8A5u;
}

static uint32_t panel_color(void)
{
    if (s_exgui.style == EXGUI_STYLE_LINUX) return 0xFF242126u;
    if (s_exgui.style == EXGUI_STYLE_MAC) return 0xFFF0F5F5u;
    return 0xFF182126u;
}

static uint32_t panel_text(void)
{
    return s_exgui.style == EXGUI_STYLE_MAC ? 0xFF111827u : 0xFFFFFFFFu;
}

const char* exgui_style_name(uint32_t style)
{
    if (style == EXGUI_STYLE_LINUX) return "linux";
    if (style == EXGUI_STYLE_MAC) return "mac";
    return "win";
}

const char* exgui_layout_name(uint32_t layout)
{
    if (layout == EXGUI_LAYOUT_TILE) return "tile";
    if (layout == EXGUI_LAYOUT_STACK) return "stack";
    return "float";
}

void exgui_init(void)
{
    s_exgui.enabled = 0;
    s_exgui.style = EXGUI_STYLE_WIN;
    s_exgui.layout = EXGUI_LAYOUT_FLOAT;
    s_exgui.focused = 0;
    s_exgui.tick = 0;
    s_exgui.last_error = 0;
}

int exgui_enable(int on)
{
    s_exgui.enabled = on ? 1u : 0u;
    s_exgui.last_error = 0;
    return 0;
}

int exgui_set_style(const char* name)
{
    if (streq(name, "win") || streq(name, "windows")) s_exgui.style = EXGUI_STYLE_WIN;
    else if (streq(name, "linux") || streq(name, "gnome") || streq(name, "kde")) s_exgui.style = EXGUI_STYLE_LINUX;
    else if (streq(name, "mac") || streq(name, "macos")) s_exgui.style = EXGUI_STYLE_MAC;
    else {
        s_exgui.last_error = 1;
        return -1;
    }
    s_exgui.enabled = 1;
    s_exgui.last_error = 0;
    return 0;
}

int exgui_set_layout(const char* name)
{
    if (streq(name, "float") || streq(name, "floating")) s_exgui.layout = EXGUI_LAYOUT_FLOAT;
    else if (streq(name, "tile") || streq(name, "tiling")) s_exgui.layout = EXGUI_LAYOUT_TILE;
    else if (streq(name, "stack") || streq(name, "stacked")) s_exgui.layout = EXGUI_LAYOUT_STACK;
    else {
        s_exgui.last_error = 2;
        return -1;
    }
    s_exgui.enabled = 1;
    s_exgui.last_error = 0;
    return 0;
}

void exgui_focus_next(void)
{
    s_exgui.focused = (s_exgui.focused + 1u) % window_count();
    s_exgui.enabled = 1;
}

void exgui_info(exgui_info_t* out)
{
    if (!out) return;
    out->enabled = s_exgui.enabled;
    out->style = (uint32_t)s_exgui.style;
    out->layout = (uint32_t)s_exgui.layout;
    out->focused = s_exgui.focused;
    out->window_count = window_count();
    out->last_error = s_exgui.last_error;
}

static void draw_wallpaper(uint32_t w, uint32_t h)
{
    uint32_t base = s_exgui.style == EXGUI_STYLE_LINUX ? 0xFF1F1B24u :
                    s_exgui.style == EXGUI_STYLE_MAC ? 0xFFE7F0EDu : 0xFF11191Du;
    uint32_t stripe = s_exgui.style == EXGUI_STYLE_LINUX ? 0xFF2F2836u :
                      s_exgui.style == EXGUI_STYLE_MAC ? 0xFFD7E9E5u : 0xFF1C2A2Eu;
    uint32_t warm = s_exgui.style == EXGUI_STYLE_MAC ? 0xFFFFDFA6u : 0xFFFFB84Du;
    uint32_t cool = s_exgui.style == EXGUI_STYLE_LINUX ? 0xFF7BE0D6u : 0xFF2BA7A0u;
    fill(0, 0, w, h, base);
    for (uint32_t y = 0; y < h; y += 36u) {
        fill(0, y, w, 3u, stripe);
    }
    fill(0, 0, w, 4u, warm);
    fill(0, h > 6u ? h - 6u : 0u, w, 6u, cool);
    fill(w > 320u ? w - 320u : 0u, 48u, 210u, h > 210u ? h - 170u : 48u, cool);
    fill(w > 284u ? w - 284u : 0u, 66u, 152u, h > 250u ? h - 230u : 32u, base);
    fill(32u, h > 220u ? h - 178u : 72u, w > 420u ? 260u : 160u, 5u, warm);
}

static void draw_window_card(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t idx)
{
    uint32_t focused = idx == s_exgui.focused;
    uint32_t title = focused ? accent() : 0xFF30343Cu;
    uint32_t body = focused ? 0xFFF2F5F2u : 0xFFE0E5E4u;
    uint32_t fg = 0xFF111827u;
    fill(x + 5u, y + 6u, w, h, 0x33000000u);
    fill(x + 2u, y + 3u, w, h, 0x22000000u);
    fill(x, y, w, h, body);
    fill(x, y, w, 22u, title);
    fill(x, y + 22u, w, 3u, focused ? 0xFFFFB84Du : 0xFF88929Au);
    fill(x, y, 5u, h, focused ? 0xFFFFB84Du : 0xFF6B7280u);
    frame(x, y, w, h, focused ? 0xFFFFFFFFu : 0xFF6B7280u);
    if (s_exgui.style == EXGUI_STYLE_MAC) {
        fill(x + 8u, y + 7u, 6u, 6u, 0xFFFF5F56u);
        fill(x + 18u, y + 7u, 6u, 6u, 0xFFFFBD2Eu);
        fill(x + 28u, y + 7u, 6u, 6u, 0xFF27C93Fu);
        text(x + 44u, y + 7u, s_window_names[idx], fg, title);
    } else {
        text(x + 8u, y + 7u, s_window_names[idx], 0xFFFFFFFFu, title);
        fill(x + w - 48u, y + 6u, 10u, 10u, 0xFFE5E7EBu);
        fill(x + w - 28u, y + 6u, 10u, 10u, 0xFFE5E7EBu);
    }
    text(x + 12u, y + 38u, idx == 0 ? "classic gui surface" :
         idx == 1 ? "native files and packages" :
         idx == 2 ? "lsh shell workspace" : "desktop preferences", fg, body);
    fill(x + 12u, y + h - 28u, w > 32u ? w - 24u : 4u, 2u, 0xFFC5CECCu);
    fill(x + 12u, y + h - 22u, w > 32u ? w - 24u : 4u, 8u, focused ? accent() : 0xFF9CA3AFu);
}

static void draw_managed_windows(uint32_t sw, uint32_t sh)
{
    uint32_t top = s_exgui.style == EXGUI_STYLE_MAC || s_exgui.style == EXGUI_STYLE_LINUX ? 34u : 18u;
    uint32_t bottom = s_exgui.style == EXGUI_STYLE_MAC ? 70u : 48u;
    if (s_exgui.layout == EXGUI_LAYOUT_TILE) {
        uint32_t usable_w = sw > 260u ? sw - 96u : sw;
        uint32_t x = s_exgui.style == EXGUI_STYLE_LINUX ? 74u : 34u;
        uint32_t y = top + 36u;
        uint32_t cw = usable_w / 2u - 10u;
        uint32_t ch = sh > top + bottom + 90u ? (sh - top - bottom - 70u) / 2u : 80u;
        for (uint32_t i = 1; i < window_count(); i++) {
            draw_window_card(x + ((i - 1u) & 1u) * (cw + 20u), y + ((i - 1u) / 2u) * (ch + 20u), cw, ch, i);
        }
        return;
    }
    if (s_exgui.layout == EXGUI_LAYOUT_STACK) {
        uint32_t x = sw > 430u ? sw - 390u : 24u;
        uint32_t y = top + 44u;
        for (uint32_t i = 1; i < window_count(); i++) {
            draw_window_card(x + i * 18u, y + i * 28u, 320u, 116u, i);
        }
        return;
    }
    draw_window_card(38u, top + 42u, 220u, 112u, 1u);
    if (sw > 720u) draw_window_card(sw - 286u, top + 92u, 238u, 128u, 2u);
    if (sh > 520u) draw_window_card(72u, sh - bottom - 154u, 240u, 108u, 3u);
}

static void draw_win_shell(uint32_t w, uint32_t h)
{
    uint32_t bar_y = h > 42u ? h - 42u : 0u;
    fill(0, bar_y, w, 42u, panel_color());
    fill(0, bar_y, w, 2u, 0xFFFFB84Du);
    fill(12u, bar_y + 8u, 72u, 26u, accent());
    text(24u, bar_y + 17u, "Start", 0xFFFFFFFFu, accent());
    for (uint32_t i = 0; i < window_count(); i++) {
        uint32_t x = 104u + i * 112u;
        if (x + 96u >= w) break;
        fill(x, bar_y + 8u, 96u, 26u, i == s_exgui.focused ? 0xFF334155u : 0xFF1F2937u);
        text(x + 8u, bar_y + 17u, s_window_names[i], 0xFFFFFFFFu, i == s_exgui.focused ? 0xFF334155u : 0xFF1F2937u);
    }
    text(w > 116u ? w - 116u : 4u, bar_y + 17u, "LardOS", 0xFFFFFFFFu, panel_color());
}

static void draw_linux_shell(uint32_t w, uint32_t h)
{
    (void)h;
    fill(0, 0, w, 30u, panel_color());
    fill(0, 30u, w, 2u, accent());
    text(12u, 11u, "Activities", panel_text(), panel_color());
    text(w > 168u ? w - 168u : 120u, 11u, "LardOS Workspace", panel_text(), panel_color());
    fill(0, 30u, 56u, h > 30u ? h - 30u : 1u, 0xCC15151Au);
    for (uint32_t i = 0; i < window_count(); i++) {
        uint32_t y = 52u + i * 46u;
        fill(12u, y, 32u, 32u, i == s_exgui.focused ? accent() : 0xFF3F3F46u);
        text(21u, y + 12u, i == 0 ? "L" : i == 1 ? "F" : i == 2 ? "T" : "S", 0xFFFFFFFFu, i == s_exgui.focused ? accent() : 0xFF3F3F46u);
    }
}

static void draw_mac_shell(uint32_t w, uint32_t h)
{
    fill(0, 0, w, 28u, panel_color());
    fill(0, 27u, w, 1u, 0xFFB7C9C7u);
    text(12u, 10u, "LardOS", panel_text(), panel_color());
    text(84u, 10u, "File Edit View Window", panel_text(), panel_color());
    text(w > 98u ? w - 98u : 4u, 10u, "exgui", panel_text(), panel_color());
    uint32_t dock_w = min_u32(360u, w > 48u ? w - 48u : w);
    uint32_t dock_x = w > dock_w ? (w - dock_w) / 2u : 0u;
    uint32_t dock_y = h > 64u ? h - 58u : 0u;
    fill(dock_x, dock_y, dock_w, 46u, 0xCCF6F8FBu);
    frame(dock_x, dock_y, dock_w, 46u, 0xFFFFFFFFu);
    for (uint32_t i = 0; i < window_count(); i++) {
        uint32_t x = dock_x + 22u + i * 76u;
        if (x + 40u > dock_x + dock_w) break;
        fill(x, dock_y + 8u, 36u, 30u, i == s_exgui.focused ? accent() : 0xFFFFFFFFu);
        text(x + 12u, dock_y + 18u, i == 0 ? "L" : i == 1 ? "F" : i == 2 ? "T" : "S", 0xFF111827u,
             i == s_exgui.focused ? accent() : 0xFFFFFFFFu);
    }
}

void exgui_draw_desktop(void)
{
    uint32_t w = gui_syscall_get_width();
    uint32_t h = gui_syscall_get_height();
    if (!s_exgui.enabled || w < 160u || h < 120u) return;
    s_exgui.tick++;
    draw_wallpaper(w, h);
    draw_managed_windows(w, h);
}

void exgui_draw_overlay(void)
{
    uint32_t w = gui_syscall_get_width();
    uint32_t h = gui_syscall_get_height();
    if (!s_exgui.enabled || w < 160u || h < 120u) return;
    if (s_exgui.style == EXGUI_STYLE_LINUX) draw_linux_shell(w, h);
    else if (s_exgui.style == EXGUI_STYLE_MAC) draw_mac_shell(w, h);
    else draw_win_shell(w, h);
    text(18u, s_exgui.style == EXGUI_STYLE_WIN && h > 58u ? h - 58u : 38u,
         exgui_layout_name((uint32_t)s_exgui.layout), panel_text(), panel_color());
}

int exgui_selftest(void)
{
    exgui_state_t saved = s_exgui;
    exgui_info_t info;
    exgui_init();
    if (exgui_enable(1) != 0) {
        s_exgui = saved;
        return -1;
    }
    if (exgui_set_style("linux") != 0 || exgui_set_layout("tile") != 0) {
        s_exgui = saved;
        return -2;
    }
    exgui_focus_next();
    exgui_info(&info);
    if (!info.enabled || info.style != EXGUI_STYLE_LINUX || info.layout != EXGUI_LAYOUT_TILE ||
        info.focused != 1u || info.window_count < 3u) {
        s_exgui = saved;
        return -3;
    }
    if (exgui_set_style("bad") == 0) {
        s_exgui = saved;
        return -4;
    }
    s_exgui = saved;
    return 0;
}
