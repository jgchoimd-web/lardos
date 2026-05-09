/*
 * os_vm.c - LardOS용 간단한 스택 VM
 *
 * Bytecode (LE):
 *   0x00: "OVM\0", 0x04: u16 ver(1), 0x08: u32 code_size
 *   0x0C: code
 *
 * Opcodes (스택: TOS = top):
 *   0x01 PUSH   i32   - push immediate
 *   0x02 ADD    - pop b,a push a+b
 *   0x03 SUB    - push a-b
 *   0x04 MUL    - push a*b
 *   0x05 DIV    - push a/b
 *   0x06 PRINT  - pop, output decimal + newline
 *   0x07 HALT   - stop
 *   0x08 JMP    i32   - pc = offset
 *   0x09 JZ     i32   - if TOS==0 pop & jump
 */
#include "os_vm.h"
#include "console.h"
#include "mem.h"
#include "vmmon.h"

#include <stdint.h>

#define OVM_MAGIC 0x004D564Fu  /* "OVM\0" LE */
#define OVM_VER 1
#define STACK_MAX 64

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_i32(const uint8_t* p)
{
    return (int32_t)rd_u32(p);
}

static void out_putc(os_vm_putc_fn putc, void* user, char c)
{
    if (putc) putc(c, user);
    else console_putc(c);
}

static void out_i32(os_vm_putc_fn putc, void* user, int32_t v)
{
    char buf[16];
    uint32_t n = 0;
    if (v == 0) {
        out_putc(putc, user, '0');
        return;
    }
    int neg = v < 0;
    uint32_t mag = neg ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v;
    while (mag) { buf[n++] = (char)('0' + mag % 10); mag /= 10; }
    if (neg) out_putc(putc, user, '-');
    while (n) out_putc(putc, user, buf[--n]);
}

int os_vm_run(const uint8_t* image, uint32_t size)
{
    return os_vm_run_io(image, size, 0, 0);
}

int os_vm_run_io(const uint8_t* image, uint32_t size, os_vm_putc_fn putc, void* user)
{
    if (!image || size < 12) {
        vmmon_record(VMMON_OSVM, 0, -1);
        return -1;
    }
    if (rd_u32(image) != OVM_MAGIC) {
        vmmon_record(VMMON_OSVM, 0, -2);
        return -2;
    }
    if ((uint16_t)(image[4] | (image[5] << 8)) != OVM_VER) {
        vmmon_record(VMMON_OSVM, 0, -3);
        return -3;
    }

    uint32_t code_size = rd_u32(image + 8);
    if (12 + code_size > size) {
        vmmon_record(VMMON_OSVM, 0, -4);
        return -4;
    }

    const uint8_t* code = image + 12;
    int32_t stack[STACK_MAX];
    uint32_t sp = 0;
    uint32_t pc = 0;
    uint32_t steps = 0;
    uint32_t budget = vmmon_budget(VMMON_OSVM);
    int rc = 0;

#define OVM_FINISH(code_) do { rc = (code_); goto ovm_done; } while (0)

    while (pc < code_size) {
        if (++steps > budget) {
            OVM_FINISH(-16);
        }
        uint8_t op = code[pc++];
        switch (op) {
        case 0x01: { /* PUSH */
            if (pc + 4 > code_size || sp >= STACK_MAX) OVM_FINISH(-5);
            stack[sp++] = rd_i32(code + pc);
            pc += 4;
            break;
        }
        case 0x02: /* ADD */
            if (sp < 2) OVM_FINISH(-6);
            stack[sp - 2] += stack[sp - 1];
            sp--;
            break;
        case 0x03: /* SUB */
            if (sp < 2) OVM_FINISH(-7);
            stack[sp - 2] -= stack[sp - 1];
            sp--;
            break;
        case 0x04: /* MUL */
            if (sp < 2) OVM_FINISH(-8);
            stack[sp - 2] *= stack[sp - 1];
            sp--;
            break;
        case 0x05: /* DIV */
            if (sp < 2 || stack[sp - 1] == 0) OVM_FINISH(-9);
            stack[sp - 2] /= stack[sp - 1];
            sp--;
            break;
        case 0x06: /* PRINT */
            if (sp == 0) OVM_FINISH(-10);
            out_i32(putc, user, stack[--sp]);
            out_putc(putc, user, '\n');
            break;
        case 0x07: /* HALT */
            OVM_FINISH(0);
        case 0x08: { /* JMP */
            if (pc + 4 > code_size) OVM_FINISH(-11);
            uint32_t target = rd_u32(code + pc);
            pc += 4;
            if (target >= code_size) OVM_FINISH(-12);
            pc = target;
            break;
        }
        case 0x09: { /* JZ */
            if (pc + 4 > code_size) OVM_FINISH(-13);
            if (sp == 0) OVM_FINISH(-14);
            int32_t v = stack[--sp];
            uint32_t target = rd_u32(code + pc);
            pc += 4;
            if (v == 0 && target < code_size) pc = target;
            break;
        }
        default:
            OVM_FINISH(-15);
        }
    }
    rc = 0;

ovm_done:
    vmmon_record(VMMON_OSVM, steps, rc);
#undef OVM_FINISH
    return rc;
}

/* Minimal inline assembler */
static int parse_int(const char* p, int32_t* out, const char** endp)
{
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') return -1;
    int32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    *endp = p;
    return 0;
}

int os_vm_asm_eval(const char* src, os_vm_putc_fn putc, void* user)
{
    if (!src) return -1;

    /* Count code size, collect labels */
    uint32_t code_len = 0;
    const char* p = src;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p || *p == ';' || *p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 'u' || p[1] == 'U') &&
            (p[2] == 's' || p[2] == 'S') && (p[3] == 'h' || p[3] == 'H')) {
            p += 4;
            int32_t v;
            const char* e;
            if (parse_int(p, &v, &e) == 0) { code_len += 5; p = e; }
        } else if ((p[0] == 'a' || p[0] == 'A') && (p[1] == 'd' || p[1] == 'D') && (p[2] == 'd' || p[2] == 'D')) {
            p += 3; code_len += 1;
        } else if ((p[0] == 's' || p[0] == 'S') && (p[1] == 'u' || p[1] == 'U') && (p[2] == 'b' || p[2] == 'B')) {
            p += 3; code_len += 1;
        } else if ((p[0] == 'm' || p[0] == 'M') && (p[1] == 'u' || p[1] == 'U') && (p[2] == 'l' || p[2] == 'L')) {
            p += 3; code_len += 1;
        } else if ((p[0] == 'd' || p[0] == 'D') && (p[1] == 'i' || p[1] == 'I') && (p[2] == 'v' || p[2] == 'V')) {
            p += 3; code_len += 1;
        } else if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 'r' || p[1] == 'R') && (p[2] == 'i' || p[2] == 'I') &&
                   (p[3] == 'n' || p[3] == 'N') && (p[4] == 't' || p[4] == 'T')) {
            p += 5; code_len += 1;
        } else if ((p[0] == 'h' || p[0] == 'H') && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'l' || p[2] == 'L') && (p[3] == 't' || p[3] == 'T')) {
            p += 4; code_len += 1;
        } else if ((p[0] == 'j' || p[0] == 'J') && (p[1] == 'm' || p[1] == 'M') && (p[2] == 'p' || p[2] == 'P')) {
            p += 3; code_len += 5;
            while (*p == ' ' || *p == '\t') p++;
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) p++;
            } else if (*p >= '0' || *p == '-') {
                int32_t target;
                parse_int(p, &target, &p);
            }
        } else if ((p[0] == 'j' || p[0] == 'J') && (p[1] == 'z' || p[1] == 'Z')) {
            p += 2; code_len += 5;
            while (*p == ' ' || *p == '\t') p++;
            if (*p >= '0' || *p == '-') {
                int32_t target;
                parse_int(p, &target, &p);
            }
        } else if (*p && *p != '\n' && p[1] == ':') {
            p += 2; /* label: skip */
        } else {
            while (*p && *p != '\n') p++;
        }
    }

    uint32_t img_size = 12 + code_len;
    uint8_t* img = (uint8_t*)kmalloc(img_size);
    if (!img) return -20;

    img[0] = 'O'; img[1] = 'V'; img[2] = 'M'; img[3] = 0;
    img[4] = OVM_VER & 0xFF; img[5] = OVM_VER >> 8;
    img[8] = (uint8_t)code_len; img[9] = (uint8_t)(code_len >> 8);
    img[10] = (uint8_t)(code_len >> 16); img[11] = (uint8_t)(code_len >> 24);

    uint8_t* c = img + 12;
    uint32_t pos = 0;
    p = src;

    while (*p && pos < code_len) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p || *p == ';' || *p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 'u') && (p[2] == 's') && (p[3] == 'h')) {
            p += 4;
            int32_t v;
            const char* e;
            if (parse_int(p, &v, &e) == 0) {
                c[pos++] = 0x01;
                c[pos++] = (uint8_t)((uint32_t)v);
                c[pos++] = (uint8_t)((uint32_t)v >> 8);
                c[pos++] = (uint8_t)((uint32_t)v >> 16);
                c[pos++] = (uint8_t)((uint32_t)v >> 24);
                p = e;
            }
        } else if ((p[0] == 'a' || p[0] == 'A') && (p[1] == 'd') && (p[2] == 'd')) {
            p += 3; c[pos++] = 0x02;
        } else if ((p[0] == 's' || p[0] == 'S') && (p[1] == 'u') && (p[2] == 'b')) {
            p += 3; c[pos++] = 0x03;
        } else if ((p[0] == 'm' || p[0] == 'M') && (p[1] == 'u') && (p[2] == 'l')) {
            p += 3; c[pos++] = 0x04;
        } else if ((p[0] == 'd' || p[0] == 'D') && (p[1] == 'i') && (p[2] == 'v')) {
            p += 3; c[pos++] = 0x05;
        } else if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 'r') && (p[2] == 'i') && (p[3] == 'n') && (p[4] == 't')) {
            p += 5; c[pos++] = 0x06;
        } else if ((p[0] == 'h' || p[0] == 'H') && (p[1] == 'a') && (p[2] == 'l') && (p[3] == 't')) {
            p += 4; c[pos++] = 0x07;
        } else if ((p[0] == 'j' || p[0] == 'J') && (p[1] == 'm') && (p[2] == 'p')) {
            p += 3;
            while (*p == ' ' || *p == '\t') p++;
            uint32_t target = 0;
            if (*p >= '0' || *p == '-') {
                int32_t t;
                parse_int(p, &t, &p);
                target = (uint32_t)t;
            }
            c[pos++] = 0x08;
            c[pos++] = (uint8_t)target;
            c[pos++] = (uint8_t)(target >> 8);
            c[pos++] = (uint8_t)(target >> 16);
            c[pos++] = (uint8_t)(target >> 24);
        } else if ((p[0] == 'j' || p[0] == 'J') && (p[1] == 'z')) {
            p += 2;
            while (*p == ' ' || *p == '\t') p++;
            uint32_t target = 0;
            if (*p >= '0' || *p == '-') {
                int32_t t;
                parse_int(p, &t, &p);
                target = (uint32_t)t;
            }
            c[pos++] = 0x09;
            c[pos++] = (uint8_t)target;
            c[pos++] = (uint8_t)(target >> 8);
            c[pos++] = (uint8_t)(target >> 16);
            c[pos++] = (uint8_t)(target >> 24);
        } else if (*p && *p != '\n' && p[1] == ':') {
            p += 2;
        } else {
            while (*p && *p != '\n') p++;
        }
    }

    int r = os_vm_run_io(img, 12 + pos, putc, user);
    kfree(img);
    return r;
}
