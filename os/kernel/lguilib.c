#include "lguilib.h"

#include "string.h"

#include <stddef.h>
#include <stdint.h>

#define LGUILIB_ERR_NONE      0u
#define LGUILIB_ERR_ARGS      1u
#define LGUILIB_ERR_HEADER    2u
#define LGUILIB_ERR_COLOR     3u
#define LGUILIB_ERR_WIDGETS   4u
#define LGUILIB_ERR_END       5u

static lguilib_theme_t s_active;
static uint32_t s_active_valid;

static int ch_space(char c)
{
    return c == ' ' || c == '\t';
}

static int ch_hex(char c, uint32_t* out)
{
    if (c >= '0' && c <= '9') {
        *out = (uint32_t)(c - '0');
        return 1;
    }
    if (c >= 'a' && c <= 'f') {
        *out = 10u + (uint32_t)(c - 'a');
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        *out = 10u + (uint32_t)(c - 'A');
        return 1;
    }
    return 0;
}

static int next_line(const char** p, const char* end, const char** ls, const char** le)
{
    const char* s = *p;
    if (s >= end) return 0;
    while (s < end && (*s == '\r' || *s == '\n')) s++;
    if (s >= end) {
        *p = s;
        return 0;
    }
    *ls = s;
    while (s < end && *s != '\r' && *s != '\n') s++;
    *le = s;
    while (s < end && (*s == '\r' || *s == '\n')) s++;
    *p = s;
    return 1;
}

static void trim_line(const char** p, const char** end)
{
    while (*p < *end && ch_space(**p)) (*p)++;
    while (*end > *p && ch_space((*end)[-1])) (*end)--;
}

static int line_eq(const char* p, const char* end, const char* s)
{
    trim_line(&p, &end);
    while (p < end && *s && *p == *s) {
        p++;
        s++;
    }
    return p == end && *s == '\0';
}

static int line_prefix(const char* p, const char* end, const char* prefix, const char** value)
{
    trim_line(&p, &end);
    while (p < end && *prefix && *p == *prefix) {
        p++;
        prefix++;
    }
    if (*prefix) return 0;
    if (p < end && !ch_space(*p)) return 0;
    while (p < end && ch_space(*p)) p++;
    if (value) *value = p;
    return 1;
}

static void copy_value(char* out, uint32_t cap, const char* p, const char* end)
{
    uint32_t n = 0;
    if (!out || cap == 0) return;
    trim_line(&p, &end);
    while (p < end && n + 1u < cap) out[n++] = *p++;
    out[n] = '\0';
}

static int read_word(const char** p, const char* end, char* out, uint32_t cap)
{
    uint32_t n = 0;
    if (!p || !out || cap == 0) return -1;
    while (*p < end && ch_space(**p)) (*p)++;
    if (*p >= end) return -1;
    while (*p < end && !ch_space(**p) && n + 1u < cap) {
        out[n++] = **p;
        (*p)++;
    }
    while (*p < end && !ch_space(**p)) (*p)++;
    out[n] = '\0';
    return n ? 0 : -1;
}

static int parse_hex_arg(const char* p, const char* end, uint32_t* out)
{
    uint32_t v = 0;
    uint32_t digits = 0;
    trim_line(&p, &end);
    if (p + 2 <= end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (p < end && !ch_space(*p)) {
        uint32_t h;
        if (!ch_hex(*p, &h) || digits >= 8u) return -1;
        v = (v << 4) | h;
        digits++;
        p++;
    }
    while (p < end && ch_space(*p)) p++;
    if (p != end || digits == 0) return -1;
    *out = v;
    return 0;
}

static uint32_t solid_color(uint32_t v)
{
    if ((v & 0xFF000000u) == 0) return v | 0xFF000000u;
    return v;
}

static void copy_default(lguilib_theme_t* out)
{
    static const char name[] = "lardos-overlay";
    uint32_t i;
    memset(out, 0, sizeof(*out));
    for (i = 0; name[i] && i < LGUILIB_NAME_MAX; i++) out->name[i] = name[i];
    out->name[i] = '\0';
    out->title_bg = 0xFF172126u;
    out->title_fg = 0xFFF3F6F3u;
    out->title_accent = 0xFFFFC857u;
    out->panel_bg = 0xFF20252Bu;
    out->border = 0xFF0B0D10u;
    out->tab_active = 0xFF235D64u;
    out->tab_idle = 0xFF2A2F34u;
    out->tab_hover = 0xFF364047u;
    out->tab_accent = 0xFFFFB84Du;
    out->button_border = 0xFF3DB8A5u;
    out->button_hover = 0xFF184D55u;
    out->button_inner = 0x667BE0D6u;
    out->output_frame = 0xFF60717Cu;
    out->hint_fg = 0xFFC8D0D7u;
    out->shadow = 0x55000000u;
    out->widget_count = 0;
    out->last_error = LGUILIB_ERR_NONE;
}

static int apply_color(lguilib_theme_t* out, const char* name, uint32_t value)
{
    if (strcmp(name, "title_bg") == 0) out->title_bg = solid_color(value);
    else if (strcmp(name, "title_fg") == 0) out->title_fg = solid_color(value);
    else if (strcmp(name, "title_accent") == 0) out->title_accent = solid_color(value);
    else if (strcmp(name, "panel_bg") == 0) out->panel_bg = solid_color(value);
    else if (strcmp(name, "border") == 0) out->border = solid_color(value);
    else if (strcmp(name, "tab_active") == 0) out->tab_active = solid_color(value);
    else if (strcmp(name, "tab_idle") == 0) out->tab_idle = solid_color(value);
    else if (strcmp(name, "tab_hover") == 0) out->tab_hover = solid_color(value);
    else if (strcmp(name, "tab_accent") == 0) out->tab_accent = solid_color(value);
    else if (strcmp(name, "button_border") == 0) out->button_border = solid_color(value);
    else if (strcmp(name, "button_hover") == 0) out->button_hover = solid_color(value);
    else if (strcmp(name, "button_inner") == 0) out->button_inner = value;
    else if (strcmp(name, "output_frame") == 0) out->output_frame = solid_color(value);
    else if (strcmp(name, "hint_fg") == 0) out->hint_fg = solid_color(value);
    else if (strcmp(name, "shadow") == 0) out->shadow = value;
    else return -1;
    return 0;
}

static int parse_color_line(lguilib_theme_t* out, const char* p, const char* end)
{
    char name[32];
    uint32_t color;
    if (read_word(&p, end, name, sizeof(name)) != 0) return -1;
    if (parse_hex_arg(p, end, &color) != 0) return -1;
    return apply_color(out, name, color);
}

int lguilib_parse(const uint8_t* data, uint32_t len, lguilib_theme_t* out)
{
    const char* p;
    const char* end;
    const char* ls;
    const char* le;
    int header = 0;
    int ended = 0;
    lguilib_theme_t tmp;

    if (!out) return -1;
    copy_default(&tmp);
    if (!data || len == 0) {
        tmp.last_error = LGUILIB_ERR_ARGS;
        *out = tmp;
        return -1;
    }
    p = (const char*)data;
    end = p + len;

    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        const char* lsp = ls;
        const char* lep = le;
        trim_line(&lsp, &lep);
        if (lsp == lep || *lsp == '#') continue;
        if (!header) {
            if (!line_eq(lsp, lep, "LGUILIB 1")) {
                tmp.last_error = LGUILIB_ERR_HEADER;
                *out = tmp;
                return -2;
            }
            header = 1;
            continue;
        }
        if (line_prefix(lsp, lep, "NAME", &v)) {
            copy_value(tmp.name, sizeof(tmp.name), v, lep);
        } else if (line_prefix(lsp, lep, "COLOR", &v)) {
            if (parse_color_line(&tmp, v, lep) != 0) {
                tmp.last_error = LGUILIB_ERR_COLOR;
                *out = tmp;
                return -3;
            }
        } else if (line_prefix(lsp, lep, "WIDGET", &v)) {
            (void)v;
            if (tmp.widget_count >= LGUILIB_WIDGET_MAX) {
                tmp.last_error = LGUILIB_ERR_WIDGETS;
                *out = tmp;
                return -4;
            }
            tmp.widget_count++;
        } else if (line_eq(lsp, lep, "END")) {
            ended = 1;
            break;
        } else {
            tmp.last_error = LGUILIB_ERR_HEADER;
            *out = tmp;
            return -5;
        }
    }

    if (!header || !ended) {
        tmp.last_error = !header ? LGUILIB_ERR_HEADER : LGUILIB_ERR_END;
        *out = tmp;
        return -6;
    }
    if (tmp.widget_count == 0) {
        tmp.last_error = LGUILIB_ERR_WIDGETS;
        *out = tmp;
        return -7;
    }
    tmp.last_error = LGUILIB_ERR_NONE;
    *out = tmp;
    return 0;
}

void lguilib_init(void)
{
    copy_default(&s_active);
    s_active.widget_count = 5u;
    s_active_valid = 1u;
}

int lguilib_load_active(const uint8_t* data, uint32_t len)
{
    lguilib_theme_t parsed;
    copy_default(&parsed);
    int r = lguilib_parse(data, len, &parsed);
    if (r != 0) {
        s_active.last_error = parsed.last_error;
        return r;
    }
    s_active = parsed;
    s_active_valid = 1u;
    return 0;
}

void lguilib_active(lguilib_info_t* out)
{
    if (!out) return;
    if (!s_active_valid) lguilib_init();
    out->valid = s_active_valid;
    out->theme = s_active;
}

const lguilib_theme_t* lguilib_active_theme(void)
{
    if (!s_active_valid) lguilib_init();
    return &s_active;
}

const char* lguilib_error_name(uint32_t error)
{
    if (error == LGUILIB_ERR_NONE) return "none";
    if (error == LGUILIB_ERR_ARGS) return "args";
    if (error == LGUILIB_ERR_HEADER) return "header";
    if (error == LGUILIB_ERR_COLOR) return "color";
    if (error == LGUILIB_ERR_WIDGETS) return "widgets";
    if (error == LGUILIB_ERR_END) return "end";
    return "unknown";
}

int lguilib_selftest(void)
{
    static const uint8_t sample[] =
        "LGUILIB 1\n"
        "NAME selftest\n"
        "COLOR title_bg 0x123456\n"
        "COLOR title_fg 0xffffff\n"
        "COLOR shadow 0x22000000\n"
        "WIDGET title chrome\n"
        "WIDGET tab compact\n"
        "END\n";
    static const uint8_t bad[] =
        "LGUI 1\n"
        "END\n";
    lguilib_theme_t t;
    if (lguilib_parse(sample, sizeof(sample) - 1u, &t) != 0) return -1;
    if (strcmp(t.name, "selftest") != 0) return -2;
    if (t.title_bg != 0xFF123456u) return -3;
    if (t.shadow != 0x22000000u) return -4;
    if (t.widget_count != 2u) return -5;
    if (lguilib_parse(bad, sizeof(bad) - 1u, &t) == 0) return -6;
    return 0;
}
