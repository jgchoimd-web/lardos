#include "fstwt.h"

typedef struct {
    uint32_t active;
    uint32_t script_bytes;
    uint32_t to_hits;
    uint32_t from_hits;
    uint32_t misses;
    uint32_t last_error;
    char source[FSTWT_NAME_MAX];
    char script[FSTWT_SCRIPT_MAX];
} fstwt_state_t;

typedef struct {
    const char* left;
    uint32_t left_len;
    const char* right;
    uint32_t right_len;
} fstwt_rule_t;

static fstwt_state_t s_fstwt;

static const char s_default_script[] =
    "FSTWTS 1\n"
    "# File System Two Way Translator scripts are user-owned path maps.\n"
    "# MAP external-prefix <=> lardos-prefix\n"
    "MAP fstwt/demo/ <=> f2wdemo_\n";

static uint32_t st_len(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static int ci_eq_n(const char* a, const char* b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (lower_char(a[i]) != lower_char(b[i])) return 0;
    }
    return 1;
}

static int ci_starts(const char* s, const char* pfx, uint32_t pfx_len)
{
    if (!s || !pfx) return 0;
    for (uint32_t i = 0; i < pfx_len; i++) {
        if (!s[i] || lower_char(s[i]) != lower_char(pfx[i])) return 0;
    }
    return 1;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_rule_word(const char* p, const char* word)
{
    uint32_t n = st_len(word);
    if (!ci_eq_n(p, word, n)) return 0;
    return p[n] == ' ' || p[n] == '\t';
}

static uint32_t trim_left(const char* s, uint32_t i, uint32_t end)
{
    while (i < end && is_space(s[i])) i++;
    return i;
}

static uint32_t trim_right(const char* s, uint32_t start, uint32_t end)
{
    while (end > start && is_space(s[end - 1u])) end--;
    return end;
}

static void copy_source(const char* source)
{
    uint32_t i = 0;
    const char* s = source && source[0] ? source : "memory";
    while (s[i] && i + 1u < FSTWT_NAME_MAX) {
        s_fstwt.source[i] = s[i];
        i++;
    }
    s_fstwt.source[i] = '\0';
}

static void copy_sanitized(char* out, uint32_t cap, uint32_t* io, const char* s, uint32_t len)
{
    uint32_t n = io ? *io : 0;
    uint32_t last_us = 0;
    if (!out || cap == 0 || !io) return;
    if (n > 0 && out[n - 1u] == '_') last_us = 1;
    for (uint32_t i = 0; i < len && n + 1u < cap; i++) {
        char c = s[i];
        char put;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            put = c;
        } else {
            put = '_';
        }
        if (put == '_') {
            if (last_us) continue;
            last_us = 1;
        } else {
            last_us = 0;
        }
        out[n++] = put;
    }
    while (n > 0 && (out[n - 1u] == '_' || out[n - 1u] == '.')) n--;
    out[n] = '\0';
    *io = n;
}

static void copy_plain(char* out, uint32_t cap, uint32_t* io, const char* s, uint32_t len)
{
    uint32_t n = io ? *io : 0;
    if (!out || cap == 0 || !io) return;
    for (uint32_t i = 0; i < len && n + 1u < cap; i++) out[n++] = s[i];
    out[n] = '\0';
    *io = n;
}

static int parse_rule_at(const char* s, uint32_t line_start, uint32_t line_end, fstwt_rule_t* out)
{
    uint32_t p = trim_left(s, line_start, line_end);
    uint32_t left_start;
    uint32_t left_end;
    uint32_t right_start;
    uint32_t right_end;
    uint32_t arrow = line_end;
    if (p >= line_end || s[p] == '#' || s[p] == ';') return 0;
    if (!is_rule_word(s + p, "map") && !is_rule_word(s + p, "alias")) return 0;
    while (p < line_end && !is_space(s[p])) p++;
    p = trim_left(s, p, line_end);
    left_start = p;
    while (p + 1u < line_end) {
        if (s[p] == '<' && s[p + 1u] == '=') { arrow = p; break; }
        if (s[p] == '=' && s[p + 1u] == '>') { arrow = p; break; }
        p++;
    }
    if (arrow >= line_end) return -1;
    left_end = trim_right(s, left_start, arrow);
    if (s[arrow] == '<') {
        p = arrow + 2u;
        if (p < line_end && s[p] == '>') p++;
    } else {
        p = arrow + 2u;
    }
    p = trim_left(s, p, line_end);
    right_start = p;
    while (p < line_end && s[p] != '#' && s[p] != ';') p++;
    right_end = trim_right(s, right_start, p);
    if (left_end <= left_start || right_end <= right_start) return -2;
    out->left = s + left_start;
    out->left_len = left_end - left_start;
    out->right = s + right_start;
    out->right_len = right_end - right_start;
    return 1;
}

static uint32_t count_rules(void)
{
    const char* s = s_fstwt.script;
    uint32_t pos = 0;
    uint32_t rules = 0;
    while (pos < s_fstwt.script_bytes && s[pos]) {
        uint32_t start = pos;
        fstwt_rule_t r;
        while (pos < s_fstwt.script_bytes && s[pos] && s[pos] != '\n') pos++;
        if (parse_rule_at(s, start, pos, &r) == 1) rules++;
        if (pos < s_fstwt.script_bytes && s[pos] == '\n') pos++;
    }
    return rules;
}

static int translate(int reverse, const char* in, char* out, uint32_t cap)
{
    const char* s = s_fstwt.script;
    uint32_t pos = 0;
    if (!s_fstwt.active || !in || !out || cap == 0) return -1;
    out[0] = '\0';
    while (pos < s_fstwt.script_bytes && s[pos]) {
        uint32_t start = pos;
        fstwt_rule_t r;
        while (pos < s_fstwt.script_bytes && s[pos] && s[pos] != '\n') pos++;
        if (parse_rule_at(s, start, pos, &r) == 1) {
            const char* from = reverse ? r.right : r.left;
            uint32_t from_len = reverse ? r.right_len : r.left_len;
            const char* to = reverse ? r.left : r.right;
            uint32_t to_len = reverse ? r.left_len : r.right_len;
            if (ci_starts(in, from, from_len)) {
                uint32_t n = 0;
                if (reverse) {
                    copy_plain(out, cap, &n, to, to_len);
                    copy_plain(out, cap, &n, in + from_len, st_len(in + from_len));
                } else {
                    copy_sanitized(out, cap, &n, to, to_len);
                    copy_sanitized(out, cap, &n, in + from_len, st_len(in + from_len));
                }
                if (!out[0]) return -3;
                return 0;
            }
        }
        if (pos < s_fstwt.script_bytes && s[pos] == '\n') pos++;
    }
    return -2;
}

void fstwt_init(void)
{
    (void)fstwt_load_script((const uint8_t*)s_default_script, sizeof(s_default_script) - 1u, "default.fstwts");
}

int fstwt_load_script(const uint8_t* data, uint32_t len, const char* source)
{
    uint32_t start = 0;
    uint32_t script_len;
    if (!data || len == 0) {
        s_fstwt.last_error = 1;
        return -1;
    }
    while (start + 6u < len) {
        if (data[start] == 'F' && data[start + 1u] == 'S' && data[start + 2u] == 'T' &&
            data[start + 3u] == 'W' && data[start + 4u] == 'T' && data[start + 5u] == 'S') break;
        start++;
    }
    if (start + 6u >= len) {
        s_fstwt.last_error = 4;
        return -4;
    }
    script_len = len - start;
    if (script_len >= FSTWT_SCRIPT_MAX) script_len = FSTWT_SCRIPT_MAX - 1u;
    for (uint32_t i = 0; i < script_len; i++) s_fstwt.script[i] = (char)data[start + i];
    s_fstwt.script[script_len] = '\0';
    s_fstwt.script_bytes = script_len;
    s_fstwt.active = 1;
    s_fstwt.last_error = 0;
    copy_source(source);
    return 0;
}

void fstwt_clear(void)
{
    s_fstwt.active = 0;
    s_fstwt.script_bytes = 0;
    s_fstwt.script[0] = '\0';
    copy_source("none");
}

int fstwt_translate_to_lard(const char* external_path, char* out, uint32_t cap)
{
    int r = translate(0, external_path, out, cap);
    if (r == 0) s_fstwt.to_hits++;
    else s_fstwt.misses++;
    if (r != -2 && r < 0) s_fstwt.last_error = (uint32_t)(-r);
    return r;
}

int fstwt_translate_from_lard(const char* lard_name, char* out, uint32_t cap)
{
    int r = translate(1, lard_name, out, cap);
    if (r == 0) s_fstwt.from_hits++;
    else s_fstwt.misses++;
    if (r != -2 && r < 0) s_fstwt.last_error = (uint32_t)(-r);
    return r;
}

void fstwt_info(fstwt_info_t* out)
{
    if (!out) return;
    out->active = s_fstwt.active;
    out->script_bytes = s_fstwt.script_bytes;
    out->rules = count_rules();
    out->to_hits = s_fstwt.to_hits;
    out->from_hits = s_fstwt.from_hits;
    out->misses = s_fstwt.misses;
    out->last_error = s_fstwt.last_error;
    for (uint32_t i = 0; i < FSTWT_NAME_MAX; i++) out->source[i] = s_fstwt.source[i];
}

const char* fstwt_script(void)
{
    return s_fstwt.script;
}

uint32_t fstwt_script_size(void)
{
    return s_fstwt.script_bytes;
}

int fstwt_selftest(void)
{
    char out[64];
    fstwt_state_t old = s_fstwt;
    static const char script[] =
        "RXE demo bytes before metadata\n"
        "FSTWTS 1\n"
        "MAP app:/save/ <=> appsave_\n"
        "MAP ext\\cfg\\ <=> cfg_\n";
    int ok = 1;
    if (fstwt_load_script((const uint8_t*)script, sizeof(script) - 1u, "selftest") != 0) ok = 0;
    if (ok && fstwt_translate_to_lard("app:/save/Slot 1.lardd", out, sizeof(out)) != 0) ok = 0;
    if (ok && !ci_starts(out, "appsave_slot_1.lardd", st_len("appsave_slot_1.lardd"))) ok = 0;
    if (ok && fstwt_translate_from_lard("cfg_video.txt", out, sizeof(out)) != 0) ok = 0;
    if (ok && !ci_starts(out, "ext\\cfg\\video.txt", st_len("ext\\cfg\\video.txt"))) ok = 0;
    s_fstwt = old;
    return ok ? 0 : -1;
}
