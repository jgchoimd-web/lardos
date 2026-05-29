#pragma once

#include <stdint.h>

#define LAR_MAGIC 0x3152414Cu  /* "LAR1" LE */
#define LAR_METHOD_STORE 0
#define LAR_METHOD_PASS_STORE 1

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
int lar_extract_password(const uint8_t* data, uint32_t len, const char* name,
                         const char* password, uint8_t* out, uint32_t* out_len);
int lar_create_single(uint8_t* out, uint32_t* out_len, const char* member,
                      const uint8_t* payload, uint32_t payload_len,
                      const char* password);
int lar_selftest(void);
