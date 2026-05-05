#pragma once

#include <stdint.h>

/* Decode one UTF-8 code point from *pp; advances *pp. Returns 0 at end of string. */
uint32_t utf8_next(const char** pp);

/* Map Unicode to VGA text mode (CP437). Unknown characters become '?'. */
uint8_t unicode_to_cp437(uint32_t cp);

/* Emit UTF-8 string to VGA via put_byte(cp437_byte, user). */
void unicode_utf8_to_cp437(
    void (*put_byte)(uint8_t b, void* user), void* user, const char* utf8);
