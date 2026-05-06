#pragma once

#include <stdint.h>

#define LAR_MAGIC 0x3152414Cu  /* "LAR1" LE */
#define LAR_METHOD_STORE 0

typedef struct {
    const char* name;
    uint8_t name_len;
    uint8_t method;
    uint32_t packed_size;
    uint32_t unpacked_size;
} lar_entry_t;

typedef void (*lar_list_cb)(const lar_entry_t* entry, void* user);

int lar_list(const uint8_t* data, uint32_t len, lar_list_cb cb, void* user);
int lar_extract(const uint8_t* data, uint32_t len, const char* name, uint8_t* out, uint32_t* out_len);
