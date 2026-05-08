#include "lil.h"

#include "mem.h"
#include "rtc.h"

#include <stddef.h>
#include <stdint.h>

enum { LIL_NUM = 1, LIL_SYM = 2, LIL_LIST = 3 };

typedef struct lil_node lil_node_t;
struct lil_node {
    int kind;
    int64_t num;
    char sym[32];
    lil_node_t** kids;
    uint32_t nkids;
};

typedef struct lil_env {
    struct lil_env* parent;
    char name[32];
    int64_t value;
} lil_env_t;

#define LIL_MAX_FUNCS 16
#define LIL_MAX_PARAMS 6
typedef struct {
    char name[32];
    uint32_t nparams;
    char params[LIL_MAX_PARAMS][32];
    lil_node_t* body;
} lil_func_t;

static lil_func_t g_funcs[LIL_MAX_FUNCS];
static int g_nfuncs;

static int lil_streq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void lil_copy_sym(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static uint64_t lil_mag(int64_t v)
{
    return (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
}

static int64_t lil_from_mag(uint64_t v)
{
    return (v > 9223372036854775807ULL) ? 9223372036854775807LL : (int64_t)v;
}

static void lil_put_dec(lil_putc_fn putc, void* user, int64_t v)
{
    char buf[24];
    uint32_t n = 0;
    int neg = 0;
    uint64_t mag;

    if (v == 0) {
        if (putc) putc('0', user);
        return;
    }
    if (v < 0) {
        neg = 1;
        mag = (uint64_t)(-(v + 1)) + 1u;
    } else {
        mag = (uint64_t)v;
    }
    while (mag && n < (uint32_t)sizeof(buf)) {
        buf[n++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    }
    if (neg && putc) {
        putc('-', user);
    }
    while (n > 0 && putc) {
        putc(buf[--n], user);
    }
}

static void lil_putc_nl(lil_putc_fn putc, void* user)
{
    if (putc) {
        putc('\n', user);
    }
}

static void skip_ws(const char** pp)
{
    const char* p = *pp;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == ';' || *p == '#') {
            while (*p && *p != '\n') {
                p++;
            }
            continue;
        }
        break;
    }
    *pp = p;
}

static int is_sym_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '+' || c == '*' || c == '/' || c == '%' || c == '=' ||
           c == '<' || c == '>' || c == '!' || c == '?';
}

static lil_node_t* lil_node_new(int kind)
{
    lil_node_t* n = (lil_node_t*)kmalloc((uint32_t)sizeof(lil_node_t));
    if (!n) {
        return 0;
    }
    n->kind = kind;
    n->num = 0;
    n->sym[0] = 0;
    n->kids = 0;
    n->nkids = 0;
    return n;
}

static void lil_tree_free(lil_node_t* n)
{
    if (!n) {
        return;
    }
    for (uint32_t i = 0; i < n->nkids; i++) {
        lil_tree_free(n->kids[i]);
    }
    kfree(n->kids);
    kfree(n);
}

static int parse_int(const char** pp, int64_t* out)
{
    const char* p = *pp;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        int64_t v = 0;
        int any = 0;
        while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
            any = 1;
            int dig;
            if (*p >= '0' && *p <= '9') {
                dig = *p - '0';
            } else if (*p >= 'a' && *p <= 'f') {
                dig = *p - 'a' + 10;
            } else {
                dig = *p - 'A' + 10;
            }
            v = (v << 4) + dig;
            p++;
        }
        if (!any) {
            return -1;
        }
        *out = neg ? -v : v;
        *pp = p;
        return 0;
    }
    if (*p < '0' || *p > '9') {
        return -1;
    }
    int64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    *pp = p;
    return 0;
}

static lil_node_t* parse_expr(const char** pp, int* err);

static lil_node_t* parse_list(const char** pp, int* err)
{
    skip_ws(pp);
    if (**pp != '(') {
        *err = -2;
        return 0;
    }
    (*pp)++;
    skip_ws(pp);

    lil_node_t* buf[128];
    uint32_t n = 0;

    while (**pp && **pp != ')') {
        if (n >= (uint32_t)(sizeof(buf) / sizeof(buf[0]))) {
            *err = -3;
            goto fail;
        }
        lil_node_t* ch = parse_expr(pp, err);
        if (*err || !ch) {
            goto fail;
        }
        buf[n++] = ch;
        skip_ws(pp);
    }
    if (**pp != ')') {
        *err = -4;
        goto fail;
    }
    (*pp)++;

    lil_node_t* node = lil_node_new(LIL_LIST);
    if (!node) {
        *err = -5;
        goto fail;
    }
    if (n > 0) {
        node->kids = (lil_node_t**)kmalloc(n * (uint32_t)sizeof(lil_node_t*));
        if (!node->kids) {
            *err = -5;
            lil_tree_free(node);
            goto fail;
        }
        for (uint32_t i = 0; i < n; i++) {
            node->kids[i] = buf[i];
        }
        node->nkids = n;
    }
    return node;

fail:
    for (uint32_t i = 0; i < n; i++) {
        lil_tree_free(buf[i]);
    }
    return 0;
}

static lil_node_t* parse_expr(const char** pp, int* err)
{
    skip_ws(pp);
    if (**pp == '(') {
        return parse_list(pp, err);
    }
    if (**pp == '-' || (**pp >= '0' && **pp <= '9')) {
        const char* save = *pp;
        int64_t v;
        if (parse_int(pp, &v) == 0) {
            lil_node_t* n = lil_node_new(LIL_NUM);
            if (!n) {
                *err = -5;
                return 0;
            }
            n->num = v;
            return n;
        }
        *pp = save;
    }
    if (!is_sym_char(**pp)) {
        *err = -6;
        return 0;
    }
    lil_node_t* n = lil_node_new(LIL_SYM);
    if (!n) {
        *err = -5;
        return 0;
    }
    uint32_t i = 0;
    while (is_sym_char(**pp) && i + 1 < sizeof(n->sym)) {
        n->sym[i++] = *(*pp)++;
    }
    n->sym[i] = 0;
    return n;
}

static int64_t lil_eval(lil_node_t* n, lil_env_t* env, lil_putc_fn putc, void* user, int* err, uint32_t* steps);

static int64_t lookup(const char* name, lil_env_t* env)
{
    for (lil_env_t* e = env; e; e = e->parent) {
        if (lil_streq(e->name, name)) {
            return e->value;
        }
    }
    return 0;
}

static int bound_p(const char* name, lil_env_t* env)
{
    for (lil_env_t* e = env; e; e = e->parent) {
        if (lil_streq(e->name, name)) {
            return 1;
        }
    }
    return 0;
}

static int64_t lil_eval_body(lil_node_t* n, uint32_t first, lil_env_t* env, lil_putc_fn putc, void* user, int* err, uint32_t* steps)
{
    int64_t r = 0;
    for (uint32_t i = first; i < n->nkids; i++) {
        r = lil_eval(n->kids[i], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
    }
    return r;
}

static int64_t lil_eval(lil_node_t* n, lil_env_t* env, lil_putc_fn putc, void* user, int* err, uint32_t* steps)
{
    if (*err) {
        return 0;
    }
    if (++(*steps) > 500000u) {
        *err = -10;
        return 0;
    }

    if (!n) {
        *err = -1;
        return 0;
    }
    if (n->kind == LIL_NUM) {
        return n->num;
    }
    if (n->kind == LIL_SYM) {
        if (bound_p(n->sym, env)) {
            return lookup(n->sym, env);
        }
        *err = -7;
        return 0;
    }

    /* list */
    if (n->nkids == 0) {
        *err = -8;
        return 0;
    }
    lil_node_t* opn = n->kids[0];
    if (opn->kind != LIL_SYM) {
        *err = -9;
        return 0;
    }
    const char* op = opn->sym;

    if (lil_streq(op, "print")) {
        if (n->nkids < 2) {
            *err = -11;
            return 0;
        }
        int64_t v = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        lil_put_dec(putc, user, v);
        lil_putc_nl(putc, user);
        return v;
    }

    if (lil_streq(op, "printn")) {
        if (n->nkids < 2) {
            *err = -11;
            return 0;
        }
        int64_t v = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        lil_put_dec(putc, user, v);
        return v;
    }

    if (lil_streq(op, "emit")) {
        if (n->nkids != 2) {
            *err = -16;
            return 0;
        }
        int64_t v = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        if (putc) putc((char)(v & 0xFF), user);
        return v;
    }

    if (lil_streq(op, "rand")) {
        static uint32_t s_r = 0;
        if (s_r == 0) {
            int64_t t = rtc_unix_seconds();
            s_r = (uint32_t)(t ^ (t >> 32)) | 1u;
        }
        s_r = (uint32_t)((uint64_t)s_r * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return (int64_t)(s_r & 0x7FFFu);
    }

    if (lil_streq(op, "time")) {
        return rtc_unix_seconds();
    }

    if (lil_streq(op, "assert")) {
        if (n->nkids < 2) {
            *err = -16;
            return 0;
        }
        for (uint32_t i = 1; i < n->nkids; i++) {
            int64_t v = lil_eval(n->kids[i], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
            if (v == 0) {
                *err = -22;
                return 0;
            }
        }
        return 1;
    }

    if (lil_streq(op, "let")) {
        if (n->nkids < 4) {
            *err = -12;
            return 0;
        }
        lil_node_t* name = n->kids[1];
        if (name->kind != LIL_SYM) {
            *err = -13;
            return 0;
        }
        int64_t val = lil_eval(n->kids[2], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        lil_env_t frame;
        frame.parent = env;
        lil_copy_sym(frame.name, (uint32_t)sizeof(frame.name), name->sym);
        frame.value = val;
        return lil_eval_body(n, 3, &frame, putc, user, err, steps);
    }

    if (lil_streq(op, "defun")) {
        if (n->nkids < 4) {
            *err = -12;
            return 0;
        }
        lil_node_t* name_n = n->kids[1];
        lil_node_t* params_n = n->kids[2];
        if (name_n->kind != LIL_SYM || params_n->kind != LIL_LIST) {
            *err = -13;
            return 0;
        }
        uint32_t nparams = params_n->nkids;
        if (nparams > LIL_MAX_PARAMS || g_nfuncs >= LIL_MAX_FUNCS) {
            *err = -20;
            return 0;
        }
        lil_func_t* f = &g_funcs[g_nfuncs++];
        lil_copy_sym(f->name, (uint32_t)sizeof(f->name), name_n->sym);
        f->nparams = nparams;
        for (uint32_t j = 0; j < nparams; j++) {
            lil_node_t* pn = params_n->kids[j];
            if (!pn || pn->kind != LIL_SYM) {
                *err = -13;
                return 0;
            }
            lil_copy_sym(f->params[j], (uint32_t)sizeof(f->params[j]), pn->sym);
        }
        f->body = n->kids[3];
        return 0;
    }

    if (lil_streq(op, "if")) {
        if (n->nkids < 3) {
            *err = -14;
            return 0;
        }
        int64_t c = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        if (c != 0) {
            return lil_eval(n->kids[2], env, putc, user, err, steps);
        }
        if (n->nkids >= 4) {
            return lil_eval(n->kids[3], env, putc, user, err, steps);
        }
        return 0;
    }

    if (lil_streq(op, "when") || lil_streq(op, "unless")) {
        if (n->nkids < 3) {
            *err = -14;
            return 0;
        }
        int64_t c = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        int take = lil_streq(op, "when") ? (c != 0) : (c == 0);
        if (take) {
            return lil_eval_body(n, 2, env, putc, user, err, steps);
        }
        return 0;
    }

    if (lil_streq(op, "begin")) {
        if (n->nkids < 2) {
            *err = -15;
            return 0;
        }
        return lil_eval_body(n, 1, env, putc, user, err, steps);
    }

    if (lil_streq(op, "while")) {
        if (n->nkids < 3) {
            *err = -16;
            return 0;
        }
        int64_t last = 0;
        for (;;) {
            int64_t c = lil_eval(n->kids[1], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
            if (c == 0) {
                break;
            }
            last = lil_eval(n->kids[2], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
        }
        return last;
    }

    if (lil_streq(op, "repeat")) {
        if (n->nkids < 3) {
            *err = -16;
            return 0;
        }
        int64_t count = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        int64_t last = 0;
        for (int64_t i = 0; i < count; i++) {
            lil_env_t frame;
            frame.parent = env;
            lil_copy_sym(frame.name, (uint32_t)sizeof(frame.name), "it");
            frame.value = i;
            last = lil_eval_body(n, 2, &frame, putc, user, err, steps);
            if (*err) return 0;
        }
        return last;
    }

    if (lil_streq(op, "for")) {
        if (n->nkids < 5) {
            *err = -16;
            return 0;
        }
        lil_node_t* var_n = n->kids[1];
        if (var_n->kind != LIL_SYM) {
            *err = -13;
            return 0;
        }
        int64_t start = lil_eval(n->kids[2], env, putc, user, err, steps);
        if (*err) return 0;
        int64_t end = lil_eval(n->kids[3], env, putc, user, err, steps);
        if (*err) return 0;
        int64_t step = 1;
        uint32_t body_idx = 4;
        if (n->nkids >= 6) {
            step = lil_eval(n->kids[4], env, putc, user, err, steps);
            if (*err) return 0;
            if (step == 0) {
                *err = -23;
                return 0;
            }
            body_idx = 5;
        }
        int64_t last = 0;
        for (int64_t i = start; (step > 0) ? (i < end) : (i > end); i += step) {
            lil_env_t frame;
            frame.parent = env;
            lil_copy_sym(frame.name, (uint32_t)sizeof(frame.name), var_n->sym);
            frame.value = i;
            last = lil_eval_body(n, body_idx, &frame, putc, user, err, steps);
            if (*err) return 0;
        }
        return last;
    }

    if (lil_streq(op, "cond")) {
        if (n->nkids < 2) {
            *err = -16;
            return 0;
        }
        for (uint32_t ci = 1; ci < n->nkids; ci++) {
            lil_node_t* clause = n->kids[ci];
            if (!clause || clause->kind != LIL_LIST || clause->nkids < 2) {
                *err = -19;
                return 0;
            }
            lil_node_t* head = clause->kids[0];
            if (head && head->kind == LIL_SYM && lil_streq(head->sym, "else")) {
                return lil_eval_body(clause, 1, env, putc, user, err, steps);
            }
            int64_t pred = lil_eval(clause->kids[0], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
            if (pred != 0) {
                return lil_eval_body(clause, 1, env, putc, user, err, steps);
            }
        }
        return 0;
    }

    if (lil_streq(op, "min") || lil_streq(op, "max")) {
        if (n->nkids < 2) {
            *err = -16;
            return 0;
        }
        int64_t acc = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        for (uint32_t i = 2; i < n->nkids; i++) {
            int64_t v = lil_eval(n->kids[i], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
            if (lil_streq(op, "min")) {
                if (v < acc) {
                    acc = v;
                }
            } else if (v > acc) {
                acc = v;
            }
        }
        return acc;
    }

    if (lil_streq(op, "clamp") || lil_streq(op, "between") || lil_streq(op, "within")) {
        if (n->nkids != 4) {
            *err = -16;
            return 0;
        }
        int64_t x = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) return 0;
        int64_t lo = lil_eval(n->kids[2], env, putc, user, err, steps);
        if (*err) return 0;
        int64_t hi = lil_eval(n->kids[3], env, putc, user, err, steps);
        if (*err) return 0;
        if (lo > hi) {
            int64_t t = lo;
            lo = hi;
            hi = t;
        }
        if (lil_streq(op, "clamp")) {
            if (x < lo) return lo;
            if (x > hi) return hi;
            return x;
        }
        if (lil_streq(op, "within")) {
            return (x >= lo && x < hi) ? 1 : 0;
        }
        return (x >= lo && x <= hi) ? 1 : 0;
    }

    if (lil_streq(op, "pow")) {
        if (n->nkids != 3) {
            *err = -16;
            return 0;
        }
        int64_t base = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) return 0;
        int64_t exp = lil_eval(n->kids[2], env, putc, user, err, steps);
        if (*err) return 0;
        if (exp < 0) {
            *err = -24;
            return 0;
        }
        int64_t acc = 1;
        while (exp > 0) {
            if (exp & 1) {
                acc *= base;
            }
            exp >>= 1;
            if (exp) {
                base *= base;
            }
        }
        return acc;
    }

    if (lil_streq(op, "gcd") || lil_streq(op, "lcm")) {
        if (n->nkids < 2) {
            *err = -16;
            return 0;
        }
        uint64_t acc = lil_mag(lil_eval(n->kids[1], env, putc, user, err, steps));
        if (*err) return 0;
        for (uint32_t i = 2; i < n->nkids; i++) {
            uint64_t b = lil_mag(lil_eval(n->kids[i], env, putc, user, err, steps));
            if (*err) return 0;
            uint64_t a = acc;
            uint64_t bb = b;
            while (bb != 0) {
                uint64_t t = a % bb;
                a = bb;
                bb = t;
            }
            if (lil_streq(op, "gcd")) {
                acc = a;
            } else {
                acc = (acc == 0 || b == 0) ? 0 : (acc / a) * b;
            }
        }
        return lil_from_mag(acc);
    }

    if (lil_streq(op, "neg")) {
        if (n->nkids != 2) {
            *err = -16;
            return 0;
        }
        return -lil_eval(n->kids[1], env, putc, user, err, steps);
    }

    if (lil_streq(op, "abs")) {
        if (n->nkids != 2) {
            *err = -16;
            return 0;
        }
        int64_t v = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        return v < 0 ? -v : v;
    }

    if (lil_streq(op, "not")) {
        if (n->nkids != 2) {
            *err = -16;
            return 0;
        }
        int64_t v = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        return ~v;
    }

    if (lil_streq(op, "eq") || lil_streq(op, "ne") || lil_streq(op, "lt") || lil_streq(op, "le") ||
        lil_streq(op, "gt") || lil_streq(op, "ge") || lil_streq(op, "and") || lil_streq(op, "or") ||
        lil_streq(op, "xor") || lil_streq(op, "shl") || lil_streq(op, "shr")) {
        if (n->nkids != 3) {
            *err = -16;
            return 0;
        }
        int64_t a = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        int64_t b = lil_eval(n->kids[2], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        if (lil_streq(op, "eq")) {
            return (a == b) ? 1 : 0;
        }
        if (lil_streq(op, "ne")) {
            return (a != b) ? 1 : 0;
        }
        if (lil_streq(op, "lt")) {
            return (a < b) ? 1 : 0;
        }
        if (lil_streq(op, "le")) {
            return (a <= b) ? 1 : 0;
        }
        if (lil_streq(op, "gt")) {
            return (a > b) ? 1 : 0;
        }
        if (lil_streq(op, "ge")) {
            return (a >= b) ? 1 : 0;
        }
        if (lil_streq(op, "and")) {
            return a & b;
        }
        if (lil_streq(op, "or")) {
            return a | b;
        }
        if (lil_streq(op, "xor")) {
            return a ^ b;
        }
        if (lil_streq(op, "shl")) {
            return a << (int)(b & 63);
        }
        return a >> (int)(b & 63);
    }

    if (lil_streq(op, "mod") || lil_streq(op, "%")) {
        if (n->nkids != 3) {
            *err = -16;
            return 0;
        }
        int64_t a = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        int64_t b = lil_eval(n->kids[2], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        if (b == 0) {
            *err = -17;
            return 0;
        }
        return a % b;
    }

    if (lil_streq(op, "/")) {
        if (n->nkids < 2) {
            *err = -16;
            return 0;
        }
        int64_t acc = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        for (uint32_t i = 2; i < n->nkids; i++) {
            int64_t b = lil_eval(n->kids[i], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
            if (b == 0) {
                *err = -17;
                return 0;
            }
            acc /= b;
        }
        return acc;
    }

    if (lil_streq(op, "+")) {
        int64_t acc = 0;
        for (uint32_t i = 1; i < n->nkids; i++) {
            acc += lil_eval(n->kids[i], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
        }
        return acc;
    }

    if (lil_streq(op, "-")) {
        if (n->nkids < 2) {
            *err = -16;
            return 0;
        }
        if (n->nkids == 2) {
            return -lil_eval(n->kids[1], env, putc, user, err, steps);
        }
        int64_t acc = lil_eval(n->kids[1], env, putc, user, err, steps);
        if (*err) {
            return 0;
        }
        for (uint32_t i = 2; i < n->nkids; i++) {
            acc -= lil_eval(n->kids[i], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
        }
        return acc;
    }

    if (lil_streq(op, "*")) {
        int64_t acc = 1;
        for (uint32_t i = 1; i < n->nkids; i++) {
            acc *= lil_eval(n->kids[i], env, putc, user, err, steps);
            if (*err) {
                return 0;
            }
        }
        return acc;
    }

    /* User-defined function call */
    for (int fi = 0; fi < g_nfuncs; fi++) {
        lil_func_t* f = &g_funcs[fi];
        if (!lil_streq(op, f->name)) continue;
        if ((uint32_t)(n->nkids - 1) != f->nparams) {
            *err = -21;
            return 0;
        }
        lil_env_t* frame_chain = 0;
        if (f->nparams > 0) {
            frame_chain = (lil_env_t*)kmalloc((uint32_t)(f->nparams * sizeof(lil_env_t)));
        }
        if (f->nparams > 0 && !frame_chain) {
            *err = -5;
            return 0;
        }
        for (uint32_t pi = 0; pi < f->nparams; pi++) {
            int64_t arg = lil_eval(n->kids[pi + 1], env, putc, user, err, steps);
            if (*err) {
                if (frame_chain) kfree(frame_chain);
                return 0;
            }
            frame_chain[pi].parent = (pi == 0) ? env : &frame_chain[pi - 1];
            lil_copy_sym(frame_chain[pi].name, (uint32_t)sizeof(frame_chain[pi].name), f->params[pi]);
            frame_chain[pi].value = arg;
        }
        lil_env_t* call_env = f->nparams ? &frame_chain[f->nparams - 1] : env;
        int64_t r = lil_eval(f->body, call_env, putc, user, err, steps);
        if (frame_chain) kfree(frame_chain);
        return r;
    }

    *err = -18;
    return 0;
}

int lil_run(const char* src, lil_putc_fn putc, void* user)
{
    if (!src) {
        return 1;
    }
    g_nfuncs = 0;
    const char* p = src;
    skip_ws(&p);
    if (*p == 0) {
        return 2;
    }

    int err = 0;
    lil_node_t* root = parse_expr(&p, &err);
    if (err || !root) {
        if (root) {
            lil_tree_free(root);
        }
        return err ? -err : 3;
    }
    skip_ws(&p);
    if (*p != 0) {
        lil_tree_free(root);
        return 4;
    }

    uint32_t steps = 0;
    err = 0;
    (void)lil_eval(root, 0, putc, user, &err, &steps);
    lil_tree_free(root);
    return err ? -err : 0;
}

static void lil_nop_putc(char c, void* u) { (void)c; (void)u; }

int lil_eval_int(const char* src, int64_t* out)
{
    if (!src || !out) return -1;
    g_nfuncs = 0;
    const char* p = src;
    skip_ws(&p);
    if (*p == 0) return -2;
    int err = 0;
    lil_node_t* root = parse_expr(&p, &err);
    if (err || !root) {
        if (root) lil_tree_free(root);
        return err ? -err : -3;
    }
    skip_ws(&p);
    if (*p != 0) {
        lil_tree_free(root);
        return -4;
    }
    uint32_t steps = 0;
    err = 0;
    *out = lil_eval(root, 0, lil_nop_putc, 0, &err, &steps);
    lil_tree_free(root);
    return err ? -err : 0;
}
