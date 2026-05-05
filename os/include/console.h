 #pragma once

 #include <stdint.h>

 /* VGA text mode dimensions */
 #define CONSOLE_WIDTH  80
 #define CONSOLE_HEIGHT 25

 void console_init(void);
 void console_clear(void);
 void console_set_color(uint8_t fg, uint8_t bg);
 void console_putc(char c);
 void console_write(const char* str);

 /* UTF-8 text; non-CP437 code points render as '?'. */
 void console_put_utf8(const char* str);

 int kprintf(const char* fmt, ...);

