 #include "console.h"

 #include <stdarg.h>
 #include <stdint.h>
 #include "io.h"
 #include "unicode.h"

 #define VGA_MEMORY ((uint16_t*)0xB8000)

 static uint8_t console_color = 0x07; /* light gray on black */
 static uint16_t cursor_x = 0;
 static uint16_t cursor_y = 0;

 static inline uint16_t make_vga_entry(char c, uint8_t color)
 {
     return ((uint16_t)color << 8) | (uint8_t)c;
 }

 static void console_scroll(void)
 {
     if (cursor_y < CONSOLE_HEIGHT) {
         return;
     }

     const uint16_t blank = make_vga_entry(' ', console_color);

     for (uint16_t y = 1; y < CONSOLE_HEIGHT; y++) {
         for (uint16_t x = 0; x < CONSOLE_WIDTH; x++) {
             VGA_MEMORY[(y - 1) * CONSOLE_WIDTH + x] =
                 VGA_MEMORY[y * CONSOLE_WIDTH + x];
         }
     }

     cursor_y = CONSOLE_HEIGHT - 1;
     for (uint16_t x = 0; x < CONSOLE_WIDTH; x++) {
         VGA_MEMORY[cursor_y * CONSOLE_WIDTH + x] = blank;
     }
 }

 static void console_update_hw_cursor(void)
 {
     uint16_t pos = cursor_y * CONSOLE_WIDTH + cursor_x;

     /* VGA cursor ports */
     outb(0x3D4, 0x0F);
     outb(0x3D5, (uint8_t)(pos & 0xFF));
     outb(0x3D4, 0x0E);
     outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
 }

 void console_init(void)
 {
     console_set_color(7, 0);
     console_clear();
 }

 void console_clear(void)
 {
     const uint16_t blank = make_vga_entry(' ', console_color);
     for (uint16_t y = 0; y < CONSOLE_HEIGHT; y++) {
         for (uint16_t x = 0; x < CONSOLE_WIDTH; x++) {
             VGA_MEMORY[y * CONSOLE_WIDTH + x] = blank;
         }
     }
     cursor_x = 0;
     cursor_y = 0;
     console_update_hw_cursor();
 }

 void console_set_color(uint8_t fg, uint8_t bg)
 {
     console_color = (uint8_t)((bg << 4) | (fg & 0x0F));
 }

 static void console_put_unicode(uint32_t cp)
 {
     if (cp == 0) {
         return;
     }
     if (cp < 0x80u) {
         console_putc((char)cp);
         return;
     }
     console_putc((char)unicode_to_cp437(cp));
 }

 void console_putc(char c)
 {
     if (c == '\n') {
         cursor_x = 0;
         cursor_y++;
     } else if (c == '\r') {
         cursor_x = 0;
     } else if (c == '\t') {
         cursor_x = (uint16_t)((cursor_x + 8) & ~(uint16_t)7);
     } else if (c == '\b') {
         if (cursor_x > 0) {
             cursor_x--;
         } else if (cursor_y > 0) {
             cursor_y--;
             cursor_x = CONSOLE_WIDTH - 1;
         }
         VGA_MEMORY[cursor_y * CONSOLE_WIDTH + cursor_x] =
             make_vga_entry(' ', console_color);
     } else {
         VGA_MEMORY[cursor_y * CONSOLE_WIDTH + cursor_x] =
             make_vga_entry(c, console_color);
         cursor_x++;
         if (cursor_x >= CONSOLE_WIDTH) {
             cursor_x = 0;
             cursor_y++;
         }
     }

     console_scroll();
     console_update_hw_cursor();
 }

 void console_write(const char* str)
 {
     if (!str) {
         return;
     }
     for (;;) {
         if (*str == '\0') {
             break;
         }
         uint32_t cp = utf8_next(&str);
         if (cp == 0) {
             break;
         }
         console_put_unicode(cp);
     }
 }

 void console_put_utf8(const char* str)
 {
     console_write(str);
 }

 static void console_write_dec(int value)
 {
     char buf[16];
     int i = 0;
     int negative = 0;

     if (value == 0) {
         console_putc('0');
         return;
     }

     if (value < 0) {
         negative = 1;
         value = -value;
     }

     while (value != 0 && i < (int)sizeof(buf)) {
         buf[i++] = (char)('0' + (value % 10));
         value /= 10;
     }

     if (negative) {
         console_putc('-');
     }

     while (i-- > 0) {
         console_putc(buf[i]);
     }
 }

 static void console_write_hex(uint32_t value)
 {
     char buf[9];
     static const char* hex = "0123456789ABCDEF";

     for (int i = 0; i < 8; i++) {
         buf[7 - i] = hex[value & 0xF];
         value >>= 4;
     }
     buf[8] = '\0';

     console_write("0x");
     console_write(buf);
 }

 int kprintf(const char* fmt, ...)
 {
     va_list args;
     va_start(args, fmt);

     int count = 0;

     while (fmt && *fmt) {
         if (*fmt != '%') {
             uint32_t cp = utf8_next(&fmt);
             if (cp == 0) {
                 break;
             }
             console_put_unicode(cp);
             count++;
             continue;
         }

         fmt++;
         char spec = *fmt++;

         switch (spec) {
         case 'c': {
             char c = (char)va_arg(args, int);
             console_putc(c);
             count++;
             break;
         }
         case 's': {
             const char* s = va_arg(args, const char*);
             if (!s) {
                 s = "(null)";
             }
             while (*s) {
                 console_putc(*s++);
                 count++;
             }
             break;
         }
         case 'd':
         case 'i': {
             int v = va_arg(args, int);
             console_write_dec(v);
             /* count is approximate; we don't track exact length */
             break;
         }
         case 'u': {
             unsigned int v = va_arg(args, unsigned int);
             console_write_dec((int)v);
             break;
         }
         case 'x': {
             uint32_t v = va_arg(args, uint32_t);
             console_write_hex(v);
             break;
         }
         case '%':
             console_putc('%');
             count++;
             break;
         default:
             /* Unknown specifier, print it literally */
             console_putc('%');
             console_putc(spec);
             count += 2;
             break;
         }
     }

     va_end(args);
     return count;
 }

