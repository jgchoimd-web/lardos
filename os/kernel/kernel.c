#include <stdint.h>
#include "console.h"
#include "fs.h"
#include "mem.h"
#include "timer.h"
#include "keyboard.h"

static void list_file_cb(const char* name, uint32_t size, void* user)
{
    (void)user;
    kprintf("  %s (%u bytes)\n", name, size);
}

#define SHELL_MAX_LINE 128

static int str_eq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void shell_read_line(char* buf, uint32_t max_len)
{
    uint32_t len = 0;

    for (;;) {
        char c = keyboard_getch();

        if (c == '\r') {
            c = '\n';
        }

        if (c == '\n') {
            console_putc('\n');
            break;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                console_putc('\b');
            }
        } else if (c >= ' ' && c <= '~') {
            if (len + 1 < max_len) {
                buf[len++] = c;
                console_putc(c);
            }
        }
    }

    buf[len] = '\0';
}

static void shell_print_help(void)
{
    kprintf("Built-in commands:\n");
    kprintf("  help   - show this help\n");
    kprintf("  ticks  - show timer tick count\n");
    kprintf("  cls    - clear the screen\n");
    kprintf("  ls     - list in-memory files\n");
    kprintf("  mem    - show heap stats\n");
}

static void shell_cmd_ls(void)
{
    kprintf("In-memory files:\n");
    fs_list(list_file_cb, 0);
}

static void shell_cmd_mem(void)
{
    kprintf("heap: start=%x end=%x free=%u\n",
            mem_heap_start(), mem_heap_end(), mem_bytes_free());
}

static void shell_run(void)
{
    char line[SHELL_MAX_LINE];

    kprintf("Welcome to lardos shell!\n");
    kprintf("Type 'help' for a list of commands.\n\n");

    for (;;) {
        kprintf("lardos> ");
        shell_read_line(line, sizeof(line));

        if (line[0] == '\0') {
            continue;
        }

        if (str_eq(line, "help")) {
            shell_print_help();
        } else if (str_eq(line, "ticks")) {
            kprintf("ticks=%u\n", timer_ticks());
        } else if (str_eq(line, "cls")) {
            console_clear();
        } else if (str_eq(line, "ls")) {
            shell_cmd_ls();
        } else if (str_eq(line, "mem")) {
            shell_cmd_mem();
        } else {
            kprintf("Unknown command: %s\n", line);
        }
    }
}

void kmain(void)
{
    console_init();
    console_clear();

    mem_init();
    void* a = kmalloc(64);
    void* b = kmalloc(1024);
    kprintf("heap test: a=%x b=%x free=%u\n", (uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)b, mem_bytes_free());
    kfree(a);
    kfree(b);
    kprintf("heap test: after free free=%u\n", mem_bytes_free());

    fs_init();

    /* Start the programmable interval timer so 'ticks' advances. */
    timer_init(100);

    shell_run();
}

