/*
 * lil - LIL (Lard Interpreter Language) host-side interpreter.
 * Replaces lang/lil.py. Matches kernel/lil.c semantics.
 *
 * Usage: lil [file.lil]
 *   or:  echo '(print (+ 1 2))' | lil
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif

#define MAX_STEPS 500000

typedef struct Env {
    struct Env* parent;
    char* name;
    int64_t value;
} Env;

static int skip_ws(const char* s, int i) {
    int n = (int)strlen(s);
    while (i < n) {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') { i++; continue; }
        if (s[i] == ';' || s[i] == '#') { while (i < n && s[i] != '\n') i++; continue; }
        break;
    }
    return i;
}

static int parse_int(const char* s, int i, int64_t* out) {
    int n = (int)strlen(s);
    int start = i;
    int neg = 0;
    if (i < n && s[i] == '-') { neg = 1; i++; }
    if (i + 1 < n && s[i] == '0' && (s[i+1] == 'x' || s[i+1] == 'X')) {
        i += 2;
        int64_t v = 0;
        int any = 0;
        while (i < n) {
            char c = s[i];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            v = (v << 4) + d;
            any = 1;
            i++;
        }
        if (!any) return start;
        *out = neg ? -v : v;
        return i;
    }
    if (i >= n || !isdigit((unsigned char)s[i])) return start;
    int64_t v = 0;
    while (i < n && isdigit((unsigned char)s[i])) {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    *out = neg ? -v : v;
    return i;
}

static int is_sym_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
           c == '<' || c == '>' || c == '=' || c == '!' || c == '?';
}

enum { NODE_NUM, NODE_SYM, NODE_LIST };

typedef struct Node {
    int kind;
    int64_t num;
    char* sym;
    struct Node** list;
    int list_len;
} Node;

static Node* parse_expr(const char* s, int* idx);
static Node* parse_list(const char* s, int* idx);

static Node* parse_expr(const char* s, int* idx) {
    *idx = skip_ws(s, *idx);
    int n = (int)strlen(s);
    if (*idx >= n) return NULL;

    if (s[*idx] == '(') return parse_list(s, idx);

    int64_t v;
    int j = parse_int(s, *idx, &v);
    if (j > *idx) {
        Node* nd = (Node*)calloc(1, sizeof(Node));
        nd->kind = NODE_NUM;
        nd->num = v;
        *idx = j;
        return nd;
    }

    j = *idx;
    while (j < n && is_sym_char(s[j])) j++;
    if (j == *idx) return NULL;
    Node* nd = (Node*)calloc(1, sizeof(Node));
    nd->kind = NODE_SYM;
    size_t symlen = (size_t)(j - *idx);
    nd->sym = (char*)malloc(symlen + 1);
    if (nd->sym) {
        memcpy(nd->sym, s + *idx, symlen);
        nd->sym[symlen] = '\0';
    }
    *idx = j;
    return nd;
}

static Node* parse_list(const char* s, int* idx) {
    *idx = skip_ws(s, *idx);
    if (s[*idx] != '(') return NULL;
    (*idx)++;
    Node** kids = NULL;
    int cap = 0, len = 0;
    while (1) {
        *idx = skip_ws(s, *idx);
        if (*idx >= (int)strlen(s)) break;
        if (s[*idx] == ')') { (*idx)++; break; }
        Node* ch = parse_expr(s, idx);
        if (!ch) break;
        if (len >= cap) {
            cap = cap ? cap * 2 : 8;
            kids = (Node**)realloc(kids, (size_t)cap * sizeof(Node*));
        }
        kids[len++] = ch;
    }
    Node* nd = (Node*)calloc(1, sizeof(Node));
    nd->kind = NODE_LIST;
    nd->list = kids;
    nd->list_len = len;
    return nd;
}

static int64_t eval(Node* node, Env* env, int* steps);

static int64_t eval(Node* node, Env* env, int* steps) {
    (*steps)++;
    if (*steps > MAX_STEPS) return -1;
    if (!node) return 0;

    if (node->kind == NODE_NUM) return node->num;
    if (node->kind == NODE_SYM) {
        Env* e = env;
        while (e) {
            if (strcmp(e->name, node->sym) == 0) return e->value;
            e = e->parent;
        }
        fprintf(stderr, "lil: undefined '%s'\n", node->sym);
        return 0;
    }

    if (node->kind != NODE_LIST || node->list_len == 0) return 0;
    Node* opn = node->list[0];
    if (!opn || opn->kind != NODE_SYM) return 0;
    const char* op = opn->sym;

    if (strcmp(op, "print") == 0) {
        if (node->list_len < 2) return 0;
        int64_t v = eval(node->list[1], env, steps);
        printf("%lld\n", (long long)v);
        return v;
    }
    if (strcmp(op, "rand") == 0) {
        static uint32_t s_r = 0;
        if (s_r == 0) s_r = (uint32_t)time(0) | 1u;
        s_r = (uint32_t)((uint64_t)s_r * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return (int64_t)(s_r & 0x7FFFu);
    }
    if (strcmp(op, "time") == 0) {
        return (int64_t)time(0);
    }
    if (strcmp(op, "let") == 0) {
        if (node->list_len < 4) return 0;
        Node* nm = node->list[1];
        if (!nm || nm->kind != NODE_SYM) return 0;
        int64_t val = eval(node->list[2], env, steps);
        Env nenv = { env, nm->sym, val };
        return eval(node->list[3], &nenv, steps);
    }
    if (strcmp(op, "if") == 0) {
        if (node->list_len < 3) return 0;
        int64_t c = eval(node->list[1], env, steps);
        if (c != 0) return eval(node->list[2], env, steps);
        if (node->list_len >= 4) return eval(node->list[3], env, steps);
        return 0;
    }
    if (strcmp(op, "begin") == 0) {
        if (node->list_len < 2) return 0;
        int64_t r = 0;
        for (int i = 1; i < node->list_len; i++)
            r = eval(node->list[i], env, steps);
        return r;
    }
    if (strcmp(op, "while") == 0) {
        if (node->list_len < 3) return 0;
        int64_t last = 0;
        while (eval(node->list[1], env, steps) != 0)
            last = eval(node->list[2], env, steps);
        return last;
    }
    if (strcmp(op, "cond") == 0) {
        for (int i = 1; i < node->list_len; i++) {
            Node* cl = node->list[i];
            if (!cl || cl->kind != NODE_LIST || cl->list_len < 2) continue;
            if (cl->list[0] && cl->list[0]->kind == NODE_SYM && strcmp(cl->list[0]->sym, "else") == 0)
                return eval(cl->list[1], env, steps);
            if (eval(cl->list[0], env, steps) != 0)
                return eval(cl->list[1], env, steps);
        }
        return 0;
    }
    if (strcmp(op, "min") == 0 || strcmp(op, "max") == 0) {
        if (node->list_len < 2) return 0;
        int64_t acc = eval(node->list[1], env, steps);
        for (int i = 2; i < node->list_len; i++) {
            int64_t v = eval(node->list[i], env, steps);
            acc = (strcmp(op, "min") == 0) ? (v < acc ? v : acc) : (v > acc ? v : acc);
        }
        return acc;
    }
    if (strcmp(op, "neg") == 0) {
        if (node->list_len != 2) return 0;
        return -eval(node->list[1], env, steps);
    }
    if (strcmp(op, "abs") == 0) {
        if (node->list_len != 2) return 0;
        int64_t v = eval(node->list[1], env, steps);
        return v < 0 ? -v : v;
    }
    if (strcmp(op, "not") == 0) {
        if (node->list_len != 2) return 0;
        return ~eval(node->list[1], env, steps);
    }
    if (node->list_len == 3) {
        int64_t a = eval(node->list[1], env, steps);
        int64_t b = eval(node->list[2], env, steps);
        if (strcmp(op, "eq") == 0) return a == b ? 1 : 0;
        if (strcmp(op, "ne") == 0) return a != b ? 1 : 0;
        if (strcmp(op, "lt") == 0) return a < b ? 1 : 0;
        if (strcmp(op, "le") == 0) return a <= b ? 1 : 0;
        if (strcmp(op, "gt") == 0) return a > b ? 1 : 0;
        if (strcmp(op, "ge") == 0) return a >= b ? 1 : 0;
        if (strcmp(op, "and") == 0) return a & b;
        if (strcmp(op, "or") == 0) return a | b;
        if (strcmp(op, "xor") == 0) return a ^ b;
        if (strcmp(op, "shl") == 0) return a << (b & 63);
        if (strcmp(op, "shr") == 0) return a >> (b & 63);
        if (strcmp(op, "mod") == 0 || strcmp(op, "%") == 0) {
            if (b == 0) { fprintf(stderr, "lil: div by zero\n"); return 0; }
            return a % b;
        }
        if (strcmp(op, "+") == 0) return a + b;
        if (strcmp(op, "-") == 0) return a - b;
        if (strcmp(op, "*") == 0) return a * b;
        if (strcmp(op, "/") == 0) {
            if (b == 0) { fprintf(stderr, "lil: div by zero\n"); return 0; }
            return a / b;
        }
    }
    if (strcmp(op, "-") == 0 && node->list_len == 2) return -eval(node->list[1], env, steps);
    if (strcmp(op, "+") == 0) {
        int64_t acc = 0;
        for (int i = 1; i < node->list_len; i++) acc += eval(node->list[i], env, steps);
        return acc;
    }
    if (strcmp(op, "*") == 0) {
        int64_t acc = 1;
        for (int i = 1; i < node->list_len; i++) acc *= eval(node->list[i], env, steps);
        return acc;
    }
    if (strcmp(op, "/") == 0 && node->list_len >= 2) {
        int64_t acc = eval(node->list[1], env, steps);
        for (int i = 2; i < node->list_len; i++) {
            int64_t b = eval(node->list[i], env, steps);
            if (b == 0) { fprintf(stderr, "lil: div by zero\n"); return 0; }
            acc /= b;
        }
        return acc;
    }

    fprintf(stderr, "lil: unknown op %s\n", op);
    return 0;
}

static void free_node(Node* n) {
    if (!n) return;
    if (n->kind == NODE_SYM) free(n->sym);
    if (n->kind == NODE_LIST) {
        for (int i = 0; i < n->list_len; i++) free_node(n->list[i]);
        free(n->list);
    }
    free(n);
}

int main(int argc, char** argv) {
    char buf[65536];
    size_t len = 0;

    if (argc >= 2) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "lil: cannot open %s\n", argv[1]);
            return 1;
        }
        len = fread(buf, 1, sizeof(buf) - 1, f);
        buf[len] = '\0';
        fclose(f);
    } else {
        while (len < sizeof(buf) - 1) {
            int c = getchar();
            if (c == EOF) break;
            buf[len++] = (char)c;
        }
        buf[len] = '\0';
    }

    int idx = skip_ws(buf, 0);
    if (idx >= (int)len) return 0;

    int i = idx;
    Node* node = parse_expr(buf, &i);
    if (!node) {
        fprintf(stderr, "lil: parse error\n");
        return 1;
    }
    i = skip_ws(buf, i);
    if (i < (int)len) {
        fprintf(stderr, "lil: trailing junk\n");
        free_node(node);
        return 1;
    }

    int steps = 0;
    int64_t v = eval(node, NULL, &steps);
    free_node(node);

    if (steps > MAX_STEPS) {
        fprintf(stderr, "lil: step limit\n");
        return 1;
    }
    if (isatty(1)) printf("%lld\n", (long long)v);
    return 0;
}
