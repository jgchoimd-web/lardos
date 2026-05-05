#include <stdint.h>
#include "bosl_vm.h"

typedef struct {
    char* out;
    uint32_t cap;
    uint32_t len;
} bosl_buf_t;

static void buf_putc(char c, void* user)
{
    bosl_buf_t* b = (bosl_buf_t*)user;
    if (!b || !b->out || b->cap == 0) return;
    if (b->len + 1 >= b->cap) return;
    b->out[b->len++] = c;
    b->out[b->len] = '\0';
}

/* A tiny built-in BOSL program (int-only so it can be JIT compiled):
 *   pushi 10
 *   pushi 3
 *   mod
 *   print
 *   halt
 */
static const uint8_t BOSL_HELLO_IMAGE[] = {
    /* header */
    'B','O','S','L',
    0x01,0x00, /* version=1 */
    0x00,0x00, /* flags=0 */
    0x00,0x00,0x00,0x00, /* const_count=0 */
    0x0D,0x00,0x00,0x00, /* code_size=13 */
    0x00,0x00,0x00,0x00, /* entry=0 */
    /* code */
    0x01, /* OP_PUSHI */ 10,0,0,0,
    0x01, /* OP_PUSHI */ 3,0,0,0,
    0x14, /* OP_MOD */
    0x20, /* OP_PRINT */
    0xFF, /* OP_HALT */
};

int bosl_demo_hello(char* out, uint32_t out_cap)
{
    bosl_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    int rc = bosl_vm_run_jit_io(BOSL_HELLO_IMAGE, (uint32_t)sizeof(BOSL_HELLO_IMAGE), buf_putc, &b);
    return rc;
}

/* Inline BOSL example (like C __asm__): assemble and run at runtime. */
int bosl_demo_inline(char* out, uint32_t out_cap)
{
    bosl_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    return BOSL_ASM_IO(
        "pushc \"inline: \"\n"
        "printn\n"
        "pushi 40\n"
        "pushi 2\n"
        "add\n"
        "print\n"
        "halt\n",
        buf_putc, &b);
}

