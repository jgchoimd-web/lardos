/*
 * LML (Lardos Markup Language) parser.
 * Minimal XML-like markup for OS config, UI, documents.
 */
#include "lml.h"
#include "mem.h"
#include "string.h"
#include <stdint.h>

#define TAG_BUF 64
#define ATTR_BUF 128
#define MAX_DEPTH 32

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_ws(const char** pp)
{
    const char* p = *pp;
    for (;;) {
        while (is_space(*p)) p++;
        if (*p == ';' || *p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        break;
    }
    *pp = p;
}

static int scan_comment(const char** pp)
{
    const char* p = *pp;
    if (p[0] != '<' || p[1] != '!' || p[2] != '-' || p[3] != '-') return 0;
    p += 4;
    for (;;) {
        if (!*p) return -1;
        if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
            p += 3;
            *pp = p;
            return 1;
        }
        p++;
    }
}

static int is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static int parse_name(const char** pp, char* buf, size_t cap)
{
    const char* p = *pp;
    size_t n = 0;
    if (!is_name_char(*p)) return -1;
    while (is_name_char(*p) && n + 1 < cap) {
        buf[n++] = *p++;
    }
    buf[n] = '\0';
    *pp = p;
    return (int)n;
}

static int parse_attr_value(const char** pp, char* buf, size_t cap)
{
    const char* p = *pp;
    if (*p != '"') return -1;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') buf[n++] = '\n';
            else if (*p == 't') buf[n++] = '\t';
            else if (*p == '"') buf[n++] = '"';
            else if (*p == '&') buf[n++] = '&';
            else buf[n++] = *p;
            if (*p) p++;
        } else {
            buf[n++] = *p++;
        }
    }
    if (*p != '"') return -1;
    p++;
    buf[n] = '\0';
    *pp = p;
    return (int)n;
}

int lml_parse(const char* src, lml_cb_fn cb, void* user)
{
    if (!src || !cb) return -1;

    const char* p = src;
    char tag_buf[TAG_BUF];
    char attr_name[ATTR_BUF];
    char attr_val[ATTR_BUF];
    int depth = 0;

    for (;;) {
        skip_ws(&p);
        if (!*p) break;

        if (p[0] == '<') {
            if (p[1] == '!' && p[2] == '-') {
                if (scan_comment(&p) < 0) return -2;
                continue;
            }
            if (p[1] == '/') {
                p += 2;
                if (parse_name(&p, tag_buf, sizeof(tag_buf)) < 0) return -3;
                skip_ws(&p);
                if (*p != '>') return -4;
                p++;
                if (depth <= 0) return -5;
                depth--;
                if (cb(LML_CLOSE_TAG, tag_buf, 0, user) != 0) return -6;
                continue;
            }
            p++;
            if (parse_name(&p, tag_buf, sizeof(tag_buf)) < 0) return -7;
            if (depth >= MAX_DEPTH) return -8;
            depth++;

            if (cb(LML_OPEN_TAG, tag_buf, 0, user) != 0) return -9;

            for (;;) {
                skip_ws(&p);
                if (*p == '>') {
                    p++;
                    break;
                }
                if (*p == '/' && p[1] == '>') {
                    p += 2;
                    depth--;
                    if (cb(LML_CLOSE_TAG, tag_buf, 0, user) != 0) return -10;
                    break;
                }
                if (parse_name(&p, attr_name, sizeof(attr_name)) < 0) return -11;
                skip_ws(&p);
                if (*p != '=') return -12;
                p++;
                skip_ws(&p);
                if (parse_attr_value(&p, attr_val, sizeof(attr_val)) < 0) return -13;
                if (cb(LML_ATTR, attr_name, attr_val, user) != 0) return -14;
            }
            continue;
        }

        const char* text_start = p;
        while (*p && *p != '<') p++;
        if (p > text_start) {
            char txt[512];
            size_t len = (size_t)(p - text_start);
            if (len >= sizeof(txt)) len = sizeof(txt) - 1;
            for (size_t i = 0; i < len; i++) txt[i] = text_start[i];
            txt[len] = '\0';
            while (len > 0 && is_space(txt[len - 1])) {
                txt[--len] = '\0';
            }
            if (len > 0 && cb(LML_TEXT, 0, txt, user) != 0) return -15;
        }
    }

    if (depth != 0) return -16;
    return 0;
}

/* ----- Tree builder ----- */

static lml_node_t* node_new(const char* tag)
{
    lml_node_t* n = (lml_node_t*)kmalloc(sizeof(lml_node_t));
    if (!n) return 0;
    size_t tl = strlen(tag) + 1;
    n->tag = (char*)kmalloc((uint32_t)tl);
    if (!n->tag) {
        kfree(n);
        return 0;
    }
    memcpy(n->tag, tag, tl);
    n->text = 0;
    n->child = 0;
    n->nchild = 0;
    n->attr_names = 0;
    n->attr_vals = 0;
    n->nattr = 0;
    return n;
}

static void node_add_attr(lml_node_t* n, const char* name, const char* val)
{
    uint32_t cnt = n->nattr;
    char** an = (char**)kmalloc((cnt + 1) * sizeof(char*));
    char** av = (char**)kmalloc((cnt + 1) * sizeof(char*));
    if (!an || !av) {
        if (an) kfree(an);
        if (av) kfree(av);
        return;
    }
    for (uint32_t i = 0; i < cnt; i++) {
        an[i] = n->attr_names[i];
        av[i] = n->attr_vals[i];
    }
    size_t nl = strlen(name) + 1;
    size_t vl = strlen(val) + 1;
    an[cnt] = (char*)kmalloc((uint32_t)nl);
    av[cnt] = (char*)kmalloc((uint32_t)vl);
    if (!an[cnt] || !av[cnt]) {
        if (an[cnt]) kfree(an[cnt]);
        if (av[cnt]) kfree(av[cnt]);
        kfree(an);
        kfree(av);
        return;
    }
    memcpy(an[cnt], name, nl);
    memcpy(av[cnt], val, vl);
    if (n->attr_names) kfree(n->attr_names);
    if (n->attr_vals) kfree(n->attr_vals);
    n->attr_names = an;
    n->attr_vals = av;
    n->nattr = cnt + 1;
}

static int node_add_child(lml_node_t* parent, lml_node_t* child)
{
    uint32_t cnt = parent->nchild;
    lml_node_t** ch = (lml_node_t**)kmalloc((cnt + 1) * sizeof(lml_node_t*));
    if (!ch) return -1;
    for (uint32_t i = 0; i < cnt; i++) ch[i] = parent->child[i];
    ch[cnt] = child;
    if (parent->child) kfree(parent->child);
    parent->child = ch;
    parent->nchild = cnt + 1;
    return 0;
}

typedef struct {
    lml_node_t* root;
    lml_node_t* stack[MAX_DEPTH];
    int sp;
} tree_ctx_t;

static int tree_cb(lml_event_t ev, const char* name, const char* value, void* user)
{
    tree_ctx_t* ctx = (tree_ctx_t*)user;

    switch (ev) {
    case LML_OPEN_TAG: {
        lml_node_t* n = node_new(name);
        if (!n) return -1;
        if (ctx->sp < 0) {
            ctx->root = node_new("lml");
            if (!ctx->root) {
                lml_tree_free(n);
                return -1;
            }
            ctx->stack[0] = ctx->root;
            ctx->sp = 0;
            if (node_add_child(ctx->root, n) != 0) {
                lml_tree_free(n);
                return -1;
            }
            ctx->stack[1] = n;
            ctx->sp = 1;
        } else {
            if (node_add_child(ctx->stack[ctx->sp], n) != 0) {
                lml_tree_free(n);
                return -1;
            }
            if (ctx->sp + 1 < MAX_DEPTH) {
                ctx->sp++;
                ctx->stack[ctx->sp] = n;
            }
        }
        break;
    }
    case LML_ATTR:
        if (ctx->sp >= 0 && ctx->stack[ctx->sp])
            node_add_attr(ctx->stack[ctx->sp], name, value);
        break;
    case LML_CLOSE_TAG:
        if (ctx->sp >= 0) ctx->sp--;
        break;
    case LML_TEXT:
        if (ctx->sp >= 0 && ctx->stack[ctx->sp] && value && value[0]) {
            lml_node_t* n = ctx->stack[ctx->sp];
            size_t len = strlen(value) + 1;
            n->text = (char*)kmalloc((uint32_t)len);
            if (n->text) memcpy(n->text, value, len);
        }
        break;
    }
    return 0;
}

int lml_parse_tree(const char* src, lml_node_t** out)
{
    if (!src || !out) return -1;
    *out = 0;

    tree_ctx_t ctx;
    ctx.root = 0;
    ctx.sp = -1;

    int r = lml_parse(src, tree_cb, &ctx);
    if (r != 0) {
        if (ctx.root) lml_tree_free(ctx.root);
        return r;
    }
    *out = ctx.root;
    return 0;
}

void lml_tree_free(lml_node_t* n)
{
    if (!n) return;
    if (n->tag) kfree(n->tag);
    if (n->text) kfree(n->text);
    for (uint32_t i = 0; i < n->nattr; i++) {
        if (n->attr_names[i]) kfree(n->attr_names[i]);
        if (n->attr_vals[i]) kfree(n->attr_vals[i]);
    }
    if (n->attr_names) kfree(n->attr_names);
    if (n->attr_vals) kfree(n->attr_vals);
    for (uint32_t i = 0; i < n->nchild; i++) {
        lml_tree_free(n->child[i]);
    }
    if (n->child) kfree(n->child);
    kfree(n);
}

const char* lml_attr(const lml_node_t* n, const char* name)
{
    if (!n || !name) return 0;
    for (uint32_t i = 0; i < n->nattr; i++) {
        if (strcmp(n->attr_names[i], name) == 0)
            return n->attr_vals[i];
    }
    return 0;
}
