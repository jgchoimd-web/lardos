/*
 * bosl_asm - In-kernel BOSL assembler for inline BOSL (like C __asm__).
 * Assembles BOSL source string to bytecode, then runs it.
 */
#include "bosl_vm.h"
#include "console.h"
#include "mem.h"
#include "string.h"

#include <stdint.h>

/* Minimal helpers (kernel has no strcmp/strchr/strtol/strdup) */
static int bosl_strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static char* bosl_strchr(const char* s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return *s == (char)c ? (char*)s : 0;
}

static long bosl_strtol(const char* s, char** end, int base)
{
    const char* p = s;
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else base = 10;
    }
    unsigned long v = 0;
    for (;;) {
        int d = *p - '0';
        if (d < 0) break;
        if (d > 9) { d = (*p | 32) - 'a' + 10; if (d < 10 || d >= base) break; }
        v = v * (unsigned)base + (unsigned)d;
        p++;
    }
    if (end) *end = (char*)p;
    return neg ? -(long)v : (long)v;
}

static char* bosl_strdup(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    char* r = (char*)kmalloc((uint32_t)(n + 1));
    if (!r) return 0;
    for (size_t i = 0; i <= n; i++) r[i] = s[i];
    return r;
}

#define MAGIC "BOSL"
#define VERSION 1
#define MAX_LABELS 64
#define MAX_FIXUPS 128
#define MAX_CONST 64
#define CODE_CAP 4096
#define LINE_BUF 256

enum {
    OP_PUSHI = 0x01, OP_PUSHC = 0x02, OP_PUSHK = 0x03, OP_PUSHI64 = 0x04, OP_PUSHK64 = 0x05,
    OP_ADD = 0x10, OP_SUB = 0x11, OP_MUL = 0x12, OP_DIV = 0x13, OP_MOD = 0x14, OP_NEG = 0x15,
    OP_AND = 0x16, OP_OR = 0x17, OP_XOR = 0x18, OP_NOT = 0x19,
    OP_SHL = 0x1A, OP_SHR = 0x1B, OP_USHR = 0x1C, OP_ROL = 0x1D, OP_ROR = 0x1E,
    OP_PRINT = 0x20, OP_DUP = 0x21, OP_DROP = 0x22, OP_SWAP = 0x23, OP_OVER = 0x24,
    OP_PICK = 0x25, OP_I32TOI64 = 0x26, OP_I64TOI32 = 0x27,
    OP_ROT = 0x28, OP_NIP = 0x29, OP_TUCK = 0x2A, OP_DEPTH = 0x2B,
    OP_PRINTN = 0x2C, OP_2DUP = 0x2D, OP_2DROP = 0x2E, OP_NOP = 0x2F,
    OP_JMP = 0x30, OP_JZ = 0x31, OP_JNZ = 0x32, OP_CALL = 0x33, OP_RET = 0x34,
    OP_EQ = 0x40, OP_NE = 0x41, OP_LT = 0x42, OP_LE = 0x43, OP_GT = 0x44, OP_GE = 0x45,
    OP_ULT = 0x46, OP_ULE = 0x47, OP_UGT = 0x48, OP_UGE = 0x49,     OP_UDIV = 0x4A, OP_UMOD = 0x4B,
    OP_MIN = 0x4C, OP_MAX = 0x4D, OP_ABS = 0x4E, OP_SGN = 0x4F,
    OP_INC = 0x50, OP_DEC = 0x51, OP_EMIT = 0x52, OP_QDUP = 0x53,
    OP_SLEN = 0x78, OP_WITHIN = 0x79, OP_2SWAP = 0x7A, OP_RAND = 0x7B, OP_TIME = 0x7C,
    OP_LIPC_SEND = 0x7D, OP_LIPC_RECV = 0x7E, OP_LIPC_PENDING = 0x7F,
    OP_LIPC_SEND_STR = 0x90,
    OP_HALT = 0xFF,
};

typedef struct { char* name; uint32_t pc; } label_t;
typedef struct { char* name; uint32_t at; } fixup_t;
typedef struct { int kind; union { int64_t i; uint8_t* s; size_t len; } u; } constant_t;

static label_t labels[MAX_LABELS];
static int nlabels;
static fixup_t fixups[MAX_FIXUPS];
static int nfixups;
static constant_t constants[MAX_CONST];
static int nconst;
static uint8_t code[CODE_CAP];
static uint32_t code_len;

static void emit_u32(uint32_t v)
{
    if (code_len + 4 <= CODE_CAP) {
        code[code_len++] = (uint8_t)v;
        code[code_len++] = (uint8_t)(v >> 8);
        code[code_len++] = (uint8_t)(v >> 16);
        code[code_len++] = (uint8_t)(v >> 24);
    }
}

static void emit_i32(int32_t v) { emit_u32((uint32_t)v); }
static void emit_i64(int64_t v) {
    emit_u32((uint32_t)(v & 0xFFFFFFFFu));
    emit_u32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static int is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int parse_int(const char* s, long* out)
{
    char* end;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        *out = bosl_strtol(s + 2, &end, 16);
        return end > s + 2;
    }
    *out = bosl_strtol(s, &end, 10);
    return end > s;
}

static int parse_str(const char* s, char* out, size_t cap, size_t* len)
{
    if (s[0] != '"') return -1;
    size_t i = 0, j = 1;
    while (s[j] && s[j] != '"') {
        if (s[j] == '\\') {
            j++;
            if (s[j] == 'n') out[i++] = '\n';
            else if (s[j] == 't') out[i++] = '\t';
            else if (s[j] == '"') out[i++] = '"';
            else if (s[j] == '\\') out[i++] = '\\';
            else return -1;
            j++;
        } else {
            if (i >= cap) return -1;
            out[i++] = (char)s[j++];
        }
    }
    if (s[j] != '"') return -1;
    *len = i;
    return 0;
}

static int intern_str(const uint8_t* b, size_t len)
{
    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 1 && constants[i].u.len == len &&
            memcmp(constants[i].u.s, b, len) == 0)
            return i;
    }
    if (nconst >= MAX_CONST) return -1;
    uint8_t* copy = (uint8_t*)kmalloc((uint32_t)len);
    if (!copy) return -1;
    memcpy(copy, b, len);
    constants[nconst].kind = 1;
    constants[nconst].u.s = copy;
    constants[nconst].u.len = len;
    return nconst++;
}

static int intern_int(int32_t v)
{
    for (int i = 0; i < nconst; i++)
        if (constants[i].kind == 2 && constants[i].u.i == v) return i;
    if (nconst >= MAX_CONST) return -1;
    constants[nconst].kind = 2;
    constants[nconst].u.i = v;
    return nconst++;
}

static int intern_i64(int64_t v)
{
    for (int i = 0; i < nconst; i++)
        if (constants[i].kind == 3 && constants[i].u.i == v) return i;
    if (nconst >= MAX_CONST) return -1;
    constants[nconst].kind = 3;
    constants[nconst].u.i = v;
    return nconst++;
}

static void strip_comment(char* line)
{
    int in_str = 0;
    for (char* p = line; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == '"') {
                in_str = 0;
            }
            continue;
        }
        if (*p == '"') {
            in_str = 1;
            continue;
        }
        if (*p == ';' || *p == '#') {
            *p = '\0';
            return;
        }
    }
}

static void rstrip_line(char* line)
{
    size_t n = 0;
    while (line[n]) {
        n++;
    }
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
}

static int assemble_line(char* line, int lineno);

static int assemble_source(char* src, int* lineno_ref)
{
    char* p = src;
    char line[LINE_BUF];
    while (*p) {
        int i = 0;
        while (*p && *p != '\n' && i < LINE_BUF - 1) line[i++] = *p++;
        line[i] = '\0';
        if (*p == '\n') p++;
        (*lineno_ref)++;
        strip_comment(line);
        rstrip_line(line);
        for (i = 0; line[i] == ' ' || line[i] == '\t'; i++);
        if (!line[i]) continue;
        if (assemble_line(line + i, *lineno_ref) != 0) return 1;
    }
    return 0;
}

#define ARG() (arg[0] ? arg : "")
#define EMIT(op) do { if (code_len < CODE_CAP) code[code_len++] = (op); } while(0)

static int assemble_line(char* line, int lineno)
{
    if (line[0] == '\0') return 0;
    char* arg = bosl_strchr(line, ' ');
    if (arg) { *arg++ = '\0'; while (*arg == ' ' || *arg == '\t') arg++; }

    size_t ll = 0;
    while (line[ll]) ll++;
    if (ll > 0 && line[ll - 1] == ':') {
        line[ll - 1] = '\0';
        for (char* p = line; *p; p++)
            if (!is_ident(*p)) { kprintf("bosl_asm:%d: invalid label\n", lineno); return 1; }
        for (int i = 0; i < nlabels; i++)
            if (bosl_strcmp(labels[i].name, line) == 0) { kprintf("bosl_asm:%d: duplicate label\n", lineno); return 1; }
        if (nlabels >= MAX_LABELS) return 1;
        labels[nlabels].name = bosl_strdup(line);
        if (!labels[nlabels].name) return 1;
        labels[nlabels].pc = code_len;
        nlabels++;
        return 0;
    }

    for (char* p = line; *p; p++) *p = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);

    if (bosl_strcmp(line, "pushi") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("bosl_asm:%d: pushi needs int\n", lineno); return 1; }
        EMIT(OP_PUSHI); emit_i32((int32_t)v);
    } else if (bosl_strcmp(line, "pushi64") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("bosl_asm:%d: pushi64 needs int\n", lineno); return 1; }
        EMIT(OP_PUSHI64); emit_i64((int64_t)v);
    } else if (bosl_strcmp(line, "pushc") == 0) {
        char buf[512]; size_t len;
        if (parse_str(ARG(), buf, sizeof(buf), &len) != 0) { kprintf("bosl_asm:%d: pushc needs \"str\"\n", lineno); return 1; }
        int idx = intern_str((uint8_t*)buf, len);
        if (idx < 0) return 1;
        EMIT(OP_PUSHC); emit_u32((uint32_t)idx);
    } else if (bosl_strcmp(line, "pushk") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("bosl_asm:%d: pushk needs int\n", lineno); return 1; }
        int idx = intern_int((int32_t)v);
        if (idx < 0) return 1;
        EMIT(OP_PUSHK); emit_u32((uint32_t)idx);
    } else if (bosl_strcmp(line, "pushk64") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("bosl_asm:%d: pushk64 needs int\n", lineno); return 1; }
        int idx = intern_i64((int64_t)v);
        if (idx < 0) return 1;
        EMIT(OP_PUSHK64); emit_u32((uint32_t)idx);
    } else if (bosl_strcmp(line, "pick") == 0) {
        long v; if (!parse_int(ARG(), &v) || v < 0 || v > 255) { kprintf("bosl_asm:%d: pick 0-255\n", lineno); return 1; }
        EMIT(OP_PICK); if (code_len < CODE_CAP) code[code_len++] = (uint8_t)v;
    } else if (bosl_strcmp(line, "jmp") == 0 || bosl_strcmp(line, "jz") == 0 || bosl_strcmp(line, "jnz") == 0 || bosl_strcmp(line, "call") == 0) {
        if (!ARG() || !ARG()[0]) { kprintf("bosl_asm:%d: label required\n", lineno); return 1; }
        for (char* p = ARG(); *p; p++) if (!is_ident(*p)) { kprintf("bosl_asm:%d: bad label\n", lineno); return 1; }
        uint8_t op = (bosl_strcmp(line, "jmp") == 0) ? OP_JMP : (bosl_strcmp(line, "jz") == 0) ? OP_JZ : (bosl_strcmp(line, "jnz") == 0) ? OP_JNZ : OP_CALL;
        EMIT(op);
        if (nfixups >= MAX_FIXUPS) return 1;
        fixups[nfixups].name = bosl_strdup(ARG());
        if (!fixups[nfixups].name) return 1;
        fixups[nfixups].at = code_len;
        nfixups++;
        emit_u32(0);
    } else {
        uint8_t op = 0;
        if (bosl_strcmp(line, "add") == 0) op = OP_ADD;
        else if (bosl_strcmp(line, "sub") == 0) op = OP_SUB;
        else if (bosl_strcmp(line, "mul") == 0) op = OP_MUL;
        else if (bosl_strcmp(line, "div") == 0) op = OP_DIV;
        else if (bosl_strcmp(line, "mod") == 0) op = OP_MOD;
        else if (bosl_strcmp(line, "neg") == 0) op = OP_NEG;
        else if (bosl_strcmp(line, "and") == 0) op = OP_AND;
        else if (bosl_strcmp(line, "or") == 0) op = OP_OR;
        else if (bosl_strcmp(line, "xor") == 0) op = OP_XOR;
        else if (bosl_strcmp(line, "not") == 0) op = OP_NOT;
        else if (bosl_strcmp(line, "shl") == 0) op = OP_SHL;
        else if (bosl_strcmp(line, "shr") == 0) op = OP_SHR;
        else if (bosl_strcmp(line, "ushr") == 0) op = OP_USHR;
        else if (bosl_strcmp(line, "rol") == 0) op = OP_ROL;
        else if (bosl_strcmp(line, "ror") == 0) op = OP_ROR;
        else if (bosl_strcmp(line, "print") == 0) op = OP_PRINT;
        else if (bosl_strcmp(line, "dup") == 0) op = OP_DUP;
        else if (bosl_strcmp(line, "drop") == 0) op = OP_DROP;
        else if (bosl_strcmp(line, "swap") == 0) op = OP_SWAP;
        else if (bosl_strcmp(line, "over") == 0) op = OP_OVER;
        else if (bosl_strcmp(line, "i32toi64") == 0) op = OP_I32TOI64;
        else if (bosl_strcmp(line, "i64toi32") == 0) op = OP_I64TOI32;
        else if (bosl_strcmp(line, "rot") == 0) op = OP_ROT;
        else if (bosl_strcmp(line, "nip") == 0) op = OP_NIP;
        else if (bosl_strcmp(line, "tuck") == 0) op = OP_TUCK;
        else if (bosl_strcmp(line, "depth") == 0) op = OP_DEPTH;
        else if (bosl_strcmp(line, "printn") == 0) op = OP_PRINTN;
        else if (bosl_strcmp(line, "2dup") == 0) op = OP_2DUP;
        else if (bosl_strcmp(line, "2drop") == 0) op = OP_2DROP;
        else if (bosl_strcmp(line, "nop") == 0) op = OP_NOP;
        else if (bosl_strcmp(line, "ret") == 0) op = OP_RET;
        else if (bosl_strcmp(line, "eq") == 0) op = OP_EQ;
        else if (bosl_strcmp(line, "ne") == 0) op = OP_NE;
        else if (bosl_strcmp(line, "lt") == 0) op = OP_LT;
        else if (bosl_strcmp(line, "le") == 0) op = OP_LE;
        else if (bosl_strcmp(line, "gt") == 0) op = OP_GT;
        else if (bosl_strcmp(line, "ge") == 0) op = OP_GE;
        else if (bosl_strcmp(line, "ult") == 0) op = OP_ULT;
        else if (bosl_strcmp(line, "ule") == 0) op = OP_ULE;
        else if (bosl_strcmp(line, "ugt") == 0) op = OP_UGT;
        else if (bosl_strcmp(line, "uge") == 0) op = OP_UGE;
        else if (bosl_strcmp(line, "udiv") == 0) op = OP_UDIV;
        else if (bosl_strcmp(line, "umod") == 0) op = OP_UMOD;
        else if (bosl_strcmp(line, "min") == 0) op = OP_MIN;
        else if (bosl_strcmp(line, "max") == 0) op = OP_MAX;
        else if (bosl_strcmp(line, "abs") == 0) op = OP_ABS;
        else if (bosl_strcmp(line, "sgn") == 0) op = OP_SGN;
        else if (bosl_strcmp(line, "inc") == 0) op = OP_INC;
        else if (bosl_strcmp(line, "dec") == 0) op = OP_DEC;
        else if (bosl_strcmp(line, "emit") == 0) op = OP_EMIT;
        else if (bosl_strcmp(line, "?dup") == 0) op = OP_QDUP;
        else if (bosl_strcmp(line, "slen") == 0) op = OP_SLEN;
        else if (bosl_strcmp(line, "within") == 0) op = OP_WITHIN;
        else if (bosl_strcmp(line, "2swap") == 0) op = OP_2SWAP;
        else if (bosl_strcmp(line, "rand") == 0) op = OP_RAND;
        else if (bosl_strcmp(line, "time") == 0) op = OP_TIME;
        else if (bosl_strcmp(line, "lipc_send") == 0) op = OP_LIPC_SEND;
        else if (bosl_strcmp(line, "lipc_recv") == 0) op = OP_LIPC_RECV;
        else if (bosl_strcmp(line, "lipc_pending") == 0) op = OP_LIPC_PENDING;
        else if (bosl_strcmp(line, "lipc_send_str") == 0) op = OP_LIPC_SEND_STR;
        else if (bosl_strcmp(line, "halt") == 0) op = OP_HALT;
        else { kprintf("bosl_asm:%d: unknown op '%s'\n", lineno, line); return 1; }
        EMIT(op);
    }
    return 0;
}

/* Build BOSL image from assembled code. Caller must kfree the result. */
static uint8_t* build_image(uint32_t* out_size)
{
    for (int i = 0; i < nfixups; i++) {
        int found = 0;
        for (int j = 0; j < nlabels; j++) {
            if (bosl_strcmp(fixups[i].name, labels[j].name) == 0) {
                uint32_t v = labels[j].pc;
                uint32_t at = fixups[i].at;
                if (at + 4 <= code_len) {
                    code[at] = (uint8_t)v;
                    code[at + 1] = (uint8_t)(v >> 8);
                    code[at + 2] = (uint8_t)(v >> 16);
                    code[at + 3] = (uint8_t)(v >> 24);
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            kprintf("bosl_asm: undefined label '%s'\n", fixups[i].name);
            return 0;
        }
    }

    uint32_t hdr = 0x14;
    uint32_t const_sz = 0;
    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 1) const_sz += 4 + 4 + (uint32_t)constants[i].u.len;
        else if (constants[i].kind == 2) const_sz += 4 + 4;
        else const_sz += 4 + 8;
    }
    uint32_t total = hdr + const_sz + code_len;
    uint8_t* img = (uint8_t*)kmalloc(total);
    if (!img) return 0;

    uint8_t* p = img;
    memcpy(p, MAGIC, 4); p += 4;
    *p++ = (uint8_t)(VERSION); *p++ = (uint8_t)(VERSION >> 8);
    *p++ = 0; *p++ = 0;
    *p++ = (uint8_t)nconst; *p++ = (uint8_t)(nconst >> 8); *p++ = (uint8_t)(nconst >> 16); *p++ = (uint8_t)(nconst >> 24);
    *p++ = (uint8_t)code_len; *p++ = (uint8_t)(code_len >> 8); *p++ = (uint8_t)(code_len >> 16); *p++ = (uint8_t)(code_len >> 24);
    *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;

    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 1) {
            uint32_t t = 1, len = (uint32_t)constants[i].u.len;
            *p++ = (uint8_t)t; *p++ = (uint8_t)(t >> 8); *p++ = (uint8_t)(t >> 16); *p++ = (uint8_t)(t >> 24);
            *p++ = (uint8_t)len; *p++ = (uint8_t)(len >> 8); *p++ = (uint8_t)(len >> 16); *p++ = (uint8_t)(len >> 24);
            memcpy(p, constants[i].u.s, len); p += len;
        } else if (constants[i].kind == 2) {
            uint32_t t = 2;
            int32_t v = (int32_t)constants[i].u.i;
            *p++ = (uint8_t)t; *p++ = (uint8_t)(t >> 8); *p++ = (uint8_t)(t >> 16); *p++ = (uint8_t)(t >> 24);
            *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8); *p++ = (uint8_t)(v >> 16); *p++ = (uint8_t)(v >> 24);
        } else {
            uint32_t t = 3;
            int64_t v = constants[i].u.i;
            *p++ = (uint8_t)t; *p++ = (uint8_t)(t >> 8); *p++ = (uint8_t)(t >> 16); *p++ = (uint8_t)(t >> 24);
            *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8); *p++ = (uint8_t)(v >> 16); *p++ = (uint8_t)(v >> 24);
            *p++ = (uint8_t)(v >> 32); *p++ = (uint8_t)(v >> 40); *p++ = (uint8_t)(v >> 48); *p++ = (uint8_t)(v >> 56);
        }
    }
    memcpy(p, code, code_len);
    *out_size = total;
    return img;
}

static void cleanup(void)
{
    for (int i = 0; i < nconst; i++)
        if (constants[i].kind == 1 && constants[i].u.s) kfree(constants[i].u.s);
    for (int i = 0; i < nlabels; i++)
        if (labels[i].name) kfree(labels[i].name);
    for (int i = 0; i < nfixups; i++)
        if (fixups[i].name) kfree(fixups[i].name);
}

/*
 * bosl_asm_eval - Assemble and run inline BOSL (like __asm__).
 * src: BOSL source string (must include halt).
 * putc, user: optional output callback (0 = kernel console).
 * Returns 0 on success, non-zero on error.
 */
int bosl_asm_eval(const char* src, bosl_putc_fn putc, void* user)
{
    if (!src) return -1;
    size_t len = 0;
    while (src[len]) len++;
    if (len == 0) return 0;

    char* src_copy = (char*)kmalloc((uint32_t)(len + 1));
    if (!src_copy) return -1;
    memcpy(src_copy, src, len + 1);

    nlabels = nfixups = nconst = 0;
    code_len = 0;

    int lineno = 1;
    int err = assemble_source(src_copy, &lineno);
    kfree(src_copy);
    if (err) { cleanup(); return -2; }

    uint32_t img_size = 0;
    uint8_t* img = build_image(&img_size);
    cleanup();
    if (!img) return -1;

    int rc = bosl_vm_run_io(img, img_size, putc, user);
    kfree(img);
    return rc;
}
