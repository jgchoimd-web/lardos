/*
 * GASM (Gasing Machine) VM: accumulator-based, object-oriented bytecode.
 * Bytecode: 9 bytes per instruction (op + 8-byte payload).
 * OOP: objects with slots, self register, call/ret, invoke (method call).
 */
#include "gasm_vm.h"
#include "console.h"
#include "mem.h"
#include "string.h"

#include <stdint.h>

enum {
    GASM_LOAD = 0, GASM_ADD = 1, GASM_SUB = 2, GASM_MUL = 3, GASM_DIV = 4,
    GASM_PRINT = 5, GASM_PRINTN = 6, GASM_HALT = 7,
    GASM_JMP = 8, GASM_JZ = 9, GASM_JNZ = 10, GASM_NOP = 11,
    GASM_NEW = 12, GASM_GET = 13, GASM_SET = 14,
    GASM_GETSELF = 15, GASM_SETSELF = 16, GASM_CALL = 17, GASM_RET = 18,
    GASM_INVOKE = 19, GASM_ADDSELF = 20,
    GASM_LT = 21, GASM_LE = 22, GASM_GT = 23, GASM_GE = 24, GASM_EQ = 25, GASM_NE = 26,
};

#define INSN_SZ 9
#define MAX_OBJS 16
#define MAX_SLOTS 8
#define MAX_RET 16

static int pc_valid(uint32_t pc, uint32_t size)
{
    return pc < size && (pc % INSN_SZ) == 0 && pc + INSN_SZ <= size;
}

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t rd_i64(const uint8_t* p)
{
    uint64_t lo = (uint64_t)rd_u32(p);
    uint64_t hi = (uint64_t)rd_u32(p + 4);
    return (int64_t)(lo | (hi << 32));
}

static void gasm_putc(gasm_putc_fn putc, void* user, char c)
{
    if (putc) putc(c, user);
    else console_putc(c);
}

static void gasm_print_i64(gasm_putc_fn putc, void* user, int64_t v)
{
    if (v == 0) {
        gasm_putc(putc, user, '0');
        return;
    }
    char buf[24];
    uint32_t n = 0;
    int neg = 0;
    uint64_t mag;
    if (v < 0) {
        neg = 1;
        mag = (uint64_t)(-(v + 1)) + 1u;
    } else {
        mag = (uint64_t)v;
    }
    while (mag && n < sizeof(buf)) {
        buf[n++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    }
    if (neg) gasm_putc(putc, user, '-');
    while (n > 0) gasm_putc(putc, user, buf[--n]);
}

int gasm_vm_run_io(const uint8_t* code, uint32_t size, gasm_putc_fn putc, void* user)
{
    if (!code || size == 0) return -1;
    int64_t A = 0;
    uint32_t pc = 0;
    uint32_t S = MAX_OBJS;  /* self (invalid until invoke) */
    uint32_t ret_stack[MAX_RET];
    int ret_sp = 0;

    /* Object storage: obj[handle][slot] = int64. obj_used[h]=1 if allocated. */
    static int64_t obj[MAX_OBJS][MAX_SLOTS];
    static uint8_t obj_used[MAX_OBJS];
    static uint8_t obj_nslots[MAX_OBJS];
    for (int i = 0; i < MAX_OBJS; i++) obj_used[i] = 0;

    while (pc + INSN_SZ <= size) {
        uint8_t op = code[pc];
        const uint8_t* p = code + pc + 1;

        switch (op) {
        case GASM_LOAD:
            A = rd_i64(p);
            break;
        case GASM_ADD:
            A += rd_i64(p);
            break;
        case GASM_SUB:
            A -= rd_i64(p);
            break;
        case GASM_MUL:
            A *= rd_i64(p);
            break;
        case GASM_DIV: {
            int64_t b = rd_i64(p);
            if (b == 0) return -2;
            A /= b;
            break;
        }
        case GASM_PRINT:
            gasm_print_i64(putc, user, A);
            gasm_putc(putc, user, '\n');
            break;
        case GASM_PRINTN:
            gasm_print_i64(putc, user, A);
            break;
        case GASM_HALT:
            return 0;
        case GASM_JMP: {
            uint32_t target = rd_u32(p);
            if (!pc_valid(target, size)) return GASM_ERR_OOB;
            pc = target;
            continue;
        }
        case GASM_JZ:
            if (A == 0) {
                uint32_t target = rd_u32(p);
                if (!pc_valid(target, size)) return GASM_ERR_OOB;
                pc = target;
                continue;
            }
            break;
        case GASM_JNZ:
            if (A != 0) {
                uint32_t target = rd_u32(p);
                if (!pc_valid(target, size)) return GASM_ERR_OOB;
                pc = target;
                continue;
            }
            break;
        case GASM_NOP:
            break;
        case GASM_NEW: {
            uint32_t n = (uint32_t)(rd_i64(p) & 0xFFFFFFFFu);
            if (n == 0 || n > MAX_SLOTS) break;
            uint32_t h;
            for (h = 0; h < MAX_OBJS && obj_used[h]; h++);
            if (h >= MAX_OBJS) break;
            obj_used[h] = 1;
            obj_nslots[h] = (uint8_t)n;
            for (uint32_t i = 0; i < n; i++) obj[h][i] = 0;
            A = (int64_t)h;
            break;
        }
        case GASM_GET: {
            uint32_t h = rd_u32(p);
            uint32_t i = rd_u32(p + 4);
            if (h < MAX_OBJS && obj_used[h] && i < (uint32_t)obj_nslots[h])
                A = obj[h][i];
            break;
        }
        case GASM_SET: {
            uint32_t h = rd_u32(p);
            uint32_t i = rd_u32(p + 4);
            if (h < MAX_OBJS && obj_used[h] && i < (uint32_t)obj_nslots[h])
                obj[h][i] = A;
            break;
        }
        case GASM_GETSELF: {
            uint32_t i = (uint32_t)(rd_i64(p) & 0xFFFFFFFFu);
            if (S < MAX_OBJS && obj_used[S] && i < (uint32_t)obj_nslots[S])
                A = obj[S][i];
            break;
        }
        case GASM_SETSELF: {
            uint32_t i = (uint32_t)(rd_i64(p) & 0xFFFFFFFFu);
            if (S < MAX_OBJS && obj_used[S] && i < (uint32_t)obj_nslots[S])
                obj[S][i] = A;
            break;
        }
        case GASM_ADDSELF: {
            uint32_t i = (uint32_t)(rd_i64(p) & 0xFFFFFFFFu);
            if (S < MAX_OBJS && obj_used[S] && i < (uint32_t)obj_nslots[S])
                A += obj[S][i];
            break;
        }
        case GASM_CALL: {
            uint32_t target = rd_u32(p);
            if (!pc_valid(target, size)) return GASM_ERR_OOB;
            if (ret_sp >= MAX_RET) return -5;
            ret_stack[ret_sp++] = pc + INSN_SZ;
            pc = target;
            continue;
        }
        case GASM_RET:
            if (ret_sp <= 0) return 0;  /* empty stack = exit */
            pc = ret_stack[--ret_sp];
            break;
        case GASM_INVOKE: {
            uint32_t h = rd_u32(p);
            uint32_t target = rd_u32(p + 4);
            if (!pc_valid(target, size)) return GASM_ERR_OOB;
            if (ret_sp >= MAX_RET) return -5;
            S = h;
            ret_stack[ret_sp++] = pc + INSN_SZ;
            pc = target;
            continue;
        }
        case GASM_LT: {
            int64_t b = rd_i64(p);
            A = (A < b) ? 1 : 0;
            break;
        }
        case GASM_LE: {
            int64_t b = rd_i64(p);
            A = (A <= b) ? 1 : 0;
            break;
        }
        case GASM_GT: {
            int64_t b = rd_i64(p);
            A = (A > b) ? 1 : 0;
            break;
        }
        case GASM_GE: {
            int64_t b = rd_i64(p);
            A = (A >= b) ? 1 : 0;
            break;
        }
        case GASM_EQ: {
            int64_t b = rd_i64(p);
            A = (A == b) ? 1 : 0;
            break;
        }
        case GASM_NE: {
            int64_t b = rd_i64(p);
            A = (A != b) ? 1 : 0;
            break;
        }
        default:
            return -3;
        }
        pc += INSN_SZ;
    }
    return 0;
}

int gasm_vm_run(const uint8_t* code, uint32_t size)
{
    return gasm_vm_run_io(code, size, 0, 0);
}

/* ----- In-kernel assembler ----- */

static int gasm_strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static char* gasm_strchr(const char* s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return *s == (char)c ? (char*)s : 0;
}

static long gasm_strtol(const char* s, char** end, int base)
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

static char* gasm_strdup(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    char* r = (char*)kmalloc((uint32_t)(n + 1));
    if (!r) return 0;
    for (size_t i = 0; i <= n; i++) r[i] = s[i];
    return r;
}

#define MAX_LABELS 32
#define MAX_FIXUPS 32
#define CODE_CAP 2048
#define LINE_BUF 128

typedef struct { char* name; uint32_t pc; } gasm_label_t;
typedef struct { char* name; uint32_t at; } gasm_fixup_t;

static gasm_label_t labels[MAX_LABELS];
static int nlabels;
static gasm_fixup_t fixups[MAX_FIXUPS];
static int nfixups;
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

static void emit_i64(int64_t v)
{
    emit_u32((uint32_t)(v & 0xFFFFFFFFu));
    emit_u32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static void emit_insn(uint8_t op, int64_t imm)
{
    if (code_len + INSN_SZ <= CODE_CAP) {
        code[code_len++] = op;
        emit_i64(imm);
    }
}

static int is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int parse_int(const char* s, long* out)
{
    char* end;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        *out = gasm_strtol(s + 2, &end, 16);
        return end > s + 2;
    }
    *out = gasm_strtol(s, &end, 10);
    return end > s;
}

static void strip_comment(char* line)
{
    for (char* p = line; *p; p++)
        if (*p == ';' || *p == '#') { *p = '\0'; return; }
}

#define ARG() (arg[0] ? arg : "")

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
        for (i = 0; line[i] == ' ' || line[i] == '\t'; i++);
        if (!line[i]) continue;
        if (assemble_line(line + i, *lineno_ref) != 0) return 1;
    }
    return 0;
}

static int assemble_line(char* line, int lineno)
{
    if (line[0] == '\0') return 0;
    char* arg = gasm_strchr(line, ' ');
    if (arg) { *arg++ = '\0'; while (*arg == ' ' || *arg == '\t') arg++; }

    size_t ll = 0;
    while (line[ll]) ll++;
    if (ll > 0 && line[ll - 1] == ':') {
        line[ll - 1] = '\0';
        for (char* p = line; *p; p++)
            if (!is_ident(*p)) { kprintf("gasm:%d: invalid label\n", lineno); return 1; }
        for (int i = 0; i < nlabels; i++)
            if (gasm_strcmp(labels[i].name, line) == 0) { kprintf("gasm:%d: duplicate label\n", lineno); return 1; }
        if (nlabels >= MAX_LABELS) return 1;
        labels[nlabels].name = gasm_strdup(line);
        if (!labels[nlabels].name) return 1;
        labels[nlabels].pc = code_len;
        nlabels++;
        return 0;
    }

    for (char* p = line; *p; p++) *p = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);

    if (gasm_strcmp(line, "load") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: load needs int\n", lineno); return 1; }
        emit_insn(GASM_LOAD, (int64_t)v);
    } else if (gasm_strcmp(line, "add") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: add needs int\n", lineno); return 1; }
        emit_insn(GASM_ADD, (int64_t)v);
    } else if (gasm_strcmp(line, "sub") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: sub needs int\n", lineno); return 1; }
        emit_insn(GASM_SUB, (int64_t)v);
    } else if (gasm_strcmp(line, "mul") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: mul needs int\n", lineno); return 1; }
        emit_insn(GASM_MUL, (int64_t)v);
    } else if (gasm_strcmp(line, "div") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: div needs int\n", lineno); return 1; }
        emit_insn(GASM_DIV, (int64_t)v);
    } else if (gasm_strcmp(line, "print") == 0) {
        emit_insn(GASM_PRINT, 0);
    } else if (gasm_strcmp(line, "printn") == 0) {
        emit_insn(GASM_PRINTN, 0);
    } else if (gasm_strcmp(line, "halt") == 0) {
        emit_insn(GASM_HALT, 0);
    } else if (gasm_strcmp(line, "nop") == 0) {
        emit_insn(GASM_NOP, 0);
    } else if (gasm_strcmp(line, "new") == 0) {
        long v; if (!parse_int(ARG(), &v) || v < 1 || v > MAX_SLOTS) { kprintf("gasm:%d: new needs 1-%d\n", lineno, MAX_SLOTS); return 1; }
        emit_insn(GASM_NEW, (int64_t)v);
    } else if (gasm_strcmp(line, "get") == 0) {
        long h, i; char* a = ARG(); char* a2 = gasm_strchr(a, ' ');
        if (!a2) { kprintf("gasm:%d: get needs handle slot\n", lineno); return 1; }
        *a2++ = '\0'; while (*a2 == ' ') a2++;
        if (!parse_int(a, &h) || !parse_int(a2, &i)) { kprintf("gasm:%d: get needs two ints\n", lineno); return 1; }
        if (code_len + INSN_SZ > CODE_CAP) return 1;
        code[code_len++] = GASM_GET;
        emit_u32((uint32_t)h);
        emit_u32((uint32_t)i);
    } else if (gasm_strcmp(line, "set") == 0) {
        long h, i; char* a = ARG(); char* a2 = gasm_strchr(a, ' ');
        if (!a2) { kprintf("gasm:%d: set needs handle slot\n", lineno); return 1; }
        *a2++ = '\0'; while (*a2 == ' ') a2++;
        if (!parse_int(a, &h) || !parse_int(a2, &i)) { kprintf("gasm:%d: set needs two ints\n", lineno); return 1; }
        if (code_len + INSN_SZ > CODE_CAP) return 1;
        code[code_len++] = GASM_SET;
        emit_u32((uint32_t)h);
        emit_u32((uint32_t)i);
    } else if (gasm_strcmp(line, "getself") == 0) {
        long v; if (!parse_int(ARG(), &v) || v < 0) { kprintf("gasm:%d: getself needs slot index\n", lineno); return 1; }
        emit_insn(GASM_GETSELF, (int64_t)v);
    } else if (gasm_strcmp(line, "setself") == 0) {
        long v; if (!parse_int(ARG(), &v) || v < 0) { kprintf("gasm:%d: setself needs slot index\n", lineno); return 1; }
        emit_insn(GASM_SETSELF, (int64_t)v);
    } else if (gasm_strcmp(line, "addself") == 0) {
        long v; if (!parse_int(ARG(), &v) || v < 0) { kprintf("gasm:%d: addself needs slot index\n", lineno); return 1; }
        emit_insn(GASM_ADDSELF, (int64_t)v);
    } else if (gasm_strcmp(line, "ret") == 0) {
        emit_insn(GASM_RET, 0);
    } else if (gasm_strcmp(line, "call") == 0) {
        if (!ARG() || !ARG()[0]) { kprintf("gasm:%d: call needs label\n", lineno); return 1; }
        for (char* p = ARG(); *p; p++) if (!is_ident(*p)) { kprintf("gasm:%d: bad label\n", lineno); return 1; }
        if (code_len + INSN_SZ > CODE_CAP) return 1;
        code[code_len++] = GASM_CALL;
        if (nfixups >= MAX_FIXUPS) return 1;
        fixups[nfixups].name = gasm_strdup(ARG());
        if (!fixups[nfixups].name) return 1;
        fixups[nfixups].at = code_len;
        nfixups++;
        emit_u32(0);
        emit_u32(0);
    } else if (gasm_strcmp(line, "invoke") == 0) {
        long h; char* a = ARG(); char* a2 = gasm_strchr(a, ' ');
        if (!a2) { kprintf("gasm:%d: invoke needs handle label\n", lineno); return 1; }
        *a2++ = '\0'; while (*a2 == ' ') a2++;
        if (!parse_int(a, &h) || h < 0) { kprintf("gasm:%d: invoke needs handle\n", lineno); return 1; }
        for (char* p = a2; *p; p++) if (!is_ident(*p)) { kprintf("gasm:%d: bad label\n", lineno); return 1; }
        if (code_len + INSN_SZ > CODE_CAP) return 1;
        code[code_len++] = GASM_INVOKE;
        emit_u32((uint32_t)h);
        if (nfixups >= MAX_FIXUPS) return 1;
        fixups[nfixups].name = gasm_strdup(a2);
        if (!fixups[nfixups].name) return 1;
        fixups[nfixups].at = code_len;
        nfixups++;
        emit_u32(0);
    } else if (gasm_strcmp(line, "lt") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: lt needs int\n", lineno); return 1; }
        emit_insn(GASM_LT, (int64_t)v);
    } else if (gasm_strcmp(line, "le") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: le needs int\n", lineno); return 1; }
        emit_insn(GASM_LE, (int64_t)v);
    } else if (gasm_strcmp(line, "gt") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: gt needs int\n", lineno); return 1; }
        emit_insn(GASM_GT, (int64_t)v);
    } else if (gasm_strcmp(line, "ge") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: ge needs int\n", lineno); return 1; }
        emit_insn(GASM_GE, (int64_t)v);
    } else if (gasm_strcmp(line, "eq") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: eq needs int\n", lineno); return 1; }
        emit_insn(GASM_EQ, (int64_t)v);
    } else if (gasm_strcmp(line, "ne") == 0) {
        long v; if (!parse_int(ARG(), &v)) { kprintf("gasm:%d: ne needs int\n", lineno); return 1; }
        emit_insn(GASM_NE, (int64_t)v);
    } else if (gasm_strcmp(line, "jmp") == 0 || gasm_strcmp(line, "jz") == 0 || gasm_strcmp(line, "jnz") == 0) {
        if (!ARG() || !ARG()[0]) { kprintf("gasm:%d: label required\n", lineno); return 1; }
        for (char* p = ARG(); *p; p++) if (!is_ident(*p)) { kprintf("gasm:%d: bad label\n", lineno); return 1; }
        uint8_t op = (gasm_strcmp(line, "jmp") == 0) ? GASM_JMP : (gasm_strcmp(line, "jz") == 0) ? GASM_JZ : GASM_JNZ;
        if (code_len + INSN_SZ > CODE_CAP) return 1;
        code[code_len++] = op;
        if (nfixups >= MAX_FIXUPS) return 1;
        fixups[nfixups].name = gasm_strdup(ARG());
        if (!fixups[nfixups].name) return 1;
        fixups[nfixups].at = code_len;
        nfixups++;
        emit_u32(0);
        emit_u32(0); /* 8-byte payload, high 4 unused for branches */
    } else {
        kprintf("gasm:%d: unknown op '%s'\n", lineno, line);
        return 1;
    }
    return 0;
}

static uint8_t* build_code(uint32_t* out_size)
{
    for (int i = 0; i < nfixups; i++) {
        int found = 0;
        for (int j = 0; j < nlabels; j++) {
            if (gasm_strcmp(fixups[i].name, labels[j].name) == 0) {
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
            kprintf("gasm: undefined label '%s'\n", fixups[i].name);
            return 0;
        }
    }

    uint8_t* copy = (uint8_t*)kmalloc(code_len);
    if (!copy) return 0;
    memcpy(copy, code, code_len);
    *out_size = code_len;
    return copy;
}

static void cleanup(void)
{
    for (int i = 0; i < nlabels; i++)
        if (labels[i].name) kfree(labels[i].name);
    for (int i = 0; i < nfixups; i++)
        if (fixups[i].name) kfree(fixups[i].name);
}

int gasm_asm_eval(const char* src, gasm_putc_fn putc, void* user)
{
    if (!src) return -1;
    size_t len = 0;
    while (src[len]) len++;
    if (len == 0) return 0;

    char* src_copy = (char*)kmalloc((uint32_t)(len + 1));
    if (!src_copy) return -1;
    memcpy(src_copy, src, len + 1);

    nlabels = nfixups = 0;
    code_len = 0;

    int lineno = 1;
    int err = assemble_source(src_copy, &lineno);
    kfree(src_copy);
    if (err) { cleanup(); return -2; }

    uint32_t size = 0;
    uint8_t* bytecode = build_code(&size);
    cleanup();
    if (!bytecode) return -1;

    int rc = gasm_vm_run_io(bytecode, size, putc, user);
    kfree(bytecode);
    return rc;
}
