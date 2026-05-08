#pragma once

#include <stdint.h>

#define AWAKE_PHASE_MAX 31u

typedef struct {
    uint32_t enabled;
    uint32_t phase;
    uint32_t total;
    uint32_t done;
    uint32_t background_runs;
    uint32_t last_error;
    char current[AWAKE_PHASE_MAX + 1u];
} awake_info_t;

void awake_init(void);
void awake_enable(int enabled, uint32_t total);
void awake_mark(uint32_t phase, const char* current);
void awake_finish(void);
void awake_fail(uint32_t error, const char* current);
void awake_info(awake_info_t* out);
int awake_selftest(void);
