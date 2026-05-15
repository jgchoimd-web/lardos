#pragma once

#include <stdint.h>

#define RXR_NAME_MAX 31u
#define RXR_UNDO_MAX_FILES 8u

typedef struct {
    char name[RXR_NAME_MAX + 1u];
    uint32_t size;
    uint32_t is_app;
} rxr_file_info_t;

typedef struct {
    uint32_t valid;
    uint32_t files;
    uint32_t app_files;
    uint32_t installable;
    uint32_t total_bytes;
    uint32_t hash;
    uint32_t warnings;
    uint32_t errors;
    char primary_app[RXR_NAME_MAX + 1u];
} rxr_verify_info_t;

typedef struct {
    uint32_t ready;
    uint32_t files;
    uint32_t bytes;
} rxr_undo_info_t;

int rxr_file_count(const uint8_t* data, uint32_t len);
int rxr_file_at(const uint8_t* data, uint32_t len, uint32_t index, rxr_file_info_t* out);
int rxr_verify(const uint8_t* data, uint32_t len, rxr_verify_info_t* out);
int rxr_install(const uint8_t* data, uint32_t len);
int rxr_undo_last(void);
void rxr_undo_info(rxr_undo_info_t* out);
int rxr_selftest(void);
