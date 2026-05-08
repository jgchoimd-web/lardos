#pragma once

#include <stdint.h>

#define LPACK_NAME_MAX 31u
#define LPACK_UNDO_MAX_FILES 4u

typedef struct {
    char name[LPACK_NAME_MAX + 1u];
    uint32_t size;
} lpack_file_info_t;

typedef struct {
    uint32_t valid;
    uint32_t files;
    uint32_t installable;
    uint32_t total_bytes;
    uint32_t hash;
    uint32_t warnings;
    uint32_t errors;
} lpack_verify_info_t;

typedef struct {
    uint32_t ready;
    uint32_t files;
    uint32_t bytes;
} lpack_undo_info_t;

int lpack_file_count(const uint8_t* data, uint32_t len);
int lpack_file_at(const uint8_t* data, uint32_t len, uint32_t index, lpack_file_info_t* out);
int lpack_verify(const uint8_t* data, uint32_t len, lpack_verify_info_t* out);
int lpack_install(const uint8_t* data, uint32_t len);
int lpack_undo_last(void);
void lpack_undo_info(lpack_undo_info_t* out);
int lpack_selftest(void);
