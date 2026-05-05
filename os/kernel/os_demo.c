/*
 * os_demo.c - OS VM demo
 */
#include <stdint.h>
#include "os_vm.h"
#include "os_demo.h"

typedef struct {
    char* out;
    uint32_t cap;
    uint32_t len;
} buf_t;

static void buf_putc(char c, void* user)
{
    buf_t* b = (buf_t*)user;
    if (!b || !b->out || b->cap == 0) return;
    if (b->len + 1 >= b->cap) return;
    b->out[b->len++] = c;
    b->out[b->len] = '\0';
}

int os_demo_hello(char* out, uint32_t out_cap)
{
    buf_t b = { out, out_cap, 0 };
    if (out && out_cap) out[0] = '\0';

    return OS_VM_ASM_IO(
        "push 40\n"
        "push 2\n"
        "add\n"
        "print\n"
        "halt\n",
        buf_putc, &b);
}
