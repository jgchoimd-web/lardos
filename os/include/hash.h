#pragma once

#include <stdint.h>

/* CRC32 (IEEE polynomial). Final XOR 0xFFFFFFFF. */
uint32_t hash_crc32(const uint8_t* data, uint32_t len);
uint32_t hash_crc32_continue(uint32_t crc, const uint8_t* data, uint32_t len);

/* FNV-1a 32-bit hash. */
uint32_t hash_fnv1a(const uint8_t* data, uint32_t len);
uint32_t hash_fnv1a_str(const char* s);
