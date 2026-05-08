#pragma once

#include <stdint.h>

typedef struct {
    uint32_t pass;
    uint32_t fail;
    uint32_t storage_available;
    uint32_t storage_dirty;
    int storage_last_result;
    uint32_t storage_generation;
} lard_post_result_t;

typedef void (*lard_post_emit_fn)(const char* status, const char* name, void* user);

/* Run LardOS Power-On Self-Test checks. Safe after mem/fs/drfl/gui init. */
void lard_post_run(lard_post_emit_fn emit, void* user, lard_post_result_t* out);

