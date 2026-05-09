#include "panic.h"
#include "crashlog.h"
#include "lardkit.h"
#include "ps2.h"
#include "taskprio.h"

#include <stddef.h>
#include <stdint.h>

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;
static int s_panicroom_runtime_ready;

static void vga_puts_at(uint16_t row, uint16_t col, const char* s, uint8_t color)
{
    size_t i = 0;
    size_t pos = (size_t)row * 80u + col;
    while (s[i] != '\0' && pos < 80u * 25u) {
        VGA[pos++] = (uint16_t)color << 8 | (uint8_t)s[i++];
    }
}

static void vga_put_u32_at(uint16_t row, uint16_t col, uint32_t v, uint8_t color)
{
    char buf[11];
    uint32_t i = 0;
    if (v == 0u) {
        vga_puts_at(row, col, "0", color);
        return;
    }
    while (v != 0u && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0u) {
        char c[2];
        c[0] = buf[--i];
        c[1] = '\0';
        vga_puts_at(row, col++, c, color);
    }
}

static void u64_hex(char out[17], uint64_t v)
{
    static const char* hexd = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) {
        int shift = (15 - i) * 4;
        out[i] = hexd[(v >> shift) & 0xF];
    }
    out[16] = '\0';
}

static void vga_clear(uint8_t color)
{
    for (size_t i = 0; i < 80u * 25u; i++) {
        VGA[i] = (uint16_t)color << 8 | (uint8_t)' ';
    }
}

static void vga_puts_wrapped(uint16_t row, const char* s, uint8_t color)
{
    uint16_t col = 0;
    if (!s) s = "(null)";
    while (*s && row < 22u) {
        char c[2];
        if (*s == '\r') {
            s++;
            continue;
        }
        if (*s == '\n') {
            row++;
            col = 0;
            s++;
            continue;
        }
        c[0] = *s++;
        c[1] = '\0';
        vga_puts_at(row, col, c, color);
        col++;
        if (col >= 79u) {
            row++;
            col = 0;
        }
    }
}

__attribute__((noreturn)) static void panic_halt_forever(void)
{
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

static void panic_legacy_screen(const char* msg, const char* value)
{
    vga_clear(0x4F);
    vga_puts_at(0, 0, "KERNEL PANIC", 0x4F);
    vga_puts_at(2, 0, msg ? msg : "(null)", 0x4F);
    if (value && value[0]) vga_puts_at(3, 0, value, 0x4F);
}

static void panicroom_wait_key(void)
{
    for (;;) {
        ps2_key_t key;
        if (ps2_kbd_poll(&key) == 0) return;
        __asm__ __volatile__("pause");
    }
}

static void panicroom_draw_status(const char* msg, const char* value, int capsule_result,
                                  int rollback_result, int dropped_task)
{
    taskprio_info_t tasks;
    taskprio_info(&tasks);

    vga_clear(0x1F);
    vga_puts_at(0, 0, "LARDOS PANIC ROOM", 0x1F);
    vga_puts_at(1, 0, "Kernel risk was detected. Recovery tools are active before halt.", 0x1F);
    vga_puts_at(3, 0, "panic: ", 0x1F);
    vga_puts_at(3, 7, msg ? msg : "(null)", 0x1F);
    if (value && value[0]) {
        vga_puts_at(4, 0, "value: ", 0x1F);
        vga_puts_at(4, 7, value, 0x1F);
    }
    vga_puts_at(6, 0, "panicroom entries: ", 0x1F);
    vga_put_u32_at(6, 19, lardkit_panicroom_entries(), 0x1F);
    vga_puts_at(7, 0, "paniccapsule.lardd: ", 0x1F);
    vga_puts_at(7, 20, capsule_result == 0 ? "written" : "failed", 0x1F);
    vga_puts_at(8, 0, "queued tasks: ", 0x1F);
    vga_put_u32_at(8, 14, tasks.queued, 0x1F);
    vga_puts_at(9, 0, "last rollback: ", 0x1F);
    if (rollback_result == 0) vga_puts_at(9, 15, "applied", 0x1F);
    else if (rollback_result < 0) vga_puts_at(9, 15, "unavailable", 0x1F);
    else vga_puts_at(9, 15, "not requested", 0x1F);
    vga_puts_at(10, 0, "last task drop: ", 0x1F);
    if (dropped_task > 0) vga_puts_at(10, 16, "dropped first queued task", 0x1F);
    else if (dropped_task < 0) vga_puts_at(10, 16, "no queued task", 0x1F);
    else vga_puts_at(10, 16, "not requested", 0x1F);

    vga_puts_at(13, 0, "Keys:", 0x1F);
    vga_puts_at(14, 2, "L show crashlog.txt", 0x1F);
    vga_puts_at(15, 2, "C rebuild paniccapsule.lardd", 0x1F);
    vga_puts_at(16, 2, "R apply last rollback snapshot", 0x1F);
    vga_puts_at(17, 2, "D drop first queued task", 0x1F);
    vga_puts_at(18, 2, "H halt safely", 0x1F);
    vga_puts_at(22, 0, "PanicRoom is intentionally tiny: it avoids the normal shell and keeps control visible.", 0x1F);
}

static void panicroom_show_crashlog(void)
{
    vga_clear(0x0F);
    vga_puts_at(0, 0, "crashlog.txt", 0x0F);
    vga_puts_wrapped(2, crashlog_text(), 0x0F);
    vga_puts_at(23, 0, "Press any key to return to PanicRoom.", 0x0F);
    panicroom_wait_key();
}

static void panicroom_drop_first_task(int* dropped_task)
{
    taskprio_task_t task;
    if (taskprio_at(0, &task) == 0 && taskprio_remove(task.id) == 0) {
        *dropped_task = 1;
    } else {
        *dropped_task = -1;
    }
}

__attribute__((noreturn)) static void panicroom_loop(const char* msg, const char* value, int capsule_result)
{
    int rollback_result = 1;
    int dropped_task = 0;
    panicroom_draw_status(msg, value, capsule_result, rollback_result, dropped_task);
    for (;;) {
        ps2_key_t key;
        if (ps2_kbd_poll(&key) != 0) {
            __asm__ __volatile__("pause");
            continue;
        }
        if (key.kind != PS2K_ASCII) continue;
        if (key.ch == 'l' || key.ch == 'L') {
            panicroom_show_crashlog();
        } else if (key.ch == 'c' || key.ch == 'C') {
            capsule_result = lardkit_panic_capsule_write();
        } else if (key.ch == 'r' || key.ch == 'R') {
            rollback_result = lardkit_rollback_apply();
        } else if (key.ch == 'd' || key.ch == 'D') {
            panicroom_drop_first_task(&dropped_task);
        } else if (key.ch == 'h' || key.ch == 'H' || key.ch == '\n') {
            vga_clear(0x4F);
            vga_puts_at(0, 0, "KERNEL PANIC", 0x4F);
            vga_puts_at(2, 0, msg ? msg : "(null)", 0x4F);
            vga_puts_at(4, 0, "PanicRoom closed. Halting.", 0x4F);
            panic_halt_forever();
        }
        panicroom_draw_status(msg, value, capsule_result, rollback_result, dropped_task);
    }
}

void panic_runtime_ready(void)
{
    s_panicroom_runtime_ready = 1;
}

__attribute__((noreturn)) void panic(const char* msg)
{
    int capsule_result;
    crashlog_record_panic(msg);
    if (!s_panicroom_runtime_ready) {
        panic_legacy_screen(msg, NULL);
        panic_halt_forever();
    }
    lardkit_panicroom_enter();
    capsule_result = lardkit_panic_capsule_write();
    panicroom_loop(msg, NULL, capsule_result);
    panic_halt_forever();
}

__attribute__((noreturn)) void panic_u64(const char* msg, uint64_t v)
{
    char hex[17];
    int capsule_result;
    u64_hex(hex, v);
    crashlog_record_panic_u64(msg, v);
    if (!s_panicroom_runtime_ready) {
        panic_legacy_screen(msg, hex);
        panic_halt_forever();
    }
    lardkit_panicroom_enter();
    capsule_result = lardkit_panic_capsule_write();
    panicroom_loop(msg, hex, capsule_result);
    panic_halt_forever();
}
