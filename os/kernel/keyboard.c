#include "keyboard.h"
#include "io.h"
#include "irq.h"
#include "console.h"

#define KBD_DATA_PORT 0x60

static char keybuf[128];
static int buf_head = 0;
static int buf_tail = 0;

static const char scancode_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',  0, '\\',
    'z','x','c','v','b','n','m',',','.','/',  0,   0,   0,   0,   0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static void enqueue_key(char c)
{
    int next = (buf_head + 1) & (int)(sizeof(keybuf) - 1);
    if (next != buf_tail) {
        keybuf[buf_head] = c;
        buf_head = next;
    }
}

static char dequeue_key(void)
{
    if (buf_head == buf_tail) {
        return 0;
    }
    char c = keybuf[buf_tail];
    buf_tail = (buf_tail + 1) & (int)(sizeof(keybuf) - 1);
    return c;
}

static void keyboard_handler(struct regs* r)
{
    (void)r;
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc & 0x80) {
        return;
    }
    if (sc < sizeof(scancode_map)) {
        char c = scancode_map[sc];
        if (c) {
            enqueue_key(c);
        }
    }
}

char keyboard_getch(void)
{
    for (;;) {
        char c = dequeue_key();
        if (c) {
            return c;
        }
        __asm__ __volatile__("hlt");
    }
}

void keyboard_init(void)
{
    irq_register_handler(1, keyboard_handler);
}

