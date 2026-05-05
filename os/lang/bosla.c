/*
 * bosla - BOSL assembler. Assembles .bosl source into BOSL bytecode.
 * Supports include for .boslib library files (like C's .h).
 *
 * Usage: bosla input.bosl -o output.bosli
 *        bosla input.bosl -o output.bosli -I libdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define PATH_SEP '\\'
#define PATH_SEP_ALT '/'
#else
#define PATH_SEP '/'
#define PATH_SEP_ALT '/'
#endif

#define MAGIC "BOSL"
#define VERSION 1
#define MAX_LABELS 256
#define MAX_FIXUPS 512
#define MAX_CONST 512
#define CODE_CAP 65536
#define LINE_BUF 512
#define MAX_INCLUDE 32
#define MAX_INCLUDE_PATHS 8
#define PATH_BUF 512

static const char* include_paths[MAX_INCLUDE_PATHS];
static int n_include_paths;
static int include_depth;

/* BOSL opcodes - match bosl_vm.c */
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
    OP_ULT = 0x46, OP_ULE = 0x47, OP_UGT = 0x48, OP_UGE = 0x49, OP_UDIV = 0x4A, OP_UMOD = 0x4B,
    OP_MIN = 0x4C, OP_MAX = 0x4D, OP_ABS = 0x4E, OP_SGN = 0x4F,
    OP_INC = 0x50, OP_DEC = 0x51, OP_EMIT = 0x52, OP_QDUP = 0x53,
    OP_PEEKU8 = 0x60, OP_POKEU8 = 0x61, OP_PEEKU32 = 0x62, OP_POKEU32 = 0x63,
    OP_INB = 0x64, OP_OUTB = 0x65, OP_INW = 0x66, OP_OUTW = 0x67, OP_INL = 0x68, OP_OUTL = 0x69,
    OP_CLI = 0x6A, OP_STI = 0x6B, OP_MEMFENCE = 0x6C, OP_PEEKU16 = 0x6D, OP_POKEU16 = 0x6E,
    OP_PEEKU64 = 0x6F, OP_POKEU64 = 0x70,
    OP_MEMCPY = 0x71, OP_MEMSET = 0x72, OP_CPU_PAUSE = 0x73,
    OP_MFENCE_CPU = 0x74, OP_LFENCE_CPU = 0x75, OP_SFENCE_CPU = 0x76,
    OP_LAFILLO_PRINT = 0x77,
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

static void emit_u32(uint32_t v) {
    if (code_len + 4 <= CODE_CAP) {
        code[code_len++] = (uint8_t)(v);
        code[code_len++] = (uint8_t)(v >> 8);
        code[code_len++] = (uint8_t)(v >> 16);
        code[code_len++] = (uint8_t)(v >> 24);
    }
}

static void emit_i32(int32_t v) {
    emit_u32((uint32_t)v);
}

static void emit_i64(int64_t v) {
    emit_u32((uint32_t)(v & 0xFFFFFFFFu));
    emit_u32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int parse_int(const char* s, long* out) {
    char* end;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        *out = strtol(s + 2, &end, 16);
        return end > s + 2;
    }
    *out = strtol(s, &end, 10);
    return end > s;
}

static int parse_str(const char* s, char* out, size_t cap, size_t* len) {
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

static int intern_str(const uint8_t* b, size_t len) {
    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 1 && constants[i].u.len == len &&
            memcmp(constants[i].u.s, b, len) == 0)
            return i;
    }
    if (nconst >= MAX_CONST) return -1;
    uint8_t* copy = (uint8_t*)malloc(len);
    if (!copy) return -1;
    memcpy(copy, b, len);
    constants[nconst].kind = 1;
    constants[nconst].u.s = copy;
    constants[nconst].u.len = len;
    return nconst++;
}

static int intern_int(int32_t v) {
    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 2 && constants[i].u.i == v) return i;
    }
    if (nconst >= MAX_CONST) return -1;
    constants[nconst].kind = 2;
    constants[nconst].u.i = v;
    return nconst++;
}

static int intern_i64(int64_t v) {
    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 3 && constants[i].u.i == v) return i;
    }
    if (nconst >= MAX_CONST) return -1;
    constants[nconst].kind = 3;
    constants[nconst].u.i = v;
    return nconst++;
}

/* `;` or `#` starts a line-end comment (ignored). Not inside "..." strings. */
static void strip_comment(char* line) {
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

static void rstrip_line(char* line) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
}

/* Copy dirname of path into out (excluding trailing slash). Returns 0 on success. */
static int get_dirname(const char* path, char* out, size_t cap) {
    const char* last = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (!last) {
        if (cap < 2) return -1;
        out[0] = '.';
        out[1] = '\0';
        return 0;
    }
    size_t n = (size_t)(last - path);
    if (n >= cap) return -1;
    memcpy(out, path, n);
    out[n] = '\0';
    return 0;
}

/* Resolve include path: try base_dir/path, then include_paths. */
static int resolve_include(const char* path, const char* base_dir, char* out, size_t cap) {
    char buf[PATH_BUF];
    if (base_dir && base_dir[0]) {
        size_t bl = strlen(base_dir);
        if (bl + 1 + strlen(path) + 1 <= sizeof(buf)) {
            memcpy(buf, base_dir, bl);
            buf[bl] = PATH_SEP;
            strcpy(buf + bl + 1, path);
            FILE* f = fopen(buf, "rb");
            if (f) { fclose(f); strncpy(out, buf, cap - 1); out[cap - 1] = '\0'; return 0; }
        }
    }
    for (int i = 0; i < n_include_paths; i++) {
        size_t il = strlen(include_paths[i]);
        if (il + 1 + strlen(path) + 1 <= sizeof(buf)) {
            memcpy(buf, include_paths[i], il);
            buf[il] = PATH_SEP;
            strcpy(buf + il + 1, path);
            FILE* f = fopen(buf, "rb");
            if (f) { fclose(f); strncpy(out, buf, cap - 1); out[cap - 1] = '\0'; return 0; }
        }
    }
    if (strlen(path) < cap) {
        strcpy(out, path);
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); return 0; }
    }
    return -1;
}

/* Parse include "path" from line. Returns 0 if valid, -1 if not include, 1 if parse error. */
static int parse_include(const char* line, char* path_out, size_t path_cap) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strlen(p) < 7) return -1;
    if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') &&
        (p[2] == 'c' || p[2] == 'C') && (p[3] == 'l' || p[3] == 'L') &&
        (p[4] == 'u' || p[4] == 'U') && (p[5] == 'd' || p[5] == 'D') &&
        (p[6] == 'e' || p[6] == 'E'))
        ;
    else return -1;
    p += 7;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < path_cap - 1) {
        if (*p == '\\' && p[1]) { p++; }
        path_out[i++] = *p++;
    }
    path_out[i] = '\0';
    return (*p == '"') ? 0 : 1;
}

static int assemble_line(char* line, int lineno);

/* Assemble source buffer. base_dir used for resolving include paths. */
static int assemble_buffer(char* src, int* lineno_ref, const char* base_dir) {
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

        char inc_path[PATH_BUF];
        int inc_r = parse_include(line + i, inc_path, sizeof(inc_path));
        if (inc_r == 0) {
            if (include_depth >= MAX_INCLUDE) {
                fprintf(stderr, "bosla:%d: include depth exceeded\n", *lineno_ref);
                return 1;
            }
            char resolved[PATH_BUF];
            if (resolve_include(inc_path, base_dir, resolved, sizeof(resolved)) != 0) {
                fprintf(stderr, "bosla:%d: cannot find include '%s'\n", *lineno_ref, inc_path);
                return 1;
            }
            FILE* inc_f = fopen(resolved, "rb");
            if (!inc_f) {
                fprintf(stderr, "bosla: cannot open %s\n", resolved);
                return 1;
            }
            fseek(inc_f, 0, SEEK_END);
            long inc_sz = ftell(inc_f);
            fseek(inc_f, 0, SEEK_SET);
            char* inc_src = (char*)malloc((size_t)inc_sz + 1);
            if (!inc_src) { fclose(inc_f); return 1; }
            fread(inc_src, 1, (size_t)inc_sz, inc_f);
            inc_src[inc_sz] = '\0';
            fclose(inc_f);

            char inc_dir[PATH_BUF];
            get_dirname(resolved, inc_dir, sizeof(inc_dir));

            include_depth++;
            int err = assemble_buffer(inc_src, lineno_ref, inc_dir);
            include_depth--;
            free(inc_src);
            if (err) return 1;
        } else if (inc_r == 1) {
            fprintf(stderr, "bosla:%d: invalid include syntax\n", *lineno_ref);
            return 1;
        } else if (assemble_line(line + i, *lineno_ref) != 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* in_path = NULL;
    const char* out_path = NULL;
    n_include_paths = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) out_path = argv[++i];
        } else if (strcmp(argv[i], "-I") == 0) {
            if (i + 1 < argc && n_include_paths < MAX_INCLUDE_PATHS)
                include_paths[n_include_paths++] = argv[++i];
        } else if (argv[i][0] != '-') {
            in_path = argv[i];
            break;
        }
    }
    if (!in_path || !out_path) {
        fprintf(stderr, "Usage: bosla input.bosl -o output.bosli [-I libdir ...]\n");
        return 1;
    }

    FILE* f = fopen(in_path, "rb");
    if (!f) {
        fprintf(stderr, "bosla: cannot open %s\n", in_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = (char*)malloc((size_t)fsz + 1);
    if (!src) { fclose(f); return 1; }
    fread(src, 1, (size_t)fsz, f);
    src[fsz] = '\0';
    fclose(f);

    nlabels = nfixups = nconst = 0;
    code_len = 0;
    include_depth = 0;

    char base_dir[PATH_BUF];
    get_dirname(in_path, base_dir, sizeof(base_dir));

    int lineno = 1;
    if (assemble_buffer(src, &lineno, base_dir) != 0) {
        free(src);
        return 1;
    }
    free(src);

    for (int i = 0; i < nfixups; i++) {
        int found = 0;
        for (int j = 0; j < nlabels; j++) {
            if (strcmp(fixups[i].name, labels[j].name) == 0) {
                uint32_t v = labels[j].pc;
                uint32_t at = fixups[i].at;
                if (at + 4 <= code_len) {
                    code[at] = (uint8_t)(v);
                    code[at + 1] = (uint8_t)(v >> 8);
                    code[at + 2] = (uint8_t)(v >> 16);
                    code[at + 3] = (uint8_t)(v >> 24);
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "bosla: undefined label '%s'\n", fixups[i].name);
            return 1;
        }
    }

    FILE* of = fopen(out_path, "wb");
    if (!of) {
        fprintf(stderr, "bosla: cannot write %s\n", out_path);
        return 1;
    }

    fwrite(MAGIC, 1, 4, of);
    { uint16_t v = VERSION; fputc(v & 0xFF, of); fputc(v >> 8, of); }
    { uint16_t v = 0; fputc(v & 0xFF, of); fputc(v >> 8, of); }
    { uint32_t v = nconst; fputc(v & 0xFF, of); fputc((v >> 8) & 0xFF, of); fputc((v >> 16) & 0xFF, of); fputc(v >> 24, of); }
    { uint32_t v = code_len; fputc(v & 0xFF, of); fputc((v >> 8) & 0xFF, of); fputc((v >> 16) & 0xFF, of); fputc(v >> 24, of); }
    { uint32_t v = 0; fputc(v & 0xFF, of); fputc((v >> 8) & 0xFF, of); fputc((v >> 16) & 0xFF, of); fputc(v >> 24, of); }

    for (int i = 0; i < nconst; i++) {
        if (constants[i].kind == 1) {
            uint32_t len = (uint32_t)constants[i].u.len;
            uint32_t t = 1;
            fwrite(&t, 4, 1, of);
            fwrite(&len, 4, 1, of);
            fwrite(constants[i].u.s, 1, len, of);
        } else if (constants[i].kind == 2) {
            uint32_t t = 2;
            int32_t v = (int32_t)constants[i].u.i;
            fwrite(&t, 4, 1, of);
            fwrite(&v, 4, 1, of);
        } else {
            uint32_t t = 3;
            int64_t v = constants[i].u.i;
            fwrite(&t, 4, 1, of);
            fwrite(&v, 8, 1, of);
        }
    }
    fwrite(code, 1, code_len, of);
    fclose(of);

    for (int i = 0; i < nconst; i++)
        if (constants[i].kind == 1) free(constants[i].u.s);
    for (int i = 0; i < nlabels; i++) free(labels[i].name);
    for (int i = 0; i < nfixups; i++) free(fixups[i].name);

    printf("Wrote %s (%u code bytes, %d consts)\n", out_path, code_len, nconst);
    return 0;
}

#define ARG() (arg[0] ? arg : "")
#define EMIT(op) do { if (code_len < CODE_CAP) code[code_len++] = (op); } while(0)

static int assemble_line(char* line, int lineno) {
    if (line[0] == '\0') return 0;
    char* arg = strchr(line, ' ');
    if (arg) { *arg++ = '\0'; while (*arg == ' ' || *arg == '\t') arg++; }

    if (line[strlen(line) - 1] == ':') {
        line[strlen(line) - 1] = '\0';
        for (char* p = line; *p; p++) if (!is_ident_char(*p)) {
            fprintf(stderr, "bosla:%d: invalid label\n", lineno);
            return 1;
        }
        for (int i = 0; i < nlabels; i++) {
            if (strcmp(labels[i].name, line) == 0) {
                fprintf(stderr, "bosla:%d: duplicate label\n", lineno);
                return 1;
            }
        }
        if (nlabels >= MAX_LABELS) return 1;
        labels[nlabels].name = strdup(line);
        labels[nlabels].pc = code_len;
        nlabels++;
        return 0;
    }

    for (char* p = line; *p; p++) *p = (char)tolower((unsigned char)*p);

    if (strcmp(line, "pushi") == 0) {
        long v; if (!parse_int(ARG(), &v)) { fprintf(stderr, "bosla:%d: pushi needs int\n", lineno); return 1; }
        EMIT(OP_PUSHI); emit_i32((int32_t)v);
    } else if (strcmp(line, "pushi64") == 0) {
        long v; if (!parse_int(ARG(), &v)) { fprintf(stderr, "bosla:%d: pushi64 needs int\n", lineno); return 1; }
        EMIT(OP_PUSHI64); emit_i64((int64_t)v);
    } else if (strcmp(line, "pushc") == 0) {
        char buf[4096]; size_t len;
        if (parse_str(ARG(), buf, sizeof(buf), &len) != 0) { fprintf(stderr, "bosla:%d: pushc needs \"str\"\n", lineno); return 1; }
        int idx = intern_str((uint8_t*)buf, len);
        if (idx < 0) return 1;
        EMIT(OP_PUSHC); emit_u32(idx);
    } else if (strcmp(line, "pushk") == 0) {
        long v; if (!parse_int(ARG(), &v)) { fprintf(stderr, "bosla:%d: pushk needs int\n", lineno); return 1; }
        int idx = intern_int((int32_t)v);
        if (idx < 0) return 1;
        EMIT(OP_PUSHK); emit_u32(idx);
    } else if (strcmp(line, "pushk64") == 0) {
        long v; if (!parse_int(ARG(), &v)) { fprintf(stderr, "bosla:%d: pushk64 needs int\n", lineno); return 1; }
        int idx = intern_i64((int64_t)v);
        if (idx < 0) return 1;
        EMIT(OP_PUSHK64); emit_u32(idx);
    } else if (strcmp(line, "pick") == 0) {
        long v; if (!parse_int(ARG(), &v) || v < 0 || v > 255) { fprintf(stderr, "bosla:%d: pick 0-255\n", lineno); return 1; }
        EMIT(OP_PICK); if (code_len < CODE_CAP) code[code_len++] = (uint8_t)v;
    } else if (strcmp(line, "jmp") == 0 || strcmp(line, "jz") == 0 || strcmp(line, "jnz") == 0 || strcmp(line, "call") == 0) {
        if (!ARG() || !ARG()[0]) { fprintf(stderr, "bosla:%d: label required\n", lineno); return 1; }
        for (char* p = ARG(); *p; p++) if (!is_ident_char(*p)) { fprintf(stderr, "bosla:%d: bad label\n", lineno); return 1; }
        uint8_t op = (strcmp(line, "jmp") == 0) ? OP_JMP : (strcmp(line, "jz") == 0) ? OP_JZ : (strcmp(line, "jnz") == 0) ? OP_JNZ : OP_CALL;
        EMIT(op);
        if (nfixups >= MAX_FIXUPS) return 1;
        fixups[nfixups].name = strdup(ARG());
        fixups[nfixups].at = code_len;
        nfixups++;
        emit_u32(0);
    } else {
        uint8_t op = 0;
        if (strcmp(line, "add") == 0) op = OP_ADD;
        else if (strcmp(line, "sub") == 0) op = OP_SUB;
        else if (strcmp(line, "mul") == 0) op = OP_MUL;
        else if (strcmp(line, "div") == 0) op = OP_DIV;
        else if (strcmp(line, "mod") == 0) op = OP_MOD;
        else if (strcmp(line, "neg") == 0) op = OP_NEG;
        else if (strcmp(line, "and") == 0) op = OP_AND;
        else if (strcmp(line, "or") == 0) op = OP_OR;
        else if (strcmp(line, "xor") == 0) op = OP_XOR;
        else if (strcmp(line, "not") == 0) op = OP_NOT;
        else if (strcmp(line, "shl") == 0) op = OP_SHL;
        else if (strcmp(line, "shr") == 0) op = OP_SHR;
        else if (strcmp(line, "ushr") == 0) op = OP_USHR;
        else if (strcmp(line, "rol") == 0) op = OP_ROL;
        else if (strcmp(line, "ror") == 0) op = OP_ROR;
        else if (strcmp(line, "print") == 0) op = OP_PRINT;
        else if (strcmp(line, "dup") == 0) op = OP_DUP;
        else if (strcmp(line, "drop") == 0) op = OP_DROP;
        else if (strcmp(line, "swap") == 0) op = OP_SWAP;
        else if (strcmp(line, "over") == 0) op = OP_OVER;
        else if (strcmp(line, "i32toi64") == 0) op = OP_I32TOI64;
        else if (strcmp(line, "i64toi32") == 0) op = OP_I64TOI32;
        else if (strcmp(line, "rot") == 0) op = OP_ROT;
        else if (strcmp(line, "nip") == 0) op = OP_NIP;
        else if (strcmp(line, "tuck") == 0) op = OP_TUCK;
        else if (strcmp(line, "depth") == 0) op = OP_DEPTH;
        else if (strcmp(line, "printn") == 0) op = OP_PRINTN;
        else if (strcmp(line, "2dup") == 0) op = OP_2DUP;
        else if (strcmp(line, "2drop") == 0) op = OP_2DROP;
        else if (strcmp(line, "nop") == 0) op = OP_NOP;
        else if (strcmp(line, "ret") == 0) op = OP_RET;
        else if (strcmp(line, "eq") == 0) op = OP_EQ;
        else if (strcmp(line, "ne") == 0) op = OP_NE;
        else if (strcmp(line, "lt") == 0) op = OP_LT;
        else if (strcmp(line, "le") == 0) op = OP_LE;
        else if (strcmp(line, "gt") == 0) op = OP_GT;
        else if (strcmp(line, "ge") == 0) op = OP_GE;
        else if (strcmp(line, "ult") == 0) op = OP_ULT;
        else if (strcmp(line, "ule") == 0) op = OP_ULE;
        else if (strcmp(line, "ugt") == 0) op = OP_UGT;
        else if (strcmp(line, "uge") == 0) op = OP_UGE;
        else if (strcmp(line, "udiv") == 0) op = OP_UDIV;
        else if (strcmp(line, "umod") == 0) op = OP_UMOD;
        else if (strcmp(line, "min") == 0) op = OP_MIN;
        else if (strcmp(line, "max") == 0) op = OP_MAX;
        else if (strcmp(line, "abs") == 0) op = OP_ABS;
        else if (strcmp(line, "sgn") == 0) op = OP_SGN;
        else if (strcmp(line, "inc") == 0) op = OP_INC;
        else if (strcmp(line, "dec") == 0) op = OP_DEC;
        else if (strcmp(line, "emit") == 0) op = OP_EMIT;
        else if (strcmp(line, "?dup") == 0) op = OP_QDUP;
        else if (strcmp(line, "peeku8") == 0) op = OP_PEEKU8;
        else if (strcmp(line, "pokeu8") == 0) op = OP_POKEU8;
        else if (strcmp(line, "peeku32") == 0) op = OP_PEEKU32;
        else if (strcmp(line, "pokeu32") == 0) op = OP_POKEU32;
        else if (strcmp(line, "peeku16") == 0) op = OP_PEEKU16;
        else if (strcmp(line, "pokeu16") == 0) op = OP_POKEU16;
        else if (strcmp(line, "peeku64") == 0) op = OP_PEEKU64;
        else if (strcmp(line, "pokeu64") == 0) op = OP_POKEU64;
        else if (strcmp(line, "inb") == 0) op = OP_INB;
        else if (strcmp(line, "outb") == 0) op = OP_OUTB;
        else if (strcmp(line, "inw") == 0) op = OP_INW;
        else if (strcmp(line, "outw") == 0) op = OP_OUTW;
        else if (strcmp(line, "inl") == 0) op = OP_INL;
        else if (strcmp(line, "outl") == 0) op = OP_OUTL;
        else if (strcmp(line, "cli") == 0) op = OP_CLI;
        else if (strcmp(line, "sti") == 0) op = OP_STI;
        else if (strcmp(line, "memfence") == 0) op = OP_MEMFENCE;
        else if (strcmp(line, "memcpy") == 0) op = OP_MEMCPY;
        else if (strcmp(line, "memset") == 0) op = OP_MEMSET;
        else if (strcmp(line, "pause") == 0) op = OP_CPU_PAUSE;
        else if (strcmp(line, "mfence") == 0) op = OP_MFENCE_CPU;
        else if (strcmp(line, "lfence") == 0) op = OP_LFENCE_CPU;
        else if (strcmp(line, "sfence") == 0) op = OP_SFENCE_CPU;
        else if (strcmp(line, "lafillop") == 0 || strcmp(line, "lafillo_print") == 0) op = OP_LAFILLO_PRINT;
        else if (strcmp(line, "slen") == 0) op = OP_SLEN;
        else if (strcmp(line, "within") == 0) op = OP_WITHIN;
        else if (strcmp(line, "2swap") == 0) op = OP_2SWAP;
        else if (strcmp(line, "rand") == 0) op = OP_RAND;
        else if (strcmp(line, "time") == 0) op = OP_TIME;
        else if (strcmp(line, "lipc_send") == 0) op = OP_LIPC_SEND;
        else if (strcmp(line, "lipc_recv") == 0) op = OP_LIPC_RECV;
        else if (strcmp(line, "lipc_pending") == 0) op = OP_LIPC_PENDING;
        else if (strcmp(line, "lipc_send_str") == 0) op = OP_LIPC_SEND_STR;
        else if (strcmp(line, "halt") == 0) op = OP_HALT;
        else {
            fprintf(stderr, "bosla:%d: unknown op '%s'\n", lineno, line);
            return 1;
        }
        EMIT(op);
    }
    return 0;
}
