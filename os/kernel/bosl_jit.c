#include "bosl_vm.h"

#include "console.h"
#include "mem.h"
#include "vmmon.h"

#include <stdint.h>

enum {
    OP_PUSHI = 0x01, /* i32 */
    OP_PUSHC = 0x02, /* u32 const index (string) */
    OP_PUSHK = 0x03, /* u32 const index (int) */

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

    OP_PRINT = 0x20,
    OP_DUP   = 0x21,
    OP_DROP  = 0x22,
    OP_SWAP  = 0x23,
    OP_OVER  = 0x24,

    OP_JMP   = 0x30, /* u32 absolute pc */
    OP_JZ    = 0x31, /* u32 absolute pc; pop int, jump if 0 */
    OP_JNZ   = 0x32, /* u32 absolute pc; pop int, jump if non-zero */

    OP_EQ    = 0x40,
    OP_NE    = 0x41,
    OP_LT    = 0x42,
    OP_LE    = 0x43,
    OP_GT    = 0x44,
    OP_GE    = 0x45,

    OP_HALT  = 0xFF,
};

enum { BOSL_JIT_CONST_STR = 1, BOSL_JIT_CONST_INT = 2, BOSL_JIT_CONST_INT64 = 3 };

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

typedef struct {
    uint32_t sp;
    int32_t stack[256];
    bosl_putc_fn putc;
    void* user;
} bosl_jit_ctx_t;

static void jit_putc(bosl_jit_ctx_t* ctx, char c)
{
    if (ctx && ctx->putc) ctx->putc(c, ctx->user);
    else console_putc(c);
}

static void jit_write_i32(bosl_jit_ctx_t* ctx, int32_t v)
{
    char buf[16];
    uint32_t i = 0;
    uint32_t neg = 0;
    if (v == 0) {
        jit_putc(ctx, '0');
        return;
    }
    if (v < 0) {
        neg = 1;
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
    if (neg) jit_putc(ctx, '-');
    while (i) jit_putc(ctx, buf[--i]);
}

__attribute__((used))
void bosl_jit_print_i32(bosl_jit_ctx_t* ctx, int32_t v)
{
    jit_write_i32(ctx, v);
    jit_putc(ctx, '\n');
}

__attribute__((used))
int32_t bosl_jit_shl(int32_t a, int32_t b)
{
    return a << (b & 31);
}

__attribute__((used))
int32_t bosl_jit_shr(int32_t a, int32_t b)
{
    return a >> (b & 31);
}

__attribute__((used))
int32_t bosl_jit_ushr(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a >> (b & 31u));
}

/* Executable JIT buffer (RWX via .jit section + MMU mapping). */
__attribute__((section(".jit"), aligned(4096)))
static uint8_t g_jit_buf[16 * 1024];

typedef struct {
    uint8_t* buf;
    uint32_t cap;
    uint32_t len;
    int err;
} emit_t;

static void e8(emit_t* e, uint8_t b)
{
    if (e->err) return;
    if (e->len + 1 > e->cap) { e->err = 1; return; }
    e->buf[e->len++] = b;
}

static void e32(emit_t* e, uint32_t v)
{
    e8(e, (uint8_t)(v & 0xFF));
    e8(e, (uint8_t)((v >> 8) & 0xFF));
    e8(e, (uint8_t)((v >> 16) & 0xFF));
    e8(e, (uint8_t)((v >> 24) & 0xFF));
}

static void e64(emit_t* e, uint64_t v)
{
    e32(e, (uint32_t)(v & 0xFFFFFFFFu));
    e32(e, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static void patch32(uint8_t* at, uint32_t v)
{
    at[0] = (uint8_t)(v & 0xFF);
    at[1] = (uint8_t)((v >> 8) & 0xFF);
    at[2] = (uint8_t)((v >> 16) & 0xFF);
    at[3] = (uint8_t)((v >> 24) & 0xFF);
}

typedef struct {
    uint32_t at;       /* native offset where rel32 starts */
    uint32_t target_pc;/* bytecode pc target */
} fixup_t;

static int emit_prologue(emit_t* e)
{
    /* System V: RDI = ctx.
     * We'll keep ctx in RDI and keep SP in ECX.
     *
     * push rbx
     * mov ecx, [rdi + 0]
     */
    e8(e, 0x53); /* push rbx */
    e8(e, 0x8B); e8(e, 0x0F); /* mov ecx, [rdi] */
    return e->err ? -1 : 0;
}

static void emit_epilogue_ok(emit_t* e)
{
    /* mov [rdi], ecx
     * xor eax,eax
     * pop rbx
     * ret
     */
    e8(e, 0x89); e8(e, 0x0F); /* mov [rdi], ecx */
    e8(e, 0x31); e8(e, 0xC0); /* xor eax,eax */
    e8(e, 0x5B); /* pop rbx */
    e8(e, 0xC3); /* ret */
}

static void emit_epilogue_err(emit_t* e, uint32_t code)
{
    /* mov [rdi], ecx
     * mov eax, imm32
     * pop rbx
     * ret
     */
    e8(e, 0x89); e8(e, 0x0F); /* mov [rdi], ecx */
    e8(e, 0xB8); e32(e, code); /* mov eax, imm32 */
    e8(e, 0x5B); /* pop rbx */
    e8(e, 0xC3); /* ret */
}

static void emit_pushi(emit_t* e, int32_t imm)
{
    /* mov eax, imm32
     * mov [rdi + 4 + rcx*4], eax    (stack at offset 4)
     * inc ecx
     */
    e8(e, 0xB8); e32(e, (uint32_t)imm);
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* [rdi+rcx*4+4] */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
}

static void emit_drop(emit_t* e)
{
    /* dec ecx */
    e8(e, 0xFF); e8(e, 0xC9);
}

static void emit_dup(emit_t* e)
{
    /* mov eax, [rdi+4 + (rcx-1)*4]
     * mov [rdi+4 + rcx*4], eax
     * inc ecx
     *
     * dec ecx; mov eax,[...]; inc ecx would be longer; do:
     * lea ebx, [rcx-1]
     */
    e8(e, 0x8D); e8(e, 0x59); e8(e, 0xFF); /* lea ebx, [rcx-1] */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x9F); e32(e, 4); /* mov eax, [rdi+rbx*4+4] */
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], eax */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
}

static void emit_binop_addsubmul(emit_t* e, uint8_t op)
{
    /* Pop b into eax, pop a into edx, compute, push result at a slot.
     *
     * dec ecx
     * mov eax, [rdi+4 + rcx*4]    ; b
     * dec ecx
     * mov edx, [rdi+4 + rcx*4]    ; a
     * <op edx,eax> or imul edx,eax
     * mov [rdi+4 + rcx*4], edx
     * inc ecx
     */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax, [rdi+rcx*4+4] */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x94); e8(e, 0x8F); e32(e, 4); /* mov edx, [rdi+rcx*4+4] */

    if (op == OP_ADD) { e8(e, 0x01); e8(e, 0xC2); }         /* add edx,eax */
    else if (op == OP_SUB) { e8(e, 0x29); e8(e, 0xC2); }    /* sub edx,eax */
    else { e8(e, 0x0F); e8(e, 0xAF); e8(e, 0xD0); }         /* imul edx,eax */

    e8(e, 0x89); e8(e, 0x94); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], edx */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
}

/* want_remainder: 0 -> quotient in eax, 1 -> remainder in edx */
static uint32_t emit_divmod(emit_t* e, int want_remainder)
{
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x9C); e8(e, 0x8F); e32(e, 4); /* mov ebx, [rdi+rcx*4+4] */
    e8(e, 0x85); e8(e, 0xDB); /* test ebx,ebx */
    e8(e, 0x0F); e8(e, 0x84); /* jz rel32 */
    uint32_t at = e->len;
    e32(e, 0);

    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax, [rdi+rcx*4+4] */
    e8(e, 0x99); /* cdq */
    e8(e, 0xF7); e8(e, 0xFB); /* idiv ebx */
    if (want_remainder) {
        e8(e, 0x89); e8(e, 0x94); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], edx */
    } else {
        e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], eax */
    }
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */

    return at;
}

static void emit_binop_bitwise(emit_t* e, uint8_t which)
{
    e8(e, 0xFF); e8(e, 0xC9);
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4);
    e8(e, 0xFF); e8(e, 0xC9);
    e8(e, 0x8B); e8(e, 0x94); e8(e, 0x8F); e32(e, 4);
    if (which == 0) {
        e8(e, 0x21); e8(e, 0xC2);
    } else if (which == 1) {
        e8(e, 0x09); e8(e, 0xC2);
    } else {
        e8(e, 0x31); e8(e, 0xC2);
    }
    e8(e, 0x89); e8(e, 0x94); e8(e, 0x8F); e32(e, 4);
    e8(e, 0xFF); e8(e, 0xC1);
}

static void emit_not_bit(emit_t* e)
{
    e8(e, 0xFF); e8(e, 0xC9);
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4);
    e8(e, 0xF7); e8(e, 0xD0); /* not eax */
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4);
    e8(e, 0xFF); e8(e, 0xC1);
}

static void emit_shift_call(emit_t* e, int32_t (*fn)(int32_t, int32_t))
{
    /* Save ctx in r11; mov edi would zero-extend and destroy rdi. */
    e8(e, 0x49); e8(e, 0x89); e8(e, 0xFB); /* mov r11, rdi */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x49); e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8B); e32(e, 4); /* mov eax,[r11+rcx*4+4] b */
    e8(e, 0x89); e8(e, 0xC6); /* mov esi, eax */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x49); e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8B); e32(e, 4); /* mov eax,[r11+rcx*4+4] a */
    e8(e, 0x89); e8(e, 0xC7); /* mov edi, eax */
    e8(e, 0x48); e8(e, 0xB8); e64(e, (uint64_t)(uintptr_t)fn);
    e8(e, 0xFF); e8(e, 0xD0);
    e8(e, 0x41); e8(e, 0x89); e8(e, 0x84); e8(e, 0x8B); e32(e, 4); /* mov [r11+rcx*4+4],eax */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
    e8(e, 0x4C); e8(e, 0x89); e8(e, 0xDF); /* mov rdi, r11 */
}

static void emit_print(emit_t* e)
{
    /* dec ecx
     * mov eax, [rdi+4 + rcx*4]
     * mov esi, eax
     * mov rax, imm64 (helper)
     * call rax
     */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax, [rdi+rcx*4+4] */
    e8(e, 0x89); e8(e, 0xC6); /* mov esi,eax */
    e8(e, 0x48); e8(e, 0xB8); e64(e, (uint64_t)(uintptr_t)&bosl_jit_print_i32); /* mov rax, imm64 */
    e8(e, 0xFF); e8(e, 0xD0); /* call rax */
}

static void emit_jmp_rel32(emit_t* e, uint32_t* at_out)
{
    e8(e, 0xE9); /* jmp rel32 */
    *at_out = e->len;
    e32(e, 0);
}

static void emit_jz_rel32_pop(emit_t* e, uint32_t* at_out)
{
    /* dec ecx; mov eax,[...]; test eax,eax; jz rel32 */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax,[rdi+rcx*4+4] */
    e8(e, 0x85); e8(e, 0xC0); /* test eax,eax */
    e8(e, 0x0F); e8(e, 0x84); /* jz rel32 */
    *at_out = e->len;
    e32(e, 0);
}

static void emit_jnz_rel32_pop(emit_t* e, uint32_t* at_out)
{
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax,[rdi+rcx*4+4] */
    e8(e, 0x85); e8(e, 0xC0); /* test eax,eax */
    e8(e, 0x0F); e8(e, 0x85); /* jnz rel32 */
    *at_out = e->len;
    e32(e, 0);
}

static void emit_neg(emit_t* e)
{
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax,[rdi+rcx*4+4] */
    e8(e, 0xF7); e8(e, 0xD8); /* neg eax */
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], eax */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
}

static void emit_swap(emit_t* e)
{
    e8(e, 0x8D); e8(e, 0x59); e8(e, 0xFF); /* lea ebx,[rcx-1] */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x9F); e32(e, 4); /* mov eax,[rdi+rbx*4+4] */
    e8(e, 0x8D); e8(e, 0x59); e8(e, 0xFE); /* lea ebx,[rcx-2] */
    e8(e, 0x8B); e8(e, 0x94); e8(e, 0x9F); e32(e, 4); /* mov edx,[rdi+rbx*4+4] */
    e8(e, 0x8D); e8(e, 0x59); e8(e, 0xFF); /* lea ebx,[rcx-1] */
    e8(e, 0x89); e8(e, 0x94); e8(e, 0x9F); e32(e, 4); /* mov [rdi+rbx*4+4], edx */
    e8(e, 0x8D); e8(e, 0x59); e8(e, 0xFE); /* lea ebx,[rcx-2] */
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x9F); e32(e, 4); /* mov [rdi+rbx*4+4], eax */
}

static void emit_over(emit_t* e)
{
    e8(e, 0x8D); e8(e, 0x59); e8(e, 0xFE); /* lea ebx,[rcx-2] */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x9F); e32(e, 4); /* mov eax,[rdi+rbx*4+4] */
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], eax */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
}

/* cmp edx (a), eax (b); setcc al; movzx eax,al */
static void emit_cmp_setcc(emit_t* e, uint8_t setcc_byte)
{
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov eax,[rdi+rcx*4+4] ; b */
    e8(e, 0xFF); e8(e, 0xC9); /* dec ecx */
    e8(e, 0x8B); e8(e, 0x94); e8(e, 0x8F); e32(e, 4); /* mov edx,[rdi+rcx*4+4] ; a */
    e8(e, 0x39); e8(e, 0xC2); /* cmp edx,eax */
    e8(e, 0x0F); e8(e, setcc_byte); e8(e, 0xC0); /* setcc al */
    e8(e, 0x0F); e8(e, 0xB6); e8(e, 0xC0); /* movzx eax,al */
    e8(e, 0x89); e8(e, 0x84); e8(e, 0x8F); e32(e, 4); /* mov [rdi+rcx*4+4], eax */
    e8(e, 0xFF); e8(e, 0xC1); /* inc ecx */
}

typedef int (*jit_fn_t)(bosl_jit_ctx_t* ctx);

static int uses_only_int_ops(const uint8_t* code, uint32_t code_size)
{
    uint32_t pc = 0;
    while (pc < code_size) {
        uint8_t op = code[pc++];
        switch (op) {
        case OP_PUSHI: case OP_PUSHK: pc += 4; break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        case OP_NEG:
        case OP_AND: case OP_OR: case OP_XOR: case OP_NOT:
        case OP_SHL: case OP_SHR: case OP_USHR:
        case OP_DUP: case OP_DROP: case OP_SWAP: case OP_OVER: case OP_PRINT:
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        case OP_HALT:
            break;
        case OP_JMP: case OP_JZ: case OP_JNZ:
            return 0; /* Branchy BOSL uses the interpreter step budget. */
        default:
            return 0;
        }
        if (pc > code_size) return 0;
    }
    return 1;
}

int bosl_vm_run_jit_io(const uint8_t* image, uint32_t size, bosl_putc_fn putc, void* user)
{
    if (!image || size < 0x14) return 1;
    if (!(image[0] == 'B' && image[1] == 'O' && image[2] == 'S' && image[3] == 'L')) return bosl_vm_run_io(image, size, putc, user);
    if (rd_u16(&image[4]) != 1) return bosl_vm_run_io(image, size, putc, user);

    uint32_t const_count = rd_u32(&image[8]);
    uint32_t code_size = rd_u32(&image[12]);
    uint32_t entry = rd_u32(&image[16]);

    uint32_t off = 0x14;
    int32_t* const_ints = 0;
    uint8_t* const_is_int = 0;
    if (const_count > 0) {
        const_ints = (int32_t*)kmalloc(const_count * (uint32_t)sizeof(int32_t));
        const_is_int = (uint8_t*)kmalloc(const_count * (uint32_t)sizeof(uint8_t));
        if (!const_ints || !const_is_int) {
            kfree(const_ints);
            kfree(const_is_int);
            return bosl_vm_run_io(image, size, putc, user);
        }
        for (uint32_t i = 0; i < const_count; i++) {
            const_is_int[i] = 0;
        }
    }
    for (uint32_t i = 0; i < const_count; i++) {
        if (off + 8 > size) {
            kfree(const_ints);
            kfree(const_is_int);
            return bosl_vm_run_io(image, size, putc, user);
        }
        uint32_t ctype = rd_u32(&image[off]);
        off += 4;
        if (ctype == BOSL_JIT_CONST_STR) {
            uint32_t clen = rd_u32(&image[off]);
            off += 4;
            if (off + clen > size) {
                kfree(const_ints);
                kfree(const_is_int);
                return bosl_vm_run_io(image, size, putc, user);
            }
            off += clen;
        } else if (ctype == BOSL_JIT_CONST_INT) {
            const_ints[i] = rd_i32(&image[off]);
            off += 4;
            const_is_int[i] = 1;
        } else if (ctype == BOSL_JIT_CONST_INT64) {
            if (off + 8 > size) {
                kfree(const_ints);
                kfree(const_is_int);
                return bosl_vm_run_io(image, size, putc, user);
            }
            off += 8;
        } else {
            kfree(const_ints);
            kfree(const_is_int);
            return bosl_vm_run_io(image, size, putc, user);
        }
    }
    if (off + code_size > size) {
        kfree(const_ints);
        kfree(const_is_int);
        return bosl_vm_run_io(image, size, putc, user);
    }
    const uint8_t* code = &image[off];
    if (entry >= code_size) {
        kfree(const_ints);
        kfree(const_is_int);
        return bosl_vm_run_io(image, size, putc, user);
    }

    if (!uses_only_int_ops(code, code_size)) {
        kfree(const_ints);
        kfree(const_is_int);
        return bosl_vm_run_io(image, size, putc, user);
    }

    /* Compile */
    emit_t e;
    e.buf = g_jit_buf;
    e.cap = (uint32_t)sizeof(g_jit_buf);
    e.len = 0;
    e.err = 0;

    /* Map bytecode pc -> native offset */
    uint32_t* pc2off = (uint32_t*)kmalloc(code_size * (uint32_t)sizeof(uint32_t));
    if (!pc2off) {
        kfree(const_ints);
        kfree(const_is_int);
        return 4;
    }
    for (uint32_t i = 0; i < code_size; i++) pc2off[i] = 0xFFFFFFFFu;

    fixup_t* fix = (fixup_t*)kmalloc(code_size * (uint32_t)sizeof(fixup_t));
    if (!fix) {
        kfree(pc2off);
        kfree(const_ints);
        kfree(const_is_int);
        return 4;
    }
    uint32_t fix_n = 0;

    if (emit_prologue(&e) != 0) {
        kfree(fix);
        kfree(pc2off);
        kfree(const_ints);
        kfree(const_is_int);
        return 5;
    }

    uint32_t pc = 0;
#define BOSL_JIT_DIV0_MAX 16u
    uint32_t div0_patch[BOSL_JIT_DIV0_MAX];
    uint32_t div0_n = 0;

    while (pc < code_size && !e.err) {
        pc2off[pc] = e.len;
        uint8_t op = code[pc++];
        switch (op) {
        case OP_PUSHI: {
            int32_t imm = rd_i32(&code[pc]);
            pc += 4;
            emit_pushi(&e, imm);
            break;
        }
        case OP_PUSHK: {
            uint32_t idx = rd_u32(&code[pc]);
            pc += 4;
            if (idx >= const_count || !const_is_int || !const_is_int[idx]) {
                e.err = 1;
                break;
            }
            emit_pushi(&e, const_ints[idx]);
            break;
        }
        case OP_DROP:
            emit_drop(&e);
            break;
        case OP_DUP:
            emit_dup(&e);
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
            emit_binop_addsubmul(&e, op);
            break;
        case OP_DIV: {
            if (div0_n >= BOSL_JIT_DIV0_MAX) {
                e.err = 1;
                break;
            }
            div0_patch[div0_n++] = emit_divmod(&e, 0);
            break;
        }
        case OP_MOD: {
            if (div0_n >= BOSL_JIT_DIV0_MAX) {
                e.err = 1;
                break;
            }
            div0_patch[div0_n++] = emit_divmod(&e, 1);
            break;
        }
        case OP_NEG:
            emit_neg(&e);
            break;
        case OP_AND:
            emit_binop_bitwise(&e, 0);
            break;
        case OP_OR:
            emit_binop_bitwise(&e, 1);
            break;
        case OP_XOR:
            emit_binop_bitwise(&e, 2);
            break;
        case OP_NOT:
            emit_not_bit(&e);
            break;
        case OP_SHL:
            emit_shift_call(&e, bosl_jit_shl);
            break;
        case OP_SHR:
            emit_shift_call(&e, bosl_jit_shr);
            break;
        case OP_USHR:
            emit_shift_call(&e, bosl_jit_ushr);
            break;
        case OP_SWAP:
            emit_swap(&e);
            break;
        case OP_OVER:
            emit_over(&e);
            break;
        case OP_EQ:
            emit_cmp_setcc(&e, 0x94);
            break;
        case OP_NE:
            emit_cmp_setcc(&e, 0x95);
            break;
        case OP_LT:
            emit_cmp_setcc(&e, 0x9C);
            break;
        case OP_LE:
            emit_cmp_setcc(&e, 0x9E);
            break;
        case OP_GT:
            emit_cmp_setcc(&e, 0x9F);
            break;
        case OP_GE:
            emit_cmp_setcc(&e, 0x9D);
            break;
        case OP_PRINT:
            emit_print(&e);
            break;
        case OP_JMP: {
            uint32_t dst = rd_u32(&code[pc]); pc += 4;
            uint32_t at;
            emit_jmp_rel32(&e, &at);
            fix[fix_n++] = (fixup_t){ .at = at, .target_pc = dst };
            break;
        }
        case OP_JZ: {
            uint32_t dst = rd_u32(&code[pc]); pc += 4;
            uint32_t at;
            emit_jz_rel32_pop(&e, &at);
            fix[fix_n++] = (fixup_t){ .at = at, .target_pc = dst };
            break;
        }
        case OP_JNZ: {
            uint32_t dst = rd_u32(&code[pc]); pc += 4;
            uint32_t at;
            emit_jnz_rel32_pop(&e, &at);
            fix[fix_n++] = (fixup_t){ .at = at, .target_pc = dst };
            break;
        }
        case OP_HALT:
            emit_epilogue_ok(&e);
            pc = code_size; /* stop */
            break;
        default:
            e.err = 1;
            break;
        }
    }

    if (e.err) {
        kfree(fix);
        kfree(pc2off);
        kfree(const_ints);
        kfree(const_is_int);
        return bosl_vm_run_io(image, size, putc, user);
    }

    /* Patch div/mod divide-by-zero jumps to a shared error epilogue. */
    if (div0_n > 0) {
        uint32_t err_at = e.len;
        emit_epilogue_err(&e, 14);
        for (uint32_t i = 0; i < div0_n; i++) {
            int32_t rel = (int32_t)err_at - (int32_t)(div0_patch[i] + 4);
            patch32(&e.buf[div0_patch[i]], (uint32_t)rel);
        }
    }

    /* Patch jmp/jz */
    for (uint32_t i = 0; i < fix_n; i++) {
        uint32_t tpc = fix[i].target_pc;
        if (tpc >= code_size || pc2off[tpc] == 0xFFFFFFFFu) {
            kfree(fix);
            kfree(pc2off);
            kfree(const_ints);
            kfree(const_is_int);
            return bosl_vm_run_io(image, size, putc, user);
        }
        uint32_t src = fix[i].at;
        uint32_t dst = pc2off[tpc];
        int32_t rel = (int32_t)dst - (int32_t)(src + 4);
        patch32(&e.buf[src], (uint32_t)rel);
    }

    kfree(fix);
    kfree(pc2off);

    jit_fn_t fn = (jit_fn_t)(void*)g_jit_buf;
    bosl_jit_ctx_t ctx;
    ctx.sp = 0;
    ctx.putc = putc;
    ctx.user = user;

    int rc = fn(&ctx);
    kfree(const_ints);
    kfree(const_is_int);
    vmmon_record(VMMON_BOSL, code_size, rc);
    return rc;
}

int bosl_vm_run_jit(const uint8_t* image, uint32_t size)
{
    return bosl_vm_run_jit_io(image, size, 0, 0);
}
