#include "lassist.h"

#include "gui.h"

#include <stdint.h>

typedef struct {
    uint32_t enabled;
    uint32_t jokes;
    uint32_t tick;
    uint32_t mood;
    char message[LASSIST_TEXT_MAX + 1u];
} lassist_state_t;

static lassist_state_t s_lassist;

static const char* const s_tips[] = {
    "Need a setting flipped? Try cfgsh. I bring casual supervision.",
    "I follow the shell, the GUI, and suspiciously confident ideas.",
    "Tiny advice: sync after changing boot settings. Future-you likes that.",
    "LardOS rule of thumb: if it can be owned, make it visible.",
    "I am not in the way. I am strategically adjacent.",
    "POST catches code bugs and the kind you can actually see.",
    "Need speed? awake on. Need predictability? awake off. I have range.",
    "lev.10 is yours now. Use power, maybe stretch first.",
};

static const char* const s_jokes[] = {
    "I would make a paper joke, but I am trying not to fold under pressure.",
    "I checked the kernel vibes. Mostly heroic, slightly crunchy.",
    "Your OS has more modes than my chair, and I do not own a chair.",
    "If a setting looks scary, call it experimental and suddenly it has manners.",
    "I am following you around because multitasking needs emotional support.",
    "I asked the scheduler for priority. It said, buddy, take a number.",
};

static uint32_t scopy(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return 0;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static uint32_t slen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void set_message(const char* msg)
{
    (void)scopy(s_lassist.message, sizeof(s_lassist.message), msg);
}

static uint32_t app_mix(uint32_t app_id)
{
    return (app_id * 3u + s_lassist.jokes + (s_lassist.tick / 160u)) %
           (sizeof(s_tips) / sizeof(s_tips[0]));
}

void lassist_init(void)
{
    for (uint32_t i = 0; i < sizeof(s_lassist); i++) ((uint8_t*)&s_lassist)[i] = 0;
    set_message("Lard Buddy parked. Run buddy on if you want company.");
}

void lassist_enable(int on)
{
    s_lassist.enabled = on ? 1u : 0u;
    if (s_lassist.enabled) set_message("Lard Buddy online. I will hover politely.");
    else set_message("Lard Buddy off. I will loiter in spirit only.");
}

int lassist_enabled(void)
{
    return s_lassist.enabled ? 1 : 0;
}

void lassist_tick(uint32_t app_id)
{
    if (!s_lassist.enabled) return;
    s_lassist.tick++;
    if ((s_lassist.tick % 480u) == 0u) {
        s_lassist.mood = (s_lassist.mood + 1u) % 4u;
        set_message(s_tips[app_mix(app_id)]);
    }
}

void lassist_next(uint32_t app_id)
{
    s_lassist.mood = (s_lassist.mood + 1u) % 4u;
    s_lassist.tick += 73u;
    set_message(s_tips[app_mix(app_id)]);
}

void lassist_joke(void)
{
    uint32_t n = sizeof(s_jokes) / sizeof(s_jokes[0]);
    set_message(s_jokes[s_lassist.jokes % n]);
    s_lassist.jokes++;
    s_lassist.enabled = 1u;
}

void lassist_info(lassist_info_t* out)
{
    if (!out) return;
    out->enabled = s_lassist.enabled;
    out->jokes = s_lassist.jokes;
    out->tick = s_lassist.tick;
    out->mood = s_lassist.mood;
    (void)scopy(out->message, sizeof(out->message), s_lassist.message);
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (w == 0 || h == 0) return;
    gui_syscall_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

static void text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg)
{
    gui_syscall_draw_text((uint16_t)x, (uint16_t)y, s, fg, bg);
}

static void frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (w < 2u || h < 2u) return;
    fill(x, y, w, 1u, color);
    fill(x, y + h - 1u, w, 1u, color);
    fill(x, y, 1u, h, color);
    fill(x + w - 1u, y, 1u, h, color);
}

static void line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0) gui_syscall_put_pixel((uint16_t)x0, (uint16_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_buddy(uint32_t x, uint32_t y)
{
    uint32_t body = 0xFFE7F2FFu;
    uint32_t edge = 0xFF203040u;
    uint32_t eye = 0xFF101018u;
    uint32_t hi = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < 3u; i++) {
        line((int)x + 10 + (int)i, (int)y + 5, (int)x + 27 + (int)i, (int)y + 25, body);
        line((int)x + 27 + (int)i, (int)y + 25, (int)x + 14 + (int)i, (int)y + 43, body);
        line((int)x + 14 + (int)i, (int)y + 43, (int)x + 5 + (int)i, (int)y + 29, body);
    }
    line((int)x + 8, (int)y + 4, (int)x + 30, (int)y + 28, edge);
    line((int)x + 30, (int)y + 28, (int)x + 14, (int)y + 47, edge);
    line((int)x + 14, (int)y + 47, (int)x + 3, (int)y + 29, edge);
    line((int)x + 3, (int)y + 29, (int)x + 8, (int)y + 4, edge);
    fill(x + 13u, y + 17u, 3u, 3u, eye);
    fill(x + 21u, y + 17u, 3u, 3u, eye);
    line((int)x + 13, (int)y + 28, (int)x + 24, (int)y + 28, edge);
    line((int)x + 11, (int)y + 9, (int)x + 22, (int)y + 20, hi);
}

static void draw_bubble(uint32_t x, uint32_t y, const char* msg)
{
    uint32_t len = slen(msg);
    uint32_t chars = len > 44u ? 44u : len;
    char linebuf[48];
    uint32_t bw = chars * 8u + 16u;
    for (uint32_t i = 0; i < chars && i + 1u < sizeof(linebuf); i++) linebuf[i] = msg[i];
    linebuf[chars] = '\0';
    if (len > chars && chars > 3u) {
        linebuf[chars - 3u] = '.';
        linebuf[chars - 2u] = '.';
        linebuf[chars - 1u] = '.';
    }
    if (bw < 144u) bw = 144u;
    if (bw > 392u) bw = 392u;
    fill(x + 3u, y + 3u, bw, 38u, 0x88202030u);
    fill(x, y, bw, 38u, 0xFFF2F6FFu);
    frame(x, y, bw, 38u, 0xFF203040u);
    text(x + 8u, y + 10u, linebuf, 0xFF102030u, 0xFFF2F6FFu);
    line((int)x + 24, (int)y + 38, (int)x + 10, (int)y + 48, 0xFF203040u);
    line((int)x + 25, (int)y + 38, (int)x + 11, (int)y + 48, 0xFFF2F6FFu);
}

void lassist_draw(uint32_t app_id, uint32_t mouse_x, uint32_t mouse_y,
                  uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    (void)app_id;
    if (!s_lassist.enabled) return;
    uint32_t sw = gui_syscall_get_width();
    uint32_t sh = gui_syscall_get_height();
    if (sw < 260u || sh < 180u) return;

    uint32_t bx = win_x + win_w + 12u;
    uint32_t base_y = win_y + (win_h > 260u ? 64u : 32u);
    uint32_t by = base_y + ((s_lassist.tick / 30u + s_lassist.mood * 9u) % 34u);
    if (bx + 220u > sw) bx = sw > 220u ? sw - 220u : 8u;
    if (by + 86u > sh) by = sh > 86u ? sh - 86u : 8u;
    if (mouse_x > bx && mouse_x < bx + 56u && mouse_y > by && mouse_y < by + 60u) {
        by = by > 64u ? by - 54u : by + 54u;
    }
    draw_buddy(bx, by + 30u);
    draw_bubble(bx + 38u, by, s_lassist.message);
}

int lassist_selftest(void)
{
    lassist_state_t saved = s_lassist;
    lassist_info_t info;
    lassist_init();
    if (lassist_enabled()) {
        s_lassist = saved;
        return -1;
    }
    lassist_enable(1);
    if (!lassist_enabled()) {
        s_lassist = saved;
        return -2;
    }
    lassist_joke();
    lassist_info(&info);
    if (!info.enabled || info.jokes != 1u || info.message[0] == '\0') {
        s_lassist = saved;
        return -3;
    }
    lassist_enable(0);
    if (lassist_enabled()) {
        s_lassist = saved;
        return -4;
    }
    s_lassist = saved;
    return 0;
}
