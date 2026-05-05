/*
 * lafillo_demo.c - Lafillo VM demo (inline HTML->text)
 */
#include <stdint.h>
#include "lafillo_vm.h"

typedef struct {
    char* out;
    uint32_t cap;
    uint32_t len;
} lafillo_buf_t;

static void buf_putc(char c, void* user)
{
    lafillo_buf_t* b = (lafillo_buf_t*)user;
    if (!b || !b->out || b->cap == 0) return;
    if (b->len + 1 >= b->cap) return;
    b->out[b->len++] = c;
    b->out[b->len] = '\0';
}

int lafillo_demo_html(char* out, uint32_t out_cap)
{
    lafillo_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    return LAFILLO_VM_ASM_IO(
        "push \"<html><body><h1>Hi</h1><p>Lafillo VM demo.</p></body></html>\"\n"
        "lafillo\n"
        "print\n"
        "halt\n",
        buf_putc, &b);
}
