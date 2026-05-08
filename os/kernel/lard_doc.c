#include "lard_doc.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LARD_DOC_NONE = 0,
    LARD_DOC_LARS = 1,
    LARD_DOC_LARDD = 2,
} lard_doc_kind_t;

static void out_ch(char* out, uint32_t cap, uint32_t* pos, char c)
{
    if (*pos + 1 < cap) {
        out[*pos] = c;
        (*pos)++;
        out[*pos] = '\0';
    }
}

static void out_n(char* out, uint32_t cap, uint32_t* pos, const char* s, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) out_ch(out, cap, pos, s[i]);
}

static void out_s(char* out, uint32_t cap, uint32_t* pos, const char* s)
{
    while (*s) out_ch(out, cap, pos, *s++);
}

static void copy_range(char* out, uint32_t cap, const char* p, const char* end)
{
    uint32_t n = 0;
    if (!out || cap == 0) return;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
    while (p < end && n + 1u < cap) out[n++] = *p++;
    out[n] = '\0';
}

static const char* skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static int line_eq(const char* p, const char* end, const char* s)
{
    while (p < end && *s && *p == *s) {
        p++;
        s++;
    }
    return p == end && *s == '\0';
}

static int line_has_prefix(const char* p, const char* end, const char* prefix,
                           const char** value)
{
    const char* q = p;
    while (q < end && *prefix && *q == *prefix) {
        q++;
        prefix++;
    }
    if (*prefix) return 0;
    if (q < end && *q != ' ' && *q != '\t' && *q != ':') return 0;
    if (q < end && *q == ':') q++;
    *value = skip_ws(q, end);
    return 1;
}

static const char* find_body(const char* p, uint32_t len, uint32_t* body_len)
{
    for (uint32_t i = 0; i + 3 < len; i++) {
        if ((p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') ||
            (p[i] == '\n' && p[i + 1] == '\n')) {
            uint32_t off = (p[i] == '\r') ? 4u : 2u;
            *body_len = len - i - off;
            return p + i + off;
        }
    }
    *body_len = len;
    return p;
}

static lard_doc_kind_t detect_kind(const char* p, uint32_t len)
{
    const char* end = p + len;
    const char* ls = p;
    const char* le;
    while (ls < end && (*ls == ' ' || *ls == '\t' || *ls == '\r' || *ls == '\n')) ls++;
    le = ls;
    while (le < end && *le != '\r' && *le != '\n') le++;
    if (line_eq(ls, le, "LARS 1")) return LARD_DOC_LARS;
    if (line_eq(ls, le, "LARDD 1")) return LARD_DOC_LARDD;
    return LARD_DOC_NONE;
}

static void render_heading(char* out, uint32_t cap, uint32_t* pos,
                           const char* p, const char* end, char mark)
{
    out_ch(out, cap, pos, mark);
    out_ch(out, cap, pos, mark);
    out_ch(out, cap, pos, ' ');
    out_n(out, cap, pos, p, (uint32_t)(end - p));
    out_ch(out, cap, pos, ' ');
    out_ch(out, cap, pos, mark);
    out_ch(out, cap, pos, mark);
    out_ch(out, cap, pos, '\n');
}

static void render_lars_line(char* out, uint32_t cap, uint32_t* pos,
                             const char* p, const char* end)
{
    const char* v;
    p = skip_ws(p, end);
    if (p == end || line_eq(p, end, "LARS 1")) return;
    if (line_has_prefix(p, end, "title", &v) || line_has_prefix(p, end, "h1", &v)) {
        render_heading(out, cap, pos, v, end, '=');
    } else if (line_has_prefix(p, end, "section", &v) || line_has_prefix(p, end, "h2", &v)) {
        out_ch(out, cap, pos, '\n');
        render_heading(out, cap, pos, v, end, '-');
    } else if (line_has_prefix(p, end, "p", &v) || line_has_prefix(p, end, "text", &v)) {
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "li", &v) || line_has_prefix(p, end, "item", &v)) {
        out_s(out, cap, pos, " * ");
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "cmd", &v)) {
        out_s(out, cap, pos, " > ");
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "button", &v)) {
        const char* bar = v;
        while (bar < end && *bar != '|') bar++;
        out_s(out, cap, pos, " [button] ");
        out_n(out, cap, pos, v, (uint32_t)(bar - v));
        if (bar < end) {
            out_s(out, cap, pos, " -> ");
            out_n(out, cap, pos, bar + 1, (uint32_t)(end - bar - 1));
        }
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "input", &v)) {
        out_s(out, cap, pos, " [input] ");
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "note", &v)) {
        out_s(out, cap, pos, " ! ");
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (!line_eq(p, end, "end")) {
        out_n(out, cap, pos, p, (uint32_t)(end - p));
        out_ch(out, cap, pos, '\n');
    }
}

static int render_lardd_line(char* out, uint32_t cap, uint32_t* pos,
                             const char* p, const char* end, int* code)
{
    const char* v;
    p = skip_ws(p, end);
    if (line_eq(p, end, "LARDD 1")) return 0;
    if (*code) {
        if (line_eq(p, end, "ENDCODE")) {
            *code = 0;
        } else {
            out_s(out, cap, pos, "    ");
            out_n(out, cap, pos, p, (uint32_t)(end - p));
            out_ch(out, cap, pos, '\n');
        }
        return 0;
    }
    if (p == end) {
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "TITLE", &v)) {
        render_heading(out, cap, pos, v, end, '=');
    } else if (line_has_prefix(p, end, "SECTION", &v)) {
        out_ch(out, cap, pos, '\n');
        render_heading(out, cap, pos, v, end, '-');
    } else if (line_has_prefix(p, end, "TEXT", &v)) {
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "ITEM", &v)) {
        out_s(out, cap, pos, " * ");
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_has_prefix(p, end, "QUOTE", &v)) {
        out_s(out, cap, pos, " | ");
        out_n(out, cap, pos, v, (uint32_t)(end - v));
        out_ch(out, cap, pos, '\n');
    } else if (line_eq(p, end, "CODE")) {
        *code = 1;
    } else if (!line_eq(p, end, "END")) {
        out_n(out, cap, pos, p, (uint32_t)(end - p));
        out_ch(out, cap, pos, '\n');
    }
    return 0;
}

static int lars_action_from_line(const char* p, const char* end, lard_doc_action_t* out)
{
    const char* v;
    p = skip_ws(p, end);
    if (line_has_prefix(p, end, "button", &v)) {
        const char* bar = v;
        while (bar < end && *bar != '|') bar++;
        if (!out) return 1;
        copy_range(out->kind, sizeof(out->kind), "button", "button" + 6);
        if (bar < end) {
            copy_range(out->label, sizeof(out->label), v, bar);
            copy_range(out->command, sizeof(out->command), bar + 1, end);
        } else {
            copy_range(out->label, sizeof(out->label), v, end);
            out->command[0] = '\0';
        }
        return 1;
    }
    if (line_has_prefix(p, end, "input", &v)) {
        if (!out) return 1;
        copy_range(out->kind, sizeof(out->kind), "input", "input" + 5);
        copy_range(out->label, sizeof(out->label), v, end);
        out->command[0] = '\0';
        return 1;
    }
    return 0;
}

int lard_doc_to_text(const char* input, uint32_t input_len, char* out, uint32_t out_cap)
{
    uint32_t body_len;
    uint32_t pos = 0;
    lard_doc_kind_t kind;
    int code = 0;
    const char* p;
    const char* end;

    if (!input || !out || out_cap == 0) return -1;
    out[0] = '\0';
    input = find_body(input, input_len, &body_len);
    kind = detect_kind(input, body_len);
    if (kind == LARD_DOC_NONE) return -2;

    p = input;
    end = input + body_len;
    while (p < end) {
        const char* ls = p;
        const char* le;
        while (ls < end && (*ls == '\r' || *ls == '\n')) {
            if (kind == LARD_DOC_LARDD) out_ch(out, out_cap, &pos, '\n');
            ls++;
        }
        le = ls;
        while (le < end && *le != '\r' && *le != '\n') le++;
        if (kind == LARD_DOC_LARS) render_lars_line(out, out_cap, &pos, ls, le);
        else render_lardd_line(out, out_cap, &pos, ls, le, &code);
        p = le;
        while (p < end && (*p == '\r' || *p == '\n')) p++;
    }
    return 0;
}

int lard_doc_action_count(const char* input, uint32_t input_len)
{
    uint32_t body_len;
    uint32_t count = 0;
    const char* p;
    const char* end;
    if (!input) return -1;
    input = find_body(input, input_len, &body_len);
    if (detect_kind(input, body_len) != LARD_DOC_LARS) return 0;
    p = input;
    end = input + body_len;
    while (p < end) {
        const char* ls = p;
        const char* le;
        while (ls < end && (*ls == '\r' || *ls == '\n')) ls++;
        le = ls;
        while (le < end && *le != '\r' && *le != '\n') le++;
        if (lars_action_from_line(ls, le, NULL)) count++;
        p = le;
        while (p < end && (*p == '\r' || *p == '\n')) p++;
    }
    return (int)count;
}

int lard_doc_action_at(const char* input, uint32_t input_len, uint32_t index, lard_doc_action_t* out)
{
    uint32_t body_len;
    uint32_t count = 0;
    const char* p;
    const char* end;
    if (!input || !out) return -1;
    input = find_body(input, input_len, &body_len);
    if (detect_kind(input, body_len) != LARD_DOC_LARS) return -2;
    p = input;
    end = input + body_len;
    while (p < end) {
        const char* ls = p;
        const char* le;
        while (ls < end && (*ls == '\r' || *ls == '\n')) ls++;
        le = ls;
        while (le < end && *le != '\r' && *le != '\n') le++;
        if (lars_action_from_line(ls, le, NULL)) {
            if (count == index) return lars_action_from_line(ls, le, out) ? 0 : -3;
            count++;
        }
        p = le;
        while (p < end && (*p == '\r' || *p == '\n')) p++;
    }
    return -4;
}

int lard_doc_selftest(void)
{
    static const char doc[] =
        "LARS 1\n"
        "title Demo\n"
        "button Status | status\n"
        "input name default\n"
        "end\n";
    char text[256];
    lard_doc_action_t a;
    if (lard_doc_to_text(doc, sizeof(doc) - 1, text, sizeof(text)) != 0) return -1;
    if (lard_doc_action_count(doc, sizeof(doc) - 1) != 2) return -2;
    if (lard_doc_action_at(doc, sizeof(doc) - 1, 0, &a) != 0) return -3;
    if (a.kind[0] != 'b' || a.command[0] != 's') return -4;
    return 0;
}
