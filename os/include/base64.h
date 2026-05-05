#pragma once

#include <stddef.h>
#include <stdint.h>

/* Base64 encode. out must have at least ((len+2)/3)*4+1 bytes. Returns output length. */
uint32_t base64_encode(const uint8_t* in, uint32_t len, char* out);
/* Base64 decode. out must have at least len bytes. Returns output length or (uint32_t)-1 on error. */
uint32_t base64_decode(const char* in, uint32_t len, uint8_t* out);
