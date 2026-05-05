#include "panic.h"

#include <stddef.h>
#include <stdint.h>

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;

static void vga_puts_at(uint16_t row, uint16_t col, const char* s, uint8_t color)
{
    size_t i = 0;
    size_t pos = (size_t)row * 80u + col;
    while (s[i] != '\0' && pos < 80u * 25u) {
        VGA[pos++] = (uint16_t)color << 8 | (uint8_t)s[i++];
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

__attribute__((noreturn)) void panic(const char* msg)
{
    vga_clear(0x4F);
    vga_puts_at(0, 0, "KERNEL PANIC", 0x4F);
    vga_puts_at(2, 0, msg ? msg : "(null)", 0x4F);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

__attribute__((noreturn)) void panic_u64(const char* msg, uint64_t v)
{
    char hex[17];
    u64_hex(hex, v);
    vga_clear(0x4F);
    vga_puts_at(0, 0, "KERNEL PANIC", 0x4F);
    vga_puts_at(2, 0, msg ? msg : "(null)", 0x4F);
    vga_puts_at(3, 0, hex, 0x4F);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

