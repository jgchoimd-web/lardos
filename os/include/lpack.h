#pragma once

#include <stdint.h>

#define LPACK_NAME_MAX 31u

typedef struct {
    char name[LPACK_NAME_MAX + 1u];
    uint32_t size;
} lpack_file_info_t;

int lpack_file_count(const uint8_t* data, uint32_t len);
int lpack_file_at(const uint8_t* data, uint32_t len, uint32_t index, lpack_file_info_t* out);
int lpack_install(const uint8_t* data, uint32_t len);
int lpack_selftest(void);
