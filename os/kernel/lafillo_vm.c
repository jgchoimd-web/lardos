/*
 * lafillo_vm.c - Lafillo 전용 간단한 VM
 *
 * Bytecode format (LE):
 *   0x00: "DVM\0"
 *   0x04: u16 version (1)
 *   0x06: u16 reserved
 *   0x08: u32 const_count
 *   0x0C: u32 code_size
 *   0x10: [consts: u32 len, u8[len]] * const_count
 *   then: code bytes
 *
 * Opcodes:
 *   0x00 HALT
 *   0x01 PUSH_STR u32 const_idx
 *   0x02 LAFILLO - pop str, html->text, push result
 *   0x03 PRINT  - pop str, output via putc
 */
#include "lafillo_vm.h"
#include "console.h"
#include "lafillo.h"
#include "mem.h"
#include "vmmon.h"

#include <stdint.h>

#define DVM_MAGIC 0x4D5644u  /* "DVM" LE */
#define DVM_VERSION 1
#define MAX_STACK 32
#define MAX_STR_LEN 4096

typedef struct {
    const uint8_t* ptr;
    uint32_t len;
} str_val_t;

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void dvm_putc(lafillo_vm_putc_fn putc, void* user, char c)
{
    if (putc) putc(c, user);
    else console_putc(c);
}

static void dvm_out_bytes(lafillo_vm_putc_fn putc, void* user, const uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        dvm_putc(putc, user, (char)p[i]);
}

int lafillo_vm_run(const uint8_t* image, uint32_t size)
{
    return lafillo_vm_run_io(image, size, 0, 0);
}

int lafillo_vm_run_io(const uint8_t* image, uint32_t size, lafillo_vm_putc_fn putc, void* user)
{
    if (!image || size < 16) {
        vmmon_record(VMMON_LAFILLO, 0, -1);
        return -1;
    }

    uint32_t mag = rd_u32(image);
    if (mag != DVM_MAGIC) {
        vmmon_record(VMMON_LAFILLO, 0, -2);
        return -2;
    }

    uint16_t ver = (uint16_t)image[4] | ((uint16_t)image[5] << 8);
    if (ver != DVM_VERSION) {
        vmmon_record(VMMON_LAFILLO, 0, -3);
        return -3;
    }

    uint32_t const_count = rd_u32(image + 8);
    uint32_t code_size = rd_u32(image + 12);

    /* Parse const pool */
    uint32_t off = 16;
    str_val_t* consts = (str_val_t*)kmalloc(const_count * sizeof(str_val_t));
    if (!consts) {
        vmmon_record(VMMON_LAFILLO, 0, -5);
        return -5;
    }
    uint32_t steps = 0;
    uint32_t budget = vmmon_budget(VMMON_LAFILLO);
    int rc = 0;

#define DVM_FINISH(code_) do { rc = (code_); goto dvm_done; } while (0)

    for (uint32_t i = 0; i < const_count; i++) {
        if (off + 4 > size) {
            DVM_FINISH(-6);
        }
        uint32_t len = rd_u32(image + off);
        off += 4;
        if (off + len > size) {
            DVM_FINISH(-7);
        }
        consts[i].ptr = image + off;
        consts[i].len = len;
        off += len;
    }

    if (off + code_size > size) {
        DVM_FINISH(-4);
    }

    const uint8_t* code = image + off;

    /* Stack of string values (ptr,len) - we use static buffers for results */
    static char stack_buf[MAX_STACK][MAX_STR_LEN];
    static uint32_t stack_len[MAX_STACK];
    uint32_t sp = 0;

    static char lafillo_out[MAX_STR_LEN];

    uint32_t pc = 0;
    while (pc < code_size) {
        if (++steps > budget) {
            DVM_FINISH(-16);
        }
        uint8_t op = code[pc++];
        switch (op) {
        case 0x00: /* HALT */
            DVM_FINISH(0);
        case 0x01: { /* PUSH_STR */
            if (pc + 4 > code_size) {
                DVM_FINISH(-9);
            }
            uint32_t idx = rd_u32(code + pc);
            pc += 4;
            if (idx >= const_count || sp >= MAX_STACK) {
                DVM_FINISH(-10);
            }
            uint32_t n = consts[idx].len;
            if (n >= MAX_STR_LEN) n = MAX_STR_LEN - 1;
            for (uint32_t i = 0; i < n; i++)
                stack_buf[sp][i] = (char)consts[idx].ptr[i];
            stack_buf[sp][n] = '\0';
            stack_len[sp] = n;
            sp++;
            break;
        }
        case 0x02: { /* LAFILLO */
            if (sp == 0) {
                DVM_FINISH(-11);
            }
            sp--;
            const char* in = stack_buf[sp];
            uint32_t in_len = stack_len[sp];
            if (lafillo_http_to_text(in, in_len, lafillo_out, sizeof(lafillo_out)) != 0) {
                DVM_FINISH(-12);
            }
            uint32_t out_len = 0;
            while (lafillo_out[out_len] && out_len < MAX_STR_LEN - 1) out_len++;
            if (sp < MAX_STACK) {
                for (uint32_t i = 0; i <= out_len && i < MAX_STR_LEN; i++)
                    stack_buf[sp][i] = lafillo_out[i];
                stack_len[sp] = out_len;
                sp++;
            }
            break;
        }
        case 0x03: { /* PRINT */
            if (sp == 0) {
                DVM_FINISH(-13);
            }
            sp--;
            dvm_out_bytes(putc, user, (const uint8_t*)stack_buf[sp], stack_len[sp]);
            dvm_putc(putc, user, '\n');
            break;
        }
        default:
            DVM_FINISH(-14);
        }
    }

dvm_done:
    kfree(consts);
    vmmon_record(VMMON_LAFILLO, steps, rc);
#undef DVM_FINISH
    return rc;
}

/* Minimal inline assembler */
static int parse_quoted_str(const char* p, char* out, uint32_t cap, const char** endp)
{
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;
    uint32_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p != '"') return -2;
    *endp = p + 1;
    return (int)i;
}

static void skip_space(const char** p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

static int ch_eq_ci(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int starts_with_ci(const char* p, const char* word)
{
    while (*word) {
        if (!ch_eq_ci(*p, *word)) return 0;
        p++;
        word++;
    }
    return 1;
}

static void skip_line(const char** p)
{
    while (**p && **p != '\n') (*p)++;
}

int lafillo_vm_asm_eval(const char* src, lafillo_vm_putc_fn putc, void* user)
{
    if (!src) return -1;

    /* Count consts and measure code */
    uint32_t nconst = 0;
    uint32_t code_len = 0;
    const char* q = src;
    while (*q) {
        skip_space(&q);
        if (!*q) break;
        if (q[0] == ';' || q[0] == '#') {
            while (*q && *q != '\n') q++;
            continue;
        }
        if (starts_with_ci(q, "push")) {
            q += 4;
            skip_space(&q);
            if (*q == '"') {
                char dummy[4096];
                const char* end;
                if (parse_quoted_str(q, dummy, sizeof(dummy), &end) >= 0) {
                    nconst++;
                    q = end;
                }
            }
            code_len += 5; /* PUSH_STR + u32 */
        } else if (starts_with_ci(q, "lafillo")) {
            q += 7;
            code_len += 1; /* LAFILLO */
        } else if (starts_with_ci(q, "print")) {
            q += 5;
            code_len += 1; /* PRINT */
        } else if (starts_with_ci(q, "halt")) {
            q += 4;
            code_len += 1; /* HALT */
        } else {
            skip_line(&q);
        }
        if (*q == '\n') q++;
    }

    /* Build image */
    uint32_t img_size = 16;
    q = src;
    uint32_t ci = 0;
    while (*q && ci < nconst) {
        skip_space(&q);
        if (!*q) break;
        if (*q == ';' || *q == '#') { while (*q && *q != '\n') q++; continue; }
        if (starts_with_ci(q, "push")) {
            q += 4;
            skip_space(&q);
            if (*q == '"') {
                char buf[4096];
                const char* end;
                int len = parse_quoted_str(q, buf, sizeof(buf), &end);
                if (len >= 0) {
                    img_size += 4 + (uint32_t)len;
                    ci++;
                    q = end;
                }
            }
        } else {
            skip_line(&q);
        }
        if (*q == '\n') q++;
    }
    img_size += code_len;

    uint8_t* img = (uint8_t*)kmalloc(img_size);
    if (!img) return -15;

    uint32_t pos = 0;
    img[pos++] = 'D';
    img[pos++] = 'V';
    img[pos++] = 'M';
    img[pos++] = 0;
    img[pos++] = DVM_VERSION & 0xFF;
    img[pos++] = DVM_VERSION >> 8;
    img[pos++] = 0;
    img[pos++] = 0;
    pos += 4; /* const_count - fill later */
    img[pos++] = (uint8_t)(code_len);
    img[pos++] = (uint8_t)(code_len >> 8);
    img[pos++] = (uint8_t)(code_len >> 16);
    img[pos++] = (uint8_t)(code_len >> 24);
    nconst = 0;
    uint32_t code_start = 0;
    q = src;
    while (*q) {
        skip_space(&q);
        if (!*q) break;
        if (*q == ';' || *q == '#') { while (*q && *q != '\n') q++; continue; }
        if (starts_with_ci(q, "push")) {
            q += 4;
            skip_space(&q);
            if (*q == '"') {
                char buf[4096];
                const char* end;
                int len = parse_quoted_str(q, buf, sizeof(buf), &end);
                if (len >= 0 && pos + 4 + len <= img_size) {
                    img[pos++] = (uint8_t)(len);
                    img[pos++] = (uint8_t)(len >> 8);
                    img[pos++] = (uint8_t)(len >> 16);
                    img[pos++] = (uint8_t)(len >> 24);
                    for (int j = 0; j < len; j++) img[pos++] = (uint8_t)buf[j];
                    nconst++;
                    q = end;
                }
            }
        } else {
            skip_line(&q);
        }
        if (*q == '\n') q++;
    }
    code_start = pos;

    img[8]  = (uint8_t)(nconst);
    img[9]  = (uint8_t)(nconst >> 8);
    img[10] = (uint8_t)(nconst >> 16);
    img[11] = (uint8_t)(nconst >> 24);

    uint32_t codepos = 0;
    uint32_t push_idx = 0;
    q = src;
    while (*q && codepos < code_len) {
        skip_space(&q);
        if (!*q) break;
        if (*q == ';' || *q == '#') { while (*q && *q != '\n') q++; continue; }
        if (starts_with_ci(q, "push")) {
            q += 4;
            skip_space(&q);
            if (*q == '"') {
                char buf[4096];
                const char* end;
                if (parse_quoted_str(q, buf, sizeof(buf), &end) >= 0) {
                    img[code_start + codepos++] = 0x01; /* PUSH_STR */
                    img[code_start + codepos++] = (uint8_t)(push_idx);
                    img[code_start + codepos++] = (uint8_t)(push_idx >> 8);
                    img[code_start + codepos++] = (uint8_t)(push_idx >> 16);
                    img[code_start + codepos++] = (uint8_t)(push_idx >> 24);
                    push_idx++;
                    q = end;
                }
            }
        } else if (starts_with_ci(q, "lafillo")) {
            q += 7;
            img[code_start + codepos++] = 0x02; /* LAFILLO */
        } else if (starts_with_ci(q, "print")) {
            q += 5;
            img[code_start + codepos++] = 0x03; /* PRINT */
        } else if (starts_with_ci(q, "halt")) {
            q += 4;
            img[code_start + codepos++] = 0x00; /* HALT */
        } else {
            skip_line(&q);
        }
        if (*q == '\n') q++;
    }

    img_size = code_start + codepos;
    int r = lafillo_vm_run_io(img, img_size, putc, user);
    kfree(img);
    return r;
}
