/*
 * KR-BASIC - 한글 플레이그라운드 언어 (재미용, 비전공자 친화)
 * 키워드: 출력, 변수, 반복, 부터, 번, 끝, 뿅, 하트
 */
#include "kr_basic.h"
#include <stdint.h>

#define KR_MAX_VARS  16
#define KR_VAR_LEN  12
#define KR_MAX_STACK 32
#define KR_MAX_DEPTH 8

typedef struct {
    char name[KR_VAR_LEN];
    int32_t val;
} kr_var_t;

static kr_var_t s_vars[KR_MAX_VARS];
static int s_nvars;
static char* s_out;
static unsigned s_out_cap;
static unsigned s_out_len;
static const char* s_src;
static const char* s_p;

static void skip_sp(void)
{
    while (*s_p == ' ' || *s_p == '\t' || *s_p == '\n' || *s_p == '\r') s_p++;
}

static void out_add(const char* s)
{
    while (*s && s_out_len + 1 < s_out_cap) s_out[s_out_len++] = *s++;
    s_out[s_out_len] = '\0';
}

static void out_add_num(int32_t n)
{
    char buf[16];
    int i = 0;
    if (n < 0) { out_add("-"); n = -n; }
    if (n == 0) { out_add("0"); return; }
    while (n && i < 14) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) {
        if (s_out_len + 1 < s_out_cap) s_out[s_out_len++] = buf[i];
    }
    s_out[s_out_len] = '\0';
}

static int match_str(const char* kw)
{
    const char* q = s_p;
    while (*kw && *q && (unsigned char)*kw == (unsigned char)*q) { kw++; q++; }
    if (*kw) return 0;
    s_p = q;
    return 1;
}

static int32_t eval_expr(void);

static int32_t eval_primary(void)
{
    skip_sp();
    if (*s_p >= '0' && *s_p <= '9') {
        int32_t v = 0;
        while (*s_p >= '0' && *s_p <= '9') v = v * 10 + (*s_p++ - '0');
        return v;
    }
    if (*s_p == '(') {
        s_p++;
        int32_t v = eval_expr();
        skip_sp();
        if (*s_p == ')') s_p++;
        return v;
    }
    /* variable */
    char name[KR_VAR_LEN];
    int ni = 0;
    while (((*s_p >= 'a' && *s_p <= 'z') || (*s_p >= 'A' && *s_p <= 'Z') ||
            (*s_p >= '0' && *s_p <= '9') || ((unsigned char)*s_p >= 0x80)) && ni < KR_VAR_LEN - 1)
        name[ni++] = *s_p++;
    name[ni] = '\0';
    for (int i = 0; i < s_nvars; i++) {
        int j = 0;
        while (name[j] && s_vars[i].name[j] && name[j] == s_vars[i].name[j]) j++;
        if (!name[j] && !s_vars[i].name[j]) return s_vars[i].val;
    }
    return 0;
}

static int32_t eval_expr(void)
{
    int32_t a = eval_primary();
    skip_sp();
    while (*s_p == '+' || *s_p == '-' || *s_p == '*' || *s_p == '/') {
        char op = *s_p++;
        skip_sp();
        int32_t b = eval_primary();
        if (op == '+') a += b;
        else if (op == '-') a -= b;
        else if (op == '*') a *= b;
        else if (op == '/') a = (b != 0) ? (a / b) : 0;
        skip_sp();
    }
    return a;
}

static void parse_ident(char* out, int cap)
{
    int i = 0;
    skip_sp();
    while ((*s_p >= 'a' && *s_p <= 'z') || (*s_p >= 'A' && *s_p <= 'Z') ||
           (*s_p >= '0' && *s_p <= '9') || (*s_p >= 0x80)) {
        if (i + 1 < cap) out[i++] = *s_p++;
        else s_p++;
    }
    out[i] = '\0';
}

static int run_line(void)
{
    skip_sp();
    if (!*s_p) return 0;

    /* 출력 "..." or 출력 expr */
    if (match_str("\xec\x9d\x9c\xeb\xa0\xa5") || match_str("print")) {
        skip_sp();
        if (*s_p == '"' || *s_p == '\'') {
            char q = *s_p++;
            while (*s_p && *s_p != q) {
                if (s_out_len + 1 < s_out_cap) s_out[s_out_len++] = *s_p;
                s_p++;
            }
            if (*s_p) s_p++;
            if (s_out_len + 1 < s_out_cap) s_out[s_out_len++] = '\n';
            s_out[s_out_len] = '\0';
        } else {
            int32_t v = eval_expr();
            out_add_num(v);
            out_add("\n");
        }
        return 1;
    }

    /* 뿅 - 재미 */
    if (match_str("\xeb\xbf\x85") || match_str("poop")) {
        out_add("\xeb\xbf\x85!\n");
        return 1;
    }

    /* 하트 */
    if (match_str("\xed\x95\x98\xed\x8a\xb8") || match_str("heart")) {
        out_add("<3\n");
        return 1;
    }

    /* 변수 이름 = expr */
    if (match_str("\xeb\xbc\x84\xec\x88\x98") || match_str("let")) {
        skip_sp();
        char name[KR_VAR_LEN];
        parse_ident(name, (int)sizeof(name));
        skip_sp();
        if (*s_p != '=') return -1;
        s_p++;
        skip_sp();
        int32_t v = eval_expr();
        int i;
        for (i = 0; i < s_nvars; i++) {
            int j = 0;
            while (name[j] && s_vars[i].name[j] && name[j] == s_vars[i].name[j]) j++;
            if (!name[j] && !s_vars[i].name[j]) { s_vars[i].val = v; return 1; }
        }
        if (s_nvars < KR_MAX_VARS) {
            i = 0;
            while (name[i] && i < KR_VAR_LEN - 1) { s_vars[s_nvars].name[i] = name[i]; i++; }
            s_vars[s_nvars].name[i] = '\0';
            s_vars[s_nvars].val = v;
            s_nvars++;
        }
        return 1;
    }

    /* 반복 N번 ... 끝 */
    if (match_str("\xeb\xb0\x98\xeb\xb3\xb5") || match_str("repeat")) {
        skip_sp();
        int32_t n = eval_expr();
        if (n < 0 || n > 1000) n = 0;
        skip_sp();
        match_str("\xeb\xb2\x88");   /* 번 */
        match_str("x");              /* 5x = 5번 */
        skip_sp();
        const char* loop_start = s_p;
        for (int32_t i = 0; i < n; i++) {
            s_p = loop_start;
            if (run_line() < 0) return -1;
        }
        while (*s_p) {
            skip_sp();
            if (match_str("\xeb\x81\x9d") || match_str("end")) return 1;
            s_p++;
        }
        return 1;
    }

    /* 끝 - skip (end of block) */
    if (match_str("\xeb\x81\x9d") || match_str("end")) return 0;

    return -1;  /* unknown */
}

int kr_basic_run(const char* src, char* out, unsigned out_cap)
{
    s_src = src;
    s_p = src;
    s_out = out;
    s_out_cap = out_cap;
    s_out_len = 0;
    s_nvars = 0;
    if (out_cap > 0) out[0] = '\0';

    while (*s_p) {
        int r = run_line();
        if (r < 0) {
            out_add("?? \n");
            s_p++;
        } else if (r == 0) {
            skip_sp();
            if (!*s_p) break;
            s_p++;
        }
    }
    return 0;
}
