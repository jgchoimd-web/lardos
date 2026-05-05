#include "bosl_vm.h"

#include "console.h"
#include "lipc.h"
#include "lafillo.h"
#include "io.h"
#include "mem.h"
#include "rtc.h"

#include <stdint.h>

typedef enum {
    BOSL_VAL_INT = 1,
    BOSL_VAL_STR = 2,
    BOSL_VAL_INT64 = 3,
} bosl_val_type_t;

typedef struct {
    bosl_val_type_t type;
    union {
        int32_t i;
        int64_t i64;
        struct {
            const uint8_t* ptr;
            uint32_t len;
        } s;
    } as;
} bosl_val_t;

static uint16_t rd_u16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t rd_i32(const uint8_t* p)
{
    return (int32_t)rd_u32(p);
}

static int64_t rd_i64(const uint8_t* p)
{
    uint64_t u = (uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32);
    return (int64_t)u;
}

static int bosl_is_intish(const bosl_val_t* v)
{
    return v && (v->type == BOSL_VAL_INT || v->type == BOSL_VAL_INT64);
}

static int64_t bosl_to_i64(const bosl_val_t* v)
{
    if (!v) {
        return 0;
    }
    if (v->type == BOSL_VAL_INT) {
        return (int64_t)v->as.i;
    }
    if (v->type == BOSL_VAL_INT64) {
        return v->as.i64;
    }
    return 0;
}

static void* bosl_ptr_from_val(const bosl_val_t* v)
{
    if (!v || !bosl_is_intish(v)) {
        return 0;
    }
    return (void*)(uintptr_t)(uint64_t)bosl_to_i64(v);
}

static void io_write_i64(bosl_putc_fn putc, void* user, int64_t v)
{
    if (v == 0) {
        io_putc(putc, user, '0');
        return;
    }
    int neg = 0;
    uint64_t mag;
    if (v < 0) {
        neg = 1;
        mag = (uint64_t)(-(v + 1)) + 1u;
    } else {
        mag = (uint64_t)v;
    }
    char buf[24];
    uint32_t n = 0;
    while (mag && n < (uint32_t)sizeof(buf)) {
        buf[n++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    }
    if (neg) {
        io_putc(putc, user, '-');
    }
    while (n > 0) {
        io_putc(putc, user, buf[--n]);
    }
}

static void console_write_bytes(const uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        console_putc((char)p[i]);
    }
}

static void io_putc(bosl_putc_fn putc, void* user, char c)
{
    if (putc) {
        putc(c, user);
    } else {
        console_putc(c);
    }
}

static void io_write_bytes(bosl_putc_fn putc, void* user, const uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        io_putc(putc, user, (char)p[i]);
    }
}

static void io_write_i32(bosl_putc_fn putc, void* user, int32_t v)
{
    /* Minimal integer formatting: writes decimal, no heap. */
    char buf[16];
    uint32_t i = 0;
    uint32_t neg = 0;

    if (v == 0) {
        io_putc(putc, user, '0');
        return;
    }
    if (v < 0) {
        neg = 1;
        /* avoid UB on INT32_MIN by using uint32_t magnitude */
        uint32_t mag = (uint32_t)(-(v + 1)) + 1;
        while (mag && i < (uint32_t)sizeof(buf)) {
            buf[i++] = (char)('0' + (mag % 10u));
            mag /= 10u;
        }
    } else {
        uint32_t mag = (uint32_t)v;
        while (mag && i < (uint32_t)sizeof(buf)) {
            buf[i++] = (char)('0' + (mag % 10u));
            mag /= 10u;
        }
    }

    if (neg) {
        io_putc(putc, user, '-');
    }
    while (i > 0) {
        io_putc(putc, user, buf[--i]);
    }
}

/* Bytecode format (little endian):
 *   0x00: char magic[4] = "BOSL"
 *   0x04: u16 version (1)
 *   0x06: u16 flags (0)
 *   0x08: u32 const_count
 *   0x0C: u32 code_size
 *   0x10: u32 entry (byte offset into code, typically 0)
 *   0x14: constants...
 *
 * Constant entry:
 *   u32 type (1=str, 2=i32, 3=i64)
 *   str: u32 len, u8[len] bytes (no NUL)
 *   int: i32 value (4 bytes)
 *   i64: i64 value (8 bytes)
 *
 * Then code bytes.
 */

enum {
    OP_PUSHI = 0x01, /* i32 */
    OP_PUSHC = 0x02, /* u32 const index (string) */
    OP_PUSHK = 0x03, /* u32 const index (int constant pool) */
    OP_PUSHI64 = 0x04, /* i64 immediate (8 bytes) */
    OP_PUSHK64 = 0x05, /* u32 const index (int64 pool) */

    OP_ADD   = 0x10,
    OP_SUB   = 0x11,
    OP_MUL   = 0x12,
    OP_DIV   = 0x13,
    OP_MOD   = 0x14,
    OP_NEG   = 0x15,

    OP_AND   = 0x16,
    OP_OR    = 0x17,
    OP_XOR   = 0x18,
    OP_NOT   = 0x19,
    OP_SHL   = 0x1A,
    OP_SHR   = 0x1B,
    OP_USHR  = 0x1C,
    OP_ROL   = 0x1D, /* pop count, pop value; rotate left (width 32 or 64) */
    OP_ROR   = 0x1E, /* rotate right */

    OP_PRINT = 0x20,
    OP_DUP   = 0x21,
    OP_DROP  = 0x22,
    OP_SWAP  = 0x23,
    OP_OVER  = 0x24,
    OP_PICK  = 0x25, /* u8 depth (0 = top) */
    OP_I32TOI64 = 0x26,
    OP_I64TOI32 = 0x27,

    OP_ROT   = 0x28, /* (a b c -- b c a) */
    OP_NIP   = 0x29, /* (a b -- b) */
    OP_TUCK  = 0x2A, /* (x y -- y x y) */
    OP_DEPTH = 0x2B, /* push stack depth as i32 */
    OP_PRINTN = 0x2C, /* print without newline */
    OP_2DUP  = 0x2D, /* (a b -- a b a b) */
    OP_2DROP = 0x2E, /* (a b -- ) */
    OP_NOP   = 0x2F, /* no operation */

    OP_JMP   = 0x30, /* u32 absolute pc */
    OP_JZ    = 0x31, /* u32 absolute pc; pop int, jump if 0 */
    OP_JNZ   = 0x32, /* u32 absolute pc; pop int, jump if non-zero */
    OP_CALL  = 0x33, /* u32 absolute pc; push return PC */
    OP_RET   = 0x34,

    OP_EQ    = 0x40, /* pop b, pop a; push 1 if a==b else 0 */
    OP_NE    = 0x41,
    OP_LT    = 0x42, /* signed a < b */
    OP_LE    = 0x43,
    OP_GT    = 0x44,
    OP_GE    = 0x45,

    OP_ULT   = 0x46, /* unsigned compare, pop b, pop a */
    OP_ULE   = 0x47,
    OP_UGT   = 0x48,
    OP_UGE   = 0x49,
    OP_UDIV  = 0x4A,
    OP_UMOD  = 0x4B,
    OP_MIN   = 0x4C, /* pop b, pop a; push min(a,b) */
    OP_MAX   = 0x4D, /* pop b, pop a; push max(a,b) */
    OP_ABS   = 0x4E, /* abs(top) */
    OP_SGN   = 0x4F, /* top = -1|0|1 by sign */
    OP_INC   = 0x50, /* top += 1 */
    OP_DEC   = 0x51, /* top -= 1 */
    OP_EMIT  = 0x52, /* pop int; print as ASCII char */
    OP_QDUP  = 0x53, /* dup if top != 0 */

    /* Privileged / kernel (interpreter only; not JIT): addresses int32 or int64. */
    OP_PEEKU8  = 0x60, /* pop addr; push *addr (zero-extended byte) */
    OP_POKEU8  = 0x61, /* pop val, pop addr; *(u8*)addr = val */
    OP_PEEKU32 = 0x62,
    OP_POKEU32 = 0x63,
    OP_INB     = 0x64, /* pop port; push inb(port) */
    OP_OUTB    = 0x65, /* pop val, pop port; outb(port,val) */
    OP_INW     = 0x66,
    OP_OUTW    = 0x67,
    OP_INL     = 0x68,
    OP_OUTL    = 0x69,
    OP_CLI     = 0x6A,
    OP_STI     = 0x6B,
    OP_MEMFENCE = 0x6C, /* compiler barrier */
    OP_PEEKU16 = 0x6D,
    OP_POKEU16 = 0x6E,
    OP_PEEKU64 = 0x6F,
    OP_POKEU64 = 0x70,

    OP_MEMCPY = 0x71, /* pop n, dst, src; memmove-compatible copy */
    OP_MEMSET = 0x72, /* pop n, c, dst; fill bytes */
    OP_CPU_PAUSE = 0x73,
    OP_MFENCE_CPU = 0x74,
    OP_LFENCE_CPU = 0x75,
    OP_SFENCE_CPU = 0x76,
    OP_LAFILLO_PRINT = 0x77, /* pop str; HTML->text via lafillo_http_to_text, output to putc */

    OP_SLEN  = 0x78, /* pop str; push length as i32 */
    OP_WITHIN = 0x79, /* pop hi, lo, x; push 1 if lo <= x < hi else 0 */
    OP_2SWAP = 0x7A, /* (a b c d -- c d a b) swap two pairs */
    OP_RAND  = 0x7B, /* push pseudo-random i32 (0 to 0x7FFF) */
    OP_TIME  = 0x7C, /* push unix seconds (i64) */
    OP_LIPC_SEND = 0x7D, /* pop len, buf ptr, port; push i32 result (see lipc.h) */
    OP_LIPC_RECV = 0x7E, /* pop cap, buf ptr, port; push i32 bytes or status */
    OP_LIPC_PENDING = 0x7F, /* pop port; push i32 pending message count */
    OP_LIPC_SEND_STR = 0x90, /* pop string, port; send pool string bytes (push i32 result) */

    OP_HALT  = 0xFF,
};

enum { BOSL_CONST_STR = 1, BOSL_CONST_INT = 2, BOSL_CONST_INT64 = 3 };

typedef struct {
    uint32_t type;
    union {
        struct {
            const uint8_t* ptr;
            uint32_t len;
        } s;
        int32_t i;
        int64_t i64;
    } u;
} bosl_const_t;

static int bosl_vm_run_impl(const uint8_t* image, uint32_t size, bosl_putc_fn putc, void* user)
{
    if (!image || size < 0x14) {
        return 1;
    }

    if (!(image[0] == 'B' && image[1] == 'O' && image[2] == 'S' && image[3] == 'L')) {
        kprintf("bosl: bad magic\n");
        return 2;
    }

    uint16_t version = rd_u16(&image[4]);
    if (version != 1) {
        kprintf("bosl: unsupported version %u\n", (uint32_t)version);
        return 3;
    }

    /* flags currently unused */
    (void)rd_u16(&image[6]);

    uint32_t const_count = rd_u32(&image[8]);
    uint32_t code_size = rd_u32(&image[12]);
    uint32_t entry = rd_u32(&image[16]);

    uint32_t off = 0x14;

    bosl_const_t* consts = 0;
    if (const_count > 0) {
        consts = (bosl_const_t*)kmalloc(const_count * (uint32_t)sizeof(bosl_const_t));
        if (!consts) {
            kprintf("bosl: out of memory (const table)\n");
            return 4;
        }
    }

    for (uint32_t i = 0; i < const_count; i++) {
        if (off + 8 > size) {
            kprintf("bosl: const table truncated\n");
            kfree(consts);
            return 5;
        }
        uint32_t ctype = rd_u32(&image[off]);
        off += 4;

        if (ctype == BOSL_CONST_STR) {
            uint32_t clen = rd_u32(&image[off]);
            off += 4;
            if (off + clen > size) {
                kprintf("bosl: const data truncated\n");
                kfree(consts);
                return 6;
            }
            consts[i].type = BOSL_CONST_STR;
            consts[i].u.s.ptr = &image[off];
            consts[i].u.s.len = clen;
            off += clen;
        } else if (ctype == BOSL_CONST_INT) {
            if (off + 4 > size) {
                kprintf("bosl: const int truncated\n");
                kfree(consts);
                return 6;
            }
            consts[i].type = BOSL_CONST_INT;
            consts[i].u.i = rd_i32(&image[off]);
            off += 4;
        } else if (ctype == BOSL_CONST_INT64) {
            if (off + 8 > size) {
                kprintf("bosl: const i64 truncated\n");
                kfree(consts);
                return 6;
            }
            consts[i].type = BOSL_CONST_INT64;
            consts[i].u.i64 = rd_i64(&image[off]);
            off += 8;
        } else {
            kprintf("bosl: unsupported const type %u\n", ctype);
            kfree(consts);
            return 7;
        }
    }

    if (off + code_size > size) {
        kprintf("bosl: code truncated (need %u bytes)\n", code_size);
        kfree(consts);
        return 8;
    }

    const uint8_t* code = &image[off];
    if (entry >= code_size) {
        kprintf("bosl: bad entry %u\n", entry);
        kfree(consts);
        return 9;
    }

    const uint32_t STACK_MAX = 256;
    bosl_val_t* stack = (bosl_val_t*)kmalloc(STACK_MAX * (uint32_t)sizeof(bosl_val_t));
    if (!stack) {
        kprintf("bosl: out of memory (stack)\n");
        kfree(consts);
            return 10;
    }
    uint32_t sp = 0;

#define BOSL_RET_STACK 256
    uint32_t ret_pc[BOSL_RET_STACK];
    uint32_t rs = 0;

    uint32_t pc = entry;
    uint32_t steps = 0;
    const uint32_t STEP_LIMIT = 1000000; /* prevents infinite loops from hanging the OS */

    for (;;) {
        if (steps++ > STEP_LIMIT) {
            kprintf("bosl: step limit exceeded\n");
            kfree(stack);
            kfree(consts);
            return 11;
        }

        if (pc >= code_size) {
            kprintf("bosl: pc out of range\n");
            kfree(stack);
            kfree(consts);
            return 12;
        }

        uint8_t op = code[pc++];
        switch (op) {
        case OP_PUSHI: {
            if (pc + 4 > code_size || sp >= STACK_MAX) {
                kprintf("bosl: pushi overflow\n");
                goto fail_runtime;
            }
            int32_t v = rd_i32(&code[pc]);
            pc += 4;
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = v;
            sp++;
            break;
        }
        case OP_PUSHC: {
            if (pc + 4 > code_size || sp >= STACK_MAX) {
                kprintf("bosl: pushc overflow\n");
                goto fail_runtime;
            }
            uint32_t idx = rd_u32(&code[pc]);
            pc += 4;
            if (idx >= const_count) {
                kprintf("bosl: bad const index %u\n", idx);
                goto fail_runtime;
            }
            if (consts[idx].type != BOSL_CONST_STR) {
                kprintf("bosl: pushc needs string const\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_STR;
            stack[sp].as.s.ptr = consts[idx].u.s.ptr;
            stack[sp].as.s.len = consts[idx].u.s.len;
            sp++;
            break;
        }
        case OP_PUSHK: {
            if (pc + 4 > code_size || sp >= STACK_MAX) {
                kprintf("bosl: pushk overflow\n");
                goto fail_runtime;
            }
            uint32_t idx = rd_u32(&code[pc]);
            pc += 4;
            if (idx >= const_count) {
                kprintf("bosl: bad const index %u\n", idx);
                goto fail_runtime;
            }
            if (consts[idx].type != BOSL_CONST_INT) {
                kprintf("bosl: pushk needs int const\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = consts[idx].u.i;
            sp++;
            break;
        }
        case OP_PUSHI64: {
            if (pc + 8 > code_size || sp >= STACK_MAX) {
                kprintf("bosl: pushi64 overflow\n");
                goto fail_runtime;
            }
            int64_t v = rd_i64(&code[pc]);
            pc += 8;
            stack[sp].type = BOSL_VAL_INT64;
            stack[sp].as.i64 = v;
            sp++;
            break;
        }
        case OP_PUSHK64: {
            if (pc + 4 > code_size || sp >= STACK_MAX) {
                kprintf("bosl: pushk64 overflow\n");
                goto fail_runtime;
            }
            uint32_t idx = rd_u32(&code[pc]);
            pc += 4;
            if (idx >= const_count) {
                kprintf("bosl: bad const index %u\n", idx);
                goto fail_runtime;
            }
            if (consts[idx].type != BOSL_CONST_INT64) {
                kprintf("bosl: pushk64 needs i64 const\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT64;
            stack[sp].as.i64 = consts[idx].u.i64;
            sp++;
            break;
        }
        case OP_DUP: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: dup under/overflow\n");
                goto fail_runtime;
            }
            stack[sp] = stack[sp - 1];
            sp++;
            break;
        }
        case OP_DROP: {
            if (sp == 0) {
                kprintf("bosl: drop underflow\n");
                goto fail_runtime;
            }
            sp--;
            break;
        }
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD: {
            if (sp < 2) {
                kprintf("bosl: arithmetic underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: arithmetic expects int or i64\n");
                goto fail_runtime;
            }
            int use64 = (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64);
            if (use64) {
                int64_t la = bosl_to_i64(&a);
                int64_t lb = bosl_to_i64(&b);
                int64_t r = 0;
                if (op == OP_ADD) r = la + lb;
                else if (op == OP_SUB) r = la - lb;
                else if (op == OP_MUL) r = la * lb;
                else {
                    if (lb == 0) {
                        kprintf("bosl: divide by zero\n");
                        goto fail_runtime;
                    }
                    if (op == OP_DIV) r = la / lb;
                    else r = la % lb;
                }
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = r;
            } else {
                int32_t r = 0;
                if (op == OP_ADD) r = a.as.i + b.as.i;
                else if (op == OP_SUB) r = a.as.i - b.as.i;
                else if (op == OP_MUL) r = a.as.i * b.as.i;
                else {
                    if (b.as.i == 0) {
                        kprintf("bosl: divide by zero\n");
                        goto fail_runtime;
                    }
                    if (op == OP_DIV) r = a.as.i / b.as.i;
                    else r = a.as.i % b.as.i;
                }
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = r;
            }
            sp++;
            break;
        }
        case OP_NEG: {
            if (sp == 0) {
                kprintf("bosl: neg underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type == BOSL_VAL_INT) {
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = -v.as.i;
            } else if (v.type == BOSL_VAL_INT64) {
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = -v.as.i64;
            } else {
                kprintf("bosl: neg expects int\n");
                goto fail_runtime;
            }
            sp++;
            break;
        }
        case OP_AND:
        case OP_OR:
        case OP_XOR: {
            if (sp < 2) {
                kprintf("bosl: bitwise underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: bitwise expects int or i64\n");
                goto fail_runtime;
            }
            if (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64) {
                int64_t la = bosl_to_i64(&a);
                int64_t lb = bosl_to_i64(&b);
                int64_t r = 0;
                if (op == OP_AND) r = la & lb;
                else if (op == OP_OR) r = la | lb;
                else r = la ^ lb;
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = r;
            } else {
                int32_t r = 0;
                if (op == OP_AND) r = a.as.i & b.as.i;
                else if (op == OP_OR) r = a.as.i | b.as.i;
                else r = a.as.i ^ b.as.i;
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = r;
            }
            sp++;
            break;
        }
        case OP_NOT: {
            if (sp == 0) {
                kprintf("bosl: not underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type == BOSL_VAL_INT) {
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = ~v.as.i;
            } else if (v.type == BOSL_VAL_INT64) {
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = ~v.as.i64;
            } else {
                kprintf("bosl: not expects int\n");
                goto fail_runtime;
            }
            sp++;
            break;
        }
        case OP_SHL:
        case OP_SHR:
        case OP_USHR: {
            if (sp < 2) {
                kprintf("bosl: shift underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: shift expects int or i64\n");
                goto fail_runtime;
            }
            if (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64) {
                int64_t la = bosl_to_i64(&a);
                uint32_t sh = (uint32_t)(bosl_to_i64(&b) & 63LL);
                int64_t r = 0;
                if (op == OP_SHL) r = la << sh;
                else if (op == OP_SHR) r = la >> sh;
                else r = (int64_t)((uint64_t)la >> sh);
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = r;
            } else {
                uint32_t sh = (uint32_t)b.as.i & 31u;
                int32_t r = 0;
                if (op == OP_SHL) r = a.as.i << sh;
                else if (op == OP_SHR) r = a.as.i >> sh;
                else r = (int32_t)((uint32_t)a.as.i >> sh);
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = r;
            }
            sp++;
            break;
        }
        case OP_ROL:
        case OP_ROR: {
            if (sp < 2) {
                kprintf("bosl: rol/ror underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: rol/ror expects int or i64\n");
                goto fail_runtime;
            }
            if (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64) {
                uint64_t v = (uint64_t)bosl_to_i64(&a);
                uint32_t k = (uint32_t)(bosl_to_i64(&b) & 63LL);
                if (op == OP_ROL) {
                    v = (v << k) | (v >> ((64 - k) & 63u));
                } else {
                    v = (v >> k) | (v << ((64 - k) & 63u));
                }
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = (int64_t)v;
            } else {
                uint32_t v = (uint32_t)a.as.i;
                uint32_t k = (uint32_t)b.as.i & 31u;
                if (op == OP_ROL) {
                    v = (v << k) | (v >> ((32 - k) & 31u));
                } else {
                    v = (v >> k) | (v << ((32 - k) & 31u));
                }
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = (int32_t)v;
            }
            sp++;
            break;
        }
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE: {
            if (sp < 2) {
                kprintf("bosl: compare underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: compare expects int or i64\n");
                goto fail_runtime;
            }
            int64_t ai = bosl_to_i64(&a);
            int64_t bi = bosl_to_i64(&b);
            int32_t r = 0;
            if (op == OP_EQ) r = (ai == bi) ? 1 : 0;
            else if (op == OP_NE) r = (ai != bi) ? 1 : 0;
            else if (op == OP_LT) r = (ai < bi) ? 1 : 0;
            else if (op == OP_LE) r = (ai <= bi) ? 1 : 0;
            else if (op == OP_GT) r = (ai > bi) ? 1 : 0;
            else r = (ai >= bi) ? 1 : 0;
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = r;
            sp++;
            break;
        }
        case OP_ULT:
        case OP_ULE:
        case OP_UGT:
        case OP_UGE: {
            if (sp < 2) {
                kprintf("bosl: unsigned compare underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: unsigned compare expects int or i64\n");
                goto fail_runtime;
            }
            int32_t r = 0;
            if (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64) {
                uint64_t ua = (uint64_t)bosl_to_i64(&a);
                uint64_t ub = (uint64_t)bosl_to_i64(&b);
                if (op == OP_ULT) r = (ua < ub) ? 1 : 0;
                else if (op == OP_ULE) r = (ua <= ub) ? 1 : 0;
                else if (op == OP_UGT) r = (ua > ub) ? 1 : 0;
                else r = (ua >= ub) ? 1 : 0;
            } else {
                uint32_t ua = (uint32_t)a.as.i;
                uint32_t ub = (uint32_t)b.as.i;
                if (op == OP_ULT) r = (ua < ub) ? 1 : 0;
                else if (op == OP_ULE) r = (ua <= ub) ? 1 : 0;
                else if (op == OP_UGT) r = (ua > ub) ? 1 : 0;
                else r = (ua >= ub) ? 1 : 0;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = r;
            sp++;
            break;
        }
        case OP_UDIV:
        case OP_UMOD: {
            if (sp < 2) {
                kprintf("bosl: udiv/umod underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: udiv/umod expects int or i64\n");
                goto fail_runtime;
            }
            if (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64) {
                uint64_t ua = (uint64_t)bosl_to_i64(&a);
                uint64_t ub = (uint64_t)bosl_to_i64(&b);
                if (ub == 0) {
                    kprintf("bosl: udiv by zero\n");
                    goto fail_runtime;
                }
                uint64_t rr = (op == OP_UDIV) ? (ua / ub) : (ua % ub);
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = (int64_t)rr;
            } else {
                uint32_t ua = (uint32_t)a.as.i;
                uint32_t ub = (uint32_t)b.as.i;
                if (ub == 0) {
                    kprintf("bosl: udiv by zero\n");
                    goto fail_runtime;
                }
                uint32_t rr = (op == OP_UDIV) ? (ua / ub) : (ua % ub);
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = (int32_t)rr;
            }
            sp++;
            break;
        }
        case OP_MIN:
        case OP_MAX: {
            if (sp < 2) {
                kprintf("bosl: min/max underflow\n");
                goto fail_runtime;
            }
            bosl_val_t b = stack[--sp];
            bosl_val_t a = stack[--sp];
            if (!bosl_is_intish(&a) || !bosl_is_intish(&b)) {
                kprintf("bosl: min/max expects int or i64\n");
                goto fail_runtime;
            }
            int64_t la = bosl_to_i64(&a);
            int64_t lb = bosl_to_i64(&b);
            int64_t r = (op == OP_MIN) ? (la < lb ? la : lb) : (la > lb ? la : lb);
            if (a.type == BOSL_VAL_INT64 || b.type == BOSL_VAL_INT64) {
                stack[sp].type = BOSL_VAL_INT64;
                stack[sp].as.i64 = r;
            } else {
                stack[sp].type = BOSL_VAL_INT;
                stack[sp].as.i = (int32_t)r;
            }
            sp++;
            break;
        }
        case OP_ABS: {
            if (sp == 0) {
                kprintf("bosl: abs underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[sp - 1];
            if (v.type == BOSL_VAL_INT) {
                stack[sp - 1].as.i = v.as.i < 0 ? -v.as.i : v.as.i;
            } else if (v.type == BOSL_VAL_INT64) {
                stack[sp - 1].as.i64 = v.as.i64 < 0 ? -v.as.i64 : v.as.i64;
            } else {
                kprintf("bosl: abs expects int or i64\n");
                goto fail_runtime;
            }
            break;
        }
        case OP_SGN: {
            if (sp == 0) {
                kprintf("bosl: sgn underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[sp - 1];
            int64_t s;
            if (v.type == BOSL_VAL_INT) {
                s = v.as.i < 0 ? -1 : (v.as.i > 0 ? 1 : 0);
                stack[sp - 1].type = BOSL_VAL_INT;
                stack[sp - 1].as.i = (int32_t)s;
            } else if (v.type == BOSL_VAL_INT64) {
                s = v.as.i64 < 0 ? -1 : (v.as.i64 > 0 ? 1 : 0);
                stack[sp - 1].type = BOSL_VAL_INT64;
                stack[sp - 1].as.i64 = s;
            } else {
                kprintf("bosl: sgn expects int or i64\n");
                goto fail_runtime;
            }
            break;
        }
        case OP_INC:
        case OP_DEC: {
            if (sp == 0) {
                kprintf("bosl: inc/dec underflow\n");
                goto fail_runtime;
            }
            bosl_val_t* v = &stack[sp - 1];
            if (v->type == BOSL_VAL_INT) {
                v->as.i += (op == OP_INC) ? 1 : -1;
            } else if (v->type == BOSL_VAL_INT64) {
                v->as.i64 += (op == OP_INC) ? 1 : -1;
            } else {
                kprintf("bosl: inc/dec expects int or i64\n");
                goto fail_runtime;
            }
            break;
        }
        case OP_EMIT: {
            if (sp == 0) {
                kprintf("bosl: emit underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (!bosl_is_intish(&v)) {
                kprintf("bosl: emit expects int\n");
                goto fail_runtime;
            }
            char c = (char)(bosl_to_i64(&v) & 0xFF);
            io_putc(putc, user, c);
            break;
        }
        case OP_QDUP: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: ?dup underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[sp - 1];
            if (!bosl_is_intish(&v)) {
                kprintf("bosl: ?dup expects int or i64\n");
                goto fail_runtime;
            }
            if (bosl_to_i64(&v) != 0) {
                stack[sp] = stack[sp - 1];
                sp++;
            }
            break;
        }
        case OP_SWAP: {
            if (sp < 2) {
                kprintf("bosl: swap underflow\n");
                goto fail_runtime;
            }
            bosl_val_t t = stack[sp - 1];
            stack[sp - 1] = stack[sp - 2];
            stack[sp - 2] = t;
            break;
        }
        case OP_OVER: {
            if (sp < 2 || sp >= STACK_MAX) {
                kprintf("bosl: over under/overflow\n");
                goto fail_runtime;
            }
            stack[sp] = stack[sp - 2];
            sp++;
            break;
        }
        case OP_ROT: {
            if (sp < 3) {
                kprintf("bosl: rot underflow\n");
                goto fail_runtime;
            }
            bosl_val_t a = stack[sp - 3];
            bosl_val_t b = stack[sp - 2];
            bosl_val_t c = stack[sp - 1];
            stack[sp - 3] = b;
            stack[sp - 2] = c;
            stack[sp - 1] = a;
            break;
        }
        case OP_NIP: {
            if (sp < 2) {
                kprintf("bosl: nip underflow\n");
                goto fail_runtime;
            }
            stack[sp - 2] = stack[sp - 1];
            sp--;
            break;
        }
        case OP_TUCK: {
            if (sp < 2 || sp >= STACK_MAX) {
                kprintf("bosl: tuck under/overflow\n");
                goto fail_runtime;
            }
            bosl_val_t x = stack[sp - 2];
            bosl_val_t y = stack[sp - 1];
            stack[sp - 2] = y;
            stack[sp - 1] = x;
            stack[sp] = y;
            sp++;
            break;
        }
        case OP_DEPTH: {
            if (sp >= STACK_MAX) {
                kprintf("bosl: depth overflow\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)sp;
            sp++;
            break;
        }
        case OP_PICK: {
            if (pc + 1 > code_size || sp >= STACK_MAX) {
                kprintf("bosl: pick truncated\n");
                goto fail_runtime;
            }
            uint8_t depth = code[pc++];
            if ((uint32_t)depth + 1u > sp) {
                kprintf("bosl: pick underflow\n");
                goto fail_runtime;
            }
            stack[sp] = stack[sp - 1u - (uint32_t)depth];
            sp++;
            break;
        }
        case OP_I32TOI64: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: i32toi64 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type != BOSL_VAL_INT) {
                kprintf("bosl: i32toi64 expects i32\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT64;
            stack[sp].as.i64 = (int64_t)v.as.i;
            sp++;
            break;
        }
        case OP_I64TOI32: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: i64toi32 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type != BOSL_VAL_INT64) {
                kprintf("bosl: i64toi32 expects i64\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)v.as.i64;
            sp++;
            break;
        }
        case OP_PRINT: {
            if (sp == 0) {
                kprintf("bosl: print underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type == BOSL_VAL_INT) {
                io_write_i32(putc, user, v.as.i);
                io_putc(putc, user, '\n');
            } else if (v.type == BOSL_VAL_INT64) {
                io_write_i64(putc, user, v.as.i64);
                io_putc(putc, user, '\n');
            } else if (v.type == BOSL_VAL_STR) {
                io_write_bytes(putc, user, v.as.s.ptr, v.as.s.len);
            } else {
                kprintf("bosl: unknown value type\n");
                goto fail_runtime;
            }
            break;
        }
        case OP_PRINTN: {
            if (sp == 0) {
                kprintf("bosl: printn underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type == BOSL_VAL_INT) {
                io_write_i32(putc, user, v.as.i);
            } else if (v.type == BOSL_VAL_INT64) {
                io_write_i64(putc, user, v.as.i64);
            } else if (v.type == BOSL_VAL_STR) {
                io_write_bytes(putc, user, v.as.s.ptr, v.as.s.len);
            } else {
                kprintf("bosl: printn unknown value type\n");
                goto fail_runtime;
            }
            break;
        }
        case OP_2DUP: {
            if (sp < 2 || sp + 2 > STACK_MAX) {
                kprintf("bosl: 2dup under/overflow\n");
                goto fail_runtime;
            }
            stack[sp] = stack[sp - 2];
            stack[sp + 1] = stack[sp - 1];
            sp += 2;
            break;
        }
        case OP_2DROP: {
            if (sp < 2) {
                kprintf("bosl: 2drop underflow\n");
                goto fail_runtime;
            }
            sp -= 2;
            break;
        }
        case OP_LAFILLO_PRINT: {
            if (sp == 0) {
                kprintf("bosl: lafillop underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type != BOSL_VAL_STR) {
                kprintf("bosl: lafillop needs string\n");
                goto fail_runtime;
            }
            {
                static char out_buf[4096];
                uint32_t in_len = v.as.s.len;
                if (in_len > 4096) in_len = 4096;
                if (lafillo_http_to_text((const char*)v.as.s.ptr, in_len, out_buf, sizeof(out_buf)) == 0) {
                    uint32_t i = 0;
                    while (out_buf[i] && i < sizeof(out_buf) - 1) {
                        io_putc(putc, user, out_buf[i]);
                        i++;
                    }
                }
            }
            break;
        }
        case OP_SLEN: {
            if (sp == 0) {
                kprintf("bosl: slen underflow\n");
                goto fail_runtime;
            }
            bosl_val_t v = stack[--sp];
            if (v.type != BOSL_VAL_STR) {
                kprintf("bosl: slen expects string\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)v.as.s.len;
            sp++;
            break;
        }
        case OP_WITHIN: {
            if (sp < 3) {
                kprintf("bosl: within underflow\n");
                goto fail_runtime;
            }
            bosl_val_t xv = stack[--sp];
            bosl_val_t lov = stack[--sp];
            bosl_val_t hiv = stack[--sp];
            if (!bosl_is_intish(&xv) || !bosl_is_intish(&lov) || !bosl_is_intish(&hiv)) {
                kprintf("bosl: within expects ints\n");
                goto fail_runtime;
            }
            int64_t x = bosl_to_i64(&xv);
            int64_t lo = bosl_to_i64(&lov);
            int64_t hi = bosl_to_i64(&hiv);
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (lo <= x && x < hi) ? 1 : 0;
            sp++;
            break;
        }
        case OP_2SWAP: {
            if (sp < 4) {
                kprintf("bosl: 2swap underflow\n");
                goto fail_runtime;
            }
            bosl_val_t a = stack[sp - 4];
            bosl_val_t b = stack[sp - 3];
            bosl_val_t c = stack[sp - 2];
            bosl_val_t d = stack[sp - 1];
            stack[sp - 4] = c;
            stack[sp - 3] = d;
            stack[sp - 2] = a;
            stack[sp - 1] = b;
            break;
        }
        case OP_RAND: {
            if (sp >= STACK_MAX) {
                kprintf("bosl: rand overflow\n");
                goto fail_runtime;
            }
            /* Simple LCG: s = (s * 1103515245 + 12345) & 0x7FFFFFFF */
            static uint32_t s_rand = 0;
            if (s_rand == 0) {
                int64_t t = rtc_unix_seconds();
                s_rand = (uint32_t)(t ^ (t >> 32)) | 1u;
            }
            s_rand = (uint32_t)((uint64_t)s_rand * 1103515245u + 12345u) & 0x7FFFFFFFu;
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)(s_rand & 0x7FFFu);
            sp++;
            break;
        }
        case OP_TIME: {
            if (sp >= STACK_MAX) {
                kprintf("bosl: time overflow\n");
                goto fail_runtime;
            }
            int64_t t = rtc_unix_seconds();
            stack[sp].type = BOSL_VAL_INT64;
            stack[sp].as.i64 = t;
            sp++;
            break;
        }
        case OP_LIPC_SEND: {
            if (sp < 3) {
                kprintf("bosl: lipc_send underflow\n");
                goto fail_runtime;
            }
            bosl_val_t lenv = stack[--sp];
            bosl_val_t bufv = stack[--sp];
            bosl_val_t portv = stack[--sp];
            if (!bosl_is_intish(&lenv) || !bosl_is_intish(&bufv) || !bosl_is_intish(&portv)) {
                kprintf("bosl: lipc_send bad args\n");
                goto fail_runtime;
            }
            uint32_t plen = (uint32_t)bosl_to_i64(&lenv);
            void* pbuf = bosl_ptr_from_val(&bufv);
            uint32_t port = (uint32_t)bosl_to_i64(&portv);
            int r = (pbuf && plen) ? lipc_send(port, pbuf, plen) : -1;
            if (sp >= STACK_MAX) {
                kprintf("bosl: lipc_send overflow\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = r;
            sp++;
            break;
        }
        case OP_LIPC_RECV: {
            if (sp < 3) {
                kprintf("bosl: lipc_recv underflow\n");
                goto fail_runtime;
            }
            bosl_val_t capv = stack[--sp];
            bosl_val_t bufv = stack[--sp];
            bosl_val_t portv = stack[--sp];
            if (!bosl_is_intish(&capv) || !bosl_is_intish(&bufv) || !bosl_is_intish(&portv)) {
                kprintf("bosl: lipc_recv bad args\n");
                goto fail_runtime;
            }
            uint32_t cap = (uint32_t)bosl_to_i64(&capv);
            void* pbuf = bosl_ptr_from_val(&bufv);
            uint32_t port = (uint32_t)bosl_to_i64(&portv);
            int r = (pbuf && cap) ? lipc_recv(port, pbuf, cap) : -1;
            if (sp >= STACK_MAX) {
                kprintf("bosl: lipc_recv overflow\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = r;
            sp++;
            break;
        }
        case OP_LIPC_PENDING: {
            if (sp < 1) {
                kprintf("bosl: lipc_pending underflow\n");
                goto fail_runtime;
            }
            bosl_val_t portv = stack[--sp];
            if (!bosl_is_intish(&portv)) {
                kprintf("bosl: lipc_pending bad arg\n");
                goto fail_runtime;
            }
            uint32_t port = (uint32_t)bosl_to_i64(&portv);
            uint32_t n = lipc_pending(port);
            if (n > 0x7FFFFFFFu) {
                n = 0x7FFFFFFFu;
            }
            if (sp >= STACK_MAX) {
                kprintf("bosl: lipc_pending overflow\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)n;
            sp++;
            break;
        }
        case OP_LIPC_SEND_STR: {
            if (sp < 2) {
                kprintf("bosl: lipc_send_str underflow\n");
                goto fail_runtime;
            }
            bosl_val_t strv = stack[--sp];
            bosl_val_t portv = stack[--sp];
            if (!bosl_is_intish(&portv) || strv.type != BOSL_VAL_STR) {
                kprintf("bosl: lipc_send_str bad args\n");
                goto fail_runtime;
            }
            uint32_t port = (uint32_t)bosl_to_i64(&portv);
            int r = lipc_send(port, strv.as.s.ptr, strv.as.s.len);
            if (sp >= STACK_MAX) {
                kprintf("bosl: lipc_send_str overflow\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = r;
            sp++;
            break;
        }
        case OP_NOP:
            break;
        case OP_CALL: {
            if (pc + 4 > code_size) {
                kprintf("bosl: call truncated\n");
                goto fail_runtime;
            }
            if (rs >= BOSL_RET_STACK) {
                kprintf("bosl: call stack overflow\n");
                goto fail_runtime;
            }
            uint32_t dst = rd_u32(&code[pc]);
            pc += 4;
            ret_pc[rs++] = pc;
            pc = dst;
            break;
        }
        case OP_RET: {
            if (rs == 0) {
                kprintf("bosl: ret underflow\n");
                goto fail_runtime;
            }
            pc = ret_pc[--rs];
            break;
        }
        case OP_JMP: {
            if (pc + 4 > code_size) {
                kprintf("bosl: jmp truncated\n");
                goto fail_runtime;
            }
            uint32_t dst = rd_u32(&code[pc]);
            pc = dst;
            break;
        }
        case OP_JZ:
        case OP_JNZ: {
            if (pc + 4 > code_size) {
                kprintf("bosl: branch truncated\n");
                goto fail_runtime;
            }
            uint32_t dst = rd_u32(&code[pc]);
            pc += 4;
            if (sp == 0) {
                kprintf("bosl: branch underflow\n");
                goto fail_runtime;
            }
            bosl_val_t cond = stack[--sp];
            if (!bosl_is_intish(&cond)) {
                kprintf("bosl: branch expects int or i64\n");
                goto fail_runtime;
            }
            int64_t cv = bosl_to_i64(&cond);
            int take = (op == OP_JZ) ? (cv == 0) : (cv != 0);
            if (take) {
                pc = dst;
            }
            break;
        }
        case OP_PEEKU8: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: peeku8 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad)) {
                kprintf("bosl: peeku8 expects int addr\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: peeku8 bad addr\n");
                goto fail_runtime;
            }
            volatile uint8_t* p = (volatile uint8_t*)pa;
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)(uint32_t)*p;
            sp++;
            break;
        }
        case OP_POKEU8: {
            if (sp < 2) {
                kprintf("bosl: pokeu8 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad) || !bosl_is_intish(&val)) {
                kprintf("bosl: pokeu8 expects int addr, int val\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: pokeu8 bad addr\n");
                goto fail_runtime;
            }
            volatile uint8_t* p = (volatile uint8_t*)pa;
            *p = (uint8_t)(bosl_to_i64(&val) & 0xFF);
            break;
        }
        case OP_PEEKU32: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: peeku32 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad)) {
                kprintf("bosl: peeku32 expects int addr\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: peeku32 bad addr\n");
                goto fail_runtime;
            }
            volatile uint32_t* p = (volatile uint32_t*)pa;
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)(*p);
            sp++;
            break;
        }
        case OP_POKEU32: {
            if (sp < 2) {
                kprintf("bosl: pokeu32 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad) || !bosl_is_intish(&val)) {
                kprintf("bosl: pokeu32 expects int addr, int val\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: pokeu32 bad addr\n");
                goto fail_runtime;
            }
            volatile uint32_t* p = (volatile uint32_t*)pa;
            *p = (uint32_t)bosl_to_i64(&val);
            break;
        }
        case OP_PEEKU16: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: peeku16 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad)) {
                kprintf("bosl: peeku16 bad addr\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: peeku16 null addr\n");
                goto fail_runtime;
            }
            volatile uint16_t* p = (volatile uint16_t*)pa;
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)(uint32_t)*p;
            sp++;
            break;
        }
        case OP_POKEU16: {
            if (sp < 2) {
                kprintf("bosl: pokeu16 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad) || !bosl_is_intish(&val)) {
                kprintf("bosl: pokeu16 bad args\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: pokeu16 null addr\n");
                goto fail_runtime;
            }
            volatile uint16_t* p = (volatile uint16_t*)pa;
            *p = (uint16_t)(bosl_to_i64(&val) & 0xFFFF);
            break;
        }
        case OP_PEEKU64: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: peeku64 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad)) {
                kprintf("bosl: peeku64 bad addr\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: peeku64 null addr\n");
                goto fail_runtime;
            }
            volatile uint64_t* p = (volatile uint64_t*)pa;
            stack[sp].type = BOSL_VAL_INT64;
            stack[sp].as.i64 = (int64_t)(*p);
            sp++;
            break;
        }
        case OP_POKEU64: {
            if (sp < 2) {
                kprintf("bosl: pokeu64 underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t ad = stack[--sp];
            if (!bosl_is_intish(&ad) || !bosl_is_intish(&val)) {
                kprintf("bosl: pokeu64 bad args\n");
                goto fail_runtime;
            }
            void* pa = bosl_ptr_from_val(&ad);
            if (!pa) {
                kprintf("bosl: pokeu64 null addr\n");
                goto fail_runtime;
            }
            volatile uint64_t* p = (volatile uint64_t*)pa;
            *p = (uint64_t)bosl_to_i64(&val);
            break;
        }
        case OP_INB: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: inb underflow\n");
                goto fail_runtime;
            }
            bosl_val_t pt = stack[--sp];
            if (!bosl_is_intish(&pt)) {
                kprintf("bosl: inb expects int port\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)(uint32_t)inb((uint16_t)(bosl_to_i64(&pt) & 0xFFFF));
            sp++;
            break;
        }
        case OP_OUTB: {
            if (sp < 2) {
                kprintf("bosl: outb underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t pt = stack[--sp];
            if (!bosl_is_intish(&pt) || !bosl_is_intish(&val)) {
                kprintf("bosl: outb expects int port, int val\n");
                goto fail_runtime;
            }
            outb((uint16_t)(bosl_to_i64(&pt) & 0xFFFF), (uint8_t)(bosl_to_i64(&val) & 0xFF));
            break;
        }
        case OP_INW: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: inw underflow\n");
                goto fail_runtime;
            }
            bosl_val_t pt = stack[--sp];
            if (!bosl_is_intish(&pt)) {
                kprintf("bosl: inw expects int port\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)(uint32_t)inw((uint16_t)(bosl_to_i64(&pt) & 0xFFFF));
            sp++;
            break;
        }
        case OP_OUTW: {
            if (sp < 2) {
                kprintf("bosl: outw underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t pt = stack[--sp];
            if (!bosl_is_intish(&pt) || !bosl_is_intish(&val)) {
                kprintf("bosl: outw expects int port, int val\n");
                goto fail_runtime;
            }
            outw((uint16_t)(bosl_to_i64(&pt) & 0xFFFF), (uint16_t)(bosl_to_i64(&val) & 0xFFFF));
            break;
        }
        case OP_INL: {
            if (sp == 0 || sp >= STACK_MAX) {
                kprintf("bosl: inl underflow\n");
                goto fail_runtime;
            }
            bosl_val_t pt = stack[--sp];
            if (!bosl_is_intish(&pt)) {
                kprintf("bosl: inl expects int port\n");
                goto fail_runtime;
            }
            stack[sp].type = BOSL_VAL_INT;
            stack[sp].as.i = (int32_t)inl((uint16_t)(bosl_to_i64(&pt) & 0xFFFF));
            sp++;
            break;
        }
        case OP_OUTL: {
            if (sp < 2) {
                kprintf("bosl: outl underflow\n");
                goto fail_runtime;
            }
            bosl_val_t val = stack[--sp];
            bosl_val_t pt = stack[--sp];
            if (!bosl_is_intish(&pt) || !bosl_is_intish(&val)) {
                kprintf("bosl: outl expects int port, int val\n");
                goto fail_runtime;
            }
            outl((uint16_t)(bosl_to_i64(&pt) & 0xFFFF), (uint32_t)bosl_to_i64(&val));
            break;
        }
        case OP_CLI: {
            __asm__ __volatile__("cli" ::: "memory");
            break;
        }
        case OP_STI: {
            __asm__ __volatile__("sti" ::: "memory");
            break;
        }
        case OP_MEMFENCE: {
            __asm__ __volatile__("" ::: "memory");
            break;
        }
        case OP_MEMCPY: {
            if (sp < 3) {
                kprintf("bosl: memcpy underflow\n");
                goto fail_runtime;
            }
            bosl_val_t nv = stack[--sp];
            bosl_val_t dstv = stack[--sp];
            bosl_val_t srcv = stack[--sp];
            if (!bosl_is_intish(&nv) || !bosl_is_intish(&dstv) || !bosl_is_intish(&srcv)) {
                kprintf("bosl: memcpy bad args\n");
                goto fail_runtime;
            }
            uint64_t n = (uint64_t)bosl_to_i64(&nv);
            if (n > 0x01000000u) {
                kprintf("bosl: memcpy too large\n");
                goto fail_runtime;
            }
            void* pd = bosl_ptr_from_val(&dstv);
            void* ps = bosl_ptr_from_val(&srcv);
            if (!pd || !ps) {
                kprintf("bosl: memcpy null ptr\n");
                goto fail_runtime;
            }
            uint8_t* d = (uint8_t*)pd;
            const uint8_t* s = (const uint8_t*)ps;
            if (n == 0) {
                break;
            }
            uintptr_t du = (uintptr_t)d;
            uintptr_t su = (uintptr_t)s;
            if (du != su) {
                if (du < su || du >= su + n) {
                    for (uint64_t i = 0; i < n; i++) {
                        d[i] = s[i];
                    }
                } else {
                    for (uint64_t i = n; i > 0; i--) {
                        d[i - 1] = s[i - 1];
                    }
                }
            }
            break;
        }
        case OP_MEMSET: {
            if (sp < 3) {
                kprintf("bosl: memset underflow\n");
                goto fail_runtime;
            }
            bosl_val_t nv = stack[--sp];
            bosl_val_t cv = stack[--sp];
            bosl_val_t dstv = stack[--sp];
            if (!bosl_is_intish(&nv) || !bosl_is_intish(&cv) || !bosl_is_intish(&dstv)) {
                kprintf("bosl: memset bad args\n");
                goto fail_runtime;
            }
            uint64_t n = (uint64_t)bosl_to_i64(&nv);
            if (n > 0x01000000u) {
                kprintf("bosl: memset too large\n");
                goto fail_runtime;
            }
            void* pd = bosl_ptr_from_val(&dstv);
            if (!pd) {
                kprintf("bosl: memset null ptr\n");
                goto fail_runtime;
            }
            uint8_t c = (uint8_t)(bosl_to_i64(&cv) & 0xFF);
            uint8_t* d = (uint8_t*)pd;
            for (uint64_t i = 0; i < n; i++) {
                d[i] = c;
            }
            break;
        }
        case OP_CPU_PAUSE: {
            __asm__ __volatile__("pause" ::: "memory");
            break;
        }
        case OP_MFENCE_CPU: {
            __asm__ __volatile__("mfence" ::: "memory");
            break;
        }
        case OP_LFENCE_CPU: {
            __asm__ __volatile__("lfence" ::: "memory");
            break;
        }
        case OP_SFENCE_CPU: {
            __asm__ __volatile__("sfence" ::: "memory");
            break;
        }
        case OP_HALT:
            kfree(stack);
            kfree(consts);
            return 0;
        default:
            kprintf("bosl: unknown opcode 0x%x\n", (uint32_t)op);
            goto fail_runtime;
        }
    }

fail_runtime:
    kfree(stack);
    kfree(consts);
    return 13;
}

int bosl_vm_run_io(const uint8_t* image, uint32_t size, bosl_putc_fn putc, void* user)
{
    return bosl_vm_run_impl(image, size, putc, user);
}

int bosl_vm_run(const uint8_t* image, uint32_t size)
{
    return bosl_vm_run_impl(image, size, 0, 0);
}

