#include <stdint.h>
#include "gasm_vm.h"

typedef struct {
    char* out;
    uint32_t cap;
    uint32_t len;
} gasm_buf_t;

static void buf_putc(char c, void* user)
{
    gasm_buf_t* b = (gasm_buf_t*)user;
    if (!b || !b->out || b->cap == 0) return;
    if (b->len + 1 >= b->cap) return;
    b->out[b->len++] = c;
    b->out[b->len] = '\0';
}

int gasm_demo_hello(char* out, uint32_t out_cap)
{
    gasm_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    return GASM_ASM_IO(
        "load 40\n"
        "add 2\n"
        "print\n"
        "halt\n",
        buf_putc, &b);
}

int gasm_demo_inline(char* out, uint32_t out_cap)
{
    gasm_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    return GASM_ASM_IO(
        "load 0x2a\n"
        "print\n"
        "halt\n",
        buf_putc, &b);
}

/* OOP demo: create object with x,y slots, set x=3 y=4, invoke add method, print result. */
int gasm_demo_oop(char* out, uint32_t out_cap)
{
    gasm_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    return GASM_ASM_IO(
        "; OOP: point with x,y. add method returns x+y\n"
        "new 2\n"
        "load 3\n"
        "set 0 0\n"
        "load 4\n"
        "set 0 1\n"
        "invoke 0 add\n"
        "print\n"
        "halt\n"
        "add:\n"
        "getself 0\n"
        "addself 1\n"
        "ret\n",
        buf_putc, &b);
}
