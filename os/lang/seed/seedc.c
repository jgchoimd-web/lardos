/*
 * seedc - Seed language compiler (bootstrap). Compiles .seed to BOSL bytecode.
 * Self-hosting: the compiler can be rewritten in Seed; seedc bootstraps it.
 *
 * Usage: seedc input.seed -o output.bosli
 *
 * Seed: minimal C-like. int main(), putint(n), putchar(c), +-*/%, if/while.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define SRC_CAP 65536
#define CODE_CAP 32768
#define MAX_SYMS 64
#define ID_LEN 32

enum {
    OP_PUSHI = 0x01, OP_PUSHK = 0x03,
    OP_ADD = 0x10, OP_SUB = 0x11, OP_MUL = 0x12, OP_DIV = 0x13, OP_MOD = 0x14, OP_NEG = 0x15,
    OP_LT = 0x42, OP_LE = 0x43, OP_GT = 0x44, OP_GE = 0x45, OP_EQ = 0x40, OP_NE = 0x41,
    OP_PRINT = 0x20, OP_DROP = 0x22,
    OP_JMP = 0x30, OP_JZ = 0x31, OP_JNZ = 0x32, OP_CALL = 0x33, OP_RET = 0x34,
    OP_EMIT = 0x52, OP_AND = 0x16, OP_OR = 0x17, OP_NOT = 0x19,
    OP_HALT = 0xFF,
};

typedef struct { char name[ID_LEN]; int pc; } sym_t;

static char src[SRC_CAP];
static uint32_t src_len, src_pos;
static uint8_t code[CODE_CAP];
static uint32_t code_len;
static sym_t syms[MAX_SYMS];
static int nsyms;
static int32_t kpool[256];
static int nkpool;
static uint32_t fixup_pc[32];
static int fixup_at[32];
static int nfixup;

static void emit(uint8_t b) { if (code_len < CODE_CAP) code[code_len++] = b; }
static void emit_u32(uint32_t v) {
    emit((uint8_t)v); emit((uint8_t)(v>>8)); emit((uint8_t)(v>>16)); emit((uint8_t)(v>>24));
}
static void emit_i32(int32_t v) { emit_u32((uint32_t)v); }
static int kadd(int32_t v) {
    for (int i = 0; i < nkpool; i++) if (kpool[i] == v) return i;
    if (nkpool >= 256) return -1;
    kpool[nkpool] = v;
    return nkpool++;
}
static void push_const(int32_t v) {
    int k = kadd(v);
    if (k >= 0) { emit(OP_PUSHK); emit_u32(k); }
    else { emit(OP_PUSHI); emit_i32(v); }
}

static void skip(void) { while (src_pos < src_len && (src[src_pos] == ' ' || src[src_pos] == '\t' || src[src_pos] == '\n' || src[src_pos] == '\r')) src_pos++; }
static void skip_to_eol(void) { while (src_pos < src_len && src[src_pos] != '\n') src_pos++; if (src_pos < src_len) src_pos++; }
static int peek(void) { skip(); return (src_pos < src_len) ? (unsigned char)src[src_pos] : 0; }
static int getch(void) { skip(); return (src_pos < src_len) ? (unsigned char)src[src_pos++] : 0; }
static int isid(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static int sym_find(const char* n) {
    for (int i = nsyms - 1; i >= 0; i--) if (strcmp(syms[i].name, n) == 0) return i;
    return -1;
}
static void sym_add(const char* n, int pc) {
    if (nsyms >= MAX_SYMS) return;
    strncpy(syms[nsyms].name, n, ID_LEN - 1);
    syms[nsyms].name[ID_LEN-1] = 0;
    syms[nsyms].pc = pc;
    nsyms++;
}

static int parse_num(void) {
    int v = 0, neg = 0;
    if (peek() == '-') { neg = 1; getch(); }
    if (peek() < '0' || peek() > '9') return 0;
    while (peek() >= '0' && peek() <= '9') v = v * 10 + (getch() - '0');
    return neg ? -v : v;
}
static int ident(char* out, int cap) {
    skip();
    int i = 0;
    if ((peek() >= 'a' && peek() <= 'z') || (peek() >= 'A' && peek() <= 'Z') || peek() == '_') {
        while (isid(peek()) && i < cap - 1) out[i++] = getch();
    }
    out[i] = 0;
    return i;
}
static int expect(const char* s) {
    skip();
    size_t n = strlen(s);
    if (src_pos + n > src_len) return 0;
    if (strncmp(&src[src_pos], s, n) != 0) return 0;
    if (src_pos + n < src_len && isid(src[src_pos + n])) return 0;
    src_pos += n;
    return 1;
}

static int expr(void);
static int stmt(void);

static int primary(void) {
    skip();
    if (peek() == '(') {
        getch();
        int r = expr();
        skip();
        if (peek() == ')') getch();
        return r;
    }
    if (peek() == '-') {
        getch();
        if (!primary()) return 0;
        emit(OP_NEG);
        return 1;
    }
    if (peek() == '!') {
        getch();
        if (!primary()) return 0;
        emit(OP_NOT);
        return 1;
    }
    if ((peek() >= '0' && peek() <= '9') || (peek() == '-' && src_pos+1 < src_len && src[src_pos+1] >= '0' && src[src_pos+1] <= '9')) {
        push_const(parse_num());
        return 1;
    }
    char id[ID_LEN];
    if (ident(id, ID_LEN) > 0) {
        skip();
        if (peek() == '(') {
            getch();
            int n = 0;
            skip();
            if (peek() != ')') {
                for (;;) {
                    if (!expr()) break;
                    n++;
                    skip();
                    if (peek() != ',') break;
                    getch();
                }
            }
            skip();
            if (peek() == ')') getch();
            if (strcmp(id, "putint") == 0 && n == 1) {
                emit(OP_PRINT);
                return 1;
            }
            if (strcmp(id, "putchar") == 0 && n == 1) {
                emit(OP_EMIT);
                return 1;
            }
            int si = sym_find(id);
            if (si < 0) return 0;
            emit(OP_CALL);
            fixup_at[nfixup] = code_len;
            fixup_pc[nfixup] = syms[si].pc;
            nfixup++;
            emit_u32(0);
            return 1;
        }
        return 0;
    }
    return 0;
}

static int term(void) {
    if (!primary()) return 0;
    for (;;) {
        skip();
        int c = peek();
        if (c == '*') { getch(); if (!primary()) return 0; emit(OP_MUL); }
        else if (c == '/') { getch(); if (!primary()) return 0; emit(OP_DIV); }
        else if (c == '%') { getch(); if (!primary()) return 0; emit(OP_MOD); }
        else break;
    }
    return 1;
}
static int expr_add(void) {
    if (!term()) return 0;
    for (;;) {
        skip();
        int c = peek();
        if (c == '+') { getch(); if (!term()) return 0; emit(OP_ADD); }
        else if (c == '-') { getch(); if (!term()) return 0; emit(OP_SUB); }
        else break;
    }
    return 1;
}
static int expr_cmp(void) {
    if (!expr_add()) return 0;
    skip();
    if (peek() == '<' && (src_pos+1 >= src_len || src[src_pos+1] != '=')) { getch(); if (!expr_add()) return 0; emit(OP_LT); return 1; }
    if (peek() == '<' && src_pos+1 < src_len && src[src_pos+1] == '=') { getch(); getch(); if (!expr_add()) return 0; emit(OP_LE); return 1; }
    if (peek() == '>' && (src_pos+1 >= src_len || src[src_pos+1] != '=')) { getch(); if (!expr_add()) return 0; emit(OP_GT); return 1; }
    if (peek() == '>' && src_pos+1 < src_len && src[src_pos+1] == '=') { getch(); getch(); if (!expr_add()) return 0; emit(OP_GE); return 1; }
    if (peek() == '=' && src_pos+1 < src_len && src[src_pos+1] == '=') { getch(); getch(); if (!expr_add()) return 0; emit(OP_EQ); return 1; }
    if (peek() == '!' && src_pos+1 < src_len && src[src_pos+1] == '=') { getch(); getch(); if (!expr_add()) return 0; emit(OP_NE); return 1; }
    return 1;
}
static int expr_and(void) {
    if (!expr_cmp()) return 0;
    while (expect("&&")) { if (!expr_cmp()) return 0; emit(OP_AND); }
    return 1;
}
static int expr_or(void) {
    if (!expr_and()) return 0;
    while (expect("||")) { if (!expr_and()) return 0; emit(OP_OR); }
    return 1;
}
static int expr(void) { return expr_or(); }

static int stmt(void) {
    skip();
    if (expect("if")) {
        if (!expect("(") || !expr() || !expect(")")) return 0;
        emit(OP_JZ);
        uint32_t p = code_len;
        emit_u32(0);
        if (!stmt()) return 0;
        uint32_t q = code_len;
        code[p] = (uint8_t)q; code[p+1] = (uint8_t)(q>>8); code[p+2] = (uint8_t)(q>>16); code[p+3] = (uint8_t)(q>>24);
        skip();
        if (expect("else")) {
            emit(OP_JMP);
            uint32_t p2 = code_len;
            emit_u32(0);
            if (!stmt()) return 0;
            uint32_t q2 = code_len;
            code[p2] = (uint8_t)q2; code[p2+1] = (uint8_t)(q2>>8); code[p2+2] = (uint8_t)(q2>>16); code[p2+3] = (uint8_t)(q2>>24);
        }
        return 1;
    }
    if (expect("while")) {
        uint32_t top = code_len;
        if (!expect("(") || !expr() || !expect(")")) return 0;
        emit(OP_JZ);
        uint32_t p = code_len;
        emit_u32(0);
        if (!stmt()) return 0;
        emit(OP_JMP);
        emit_u32(top);
        uint32_t q = code_len;
        code[p] = (uint8_t)q; code[p+1] = (uint8_t)(q>>8); code[p+2] = (uint8_t)(q>>16); code[p+3] = (uint8_t)(q>>24);
        return 1;
    }
    if (expect("return")) {
        skip();
        if (peek() != ';') { if (!expr()) return 0; }
        skip();
        if (peek() == ';') getch();
        emit(OP_RET);
        return 1;
    }
    if (peek() == ';') { getch(); return 1; }
    if (!expr()) return 0;
    skip();
    if (peek() == ';') getch();
    emit(OP_DROP);
    return 1;
}

static int parse_func(void) {
    if (!expect("int")) return 0;
    char name[ID_LEN];
    if (ident(name, ID_LEN) <= 0) return 0;
    if (!expect("(")) return 0;
    skip();
    while (peek() != ')' && peek() != 0) getch();
    if (peek() == ')') getch();
    if (!expect("{")) return 0;
    sym_add(name, code_len);
    while (peek() != '}' && peek() != 0) {
        if (!stmt()) return 0;
    }
    if (peek() == '}') getch();
    emit(OP_RET);
    return 1;
}

int main(int argc, char** argv) {
    const char* in = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out = argv[++i]; }
        else if (argv[i][0] != '-') { in = argv[i]; break; }
    }
    if (!in || !out) { fprintf(stderr, "Usage: seedc input.seed -o output.bosli\n"); return 1; }
    FILE* f = fopen(in, "rb");
    if (!f) { fprintf(stderr, "seedc: cannot open %s\n", in); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz >= (long)SRC_CAP) { fclose(f); return 1; }
    fread(src, 1, (size_t)sz, f);
    src[sz] = '\0';
    src_len = (uint32_t)sz;
    fclose(f);

    nsyms = nkpool = nfixup = 0;
    code_len = src_pos = 0;

    while (peek()) {
        if (peek() == '/' && src_pos+1 < src_len && src[src_pos+1] == '/') { skip_to_eol(); continue; }
        if (!parse_func()) break;
    }

    for (int i = 0; i < nfixup; i++) {
        uint32_t t = (uint32_t)fixup_pc[i];
        int at = fixup_at[i];
        if (at + 4 <= (int)code_len) {
            code[at] = (uint8_t)t; code[at+1] = (uint8_t)(t>>8);
            code[at+2] = (uint8_t)(t>>16); code[at+3] = (uint8_t)(t>>24);
        }
    }

    int main_i = sym_find("main");
    if (main_i < 0) { fprintf(stderr, "seedc: no main()\n"); return 1; }
    uint32_t entry = (uint32_t)syms[main_i].pc;

    uint8_t header[0x14];
    memcpy(header, "BOSL", 4);
    header[4] = 1; header[5] = 0;
    header[6] = 0; header[7] = 0;
    *(uint32_t*)(header + 8) = (uint32_t)nkpool;
    *(uint32_t*)(header + 12) = code_len;
    *(uint32_t*)(header + 16) = entry;

    f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "seedc: cannot write %s\n", out); return 1; }
    fwrite(header, 1, 0x14, f);
    for (int i = 0; i < nkpool; i++) {
        uint32_t t = 2;
        fwrite(&t, 4, 1, f);
        fwrite(&kpool[i], 4, 1, f);
    }
    fwrite(code, 1, code_len, f);
    fclose(f);

    printf("Wrote %s (%u code bytes)\n", out, code_len);
    return 0;
}
