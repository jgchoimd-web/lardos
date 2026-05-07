#include <stdint.h>
#include "lil.h"
#include "lil_demo.h"

typedef struct {
    char* out;
    uint32_t cap;
    uint32_t len;
} lil_buf_t;

static void buf_putc(char c, void* user)
{
    lil_buf_t* b = (lil_buf_t*)user;
    if (!b || !b->out || b->cap == 0) {
        return;
    }
    if (b->len + 1 >= b->cap) {
        return;
    }
    b->out[b->len++] = c;
    b->out[b->len] = '\0';
}

/* S-expression: print (+ 40 2) -> "42\n" */
static const char LIL_HELLO[] = "(print (+ 40 2))";

int lil_demo_hello(char* out, uint32_t out_cap)
{
    lil_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) {
        out[0] = '\0';
    }

    return lil_run(LIL_HELLO, buf_putc, &b);
}

/* Inline LIL example (LIL_ASM): run LIL source at runtime. */
int lil_demo_inline(char* out, uint32_t out_cap)
{
    lil_buf_t b;
    b.out = out;
    b.cap = out_cap;
    b.len = 0;
    if (out && out_cap) out[0] = '\0';

    return LIL_ASM_IO("(begin (print (pow 2 8)) (repeat 3 (printn it) (emit 32)) (emit 10))", buf_putc, &b);
}
