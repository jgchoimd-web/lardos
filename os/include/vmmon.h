#pragma once

#include <stdint.h>

enum {
    VMMON_BOSL = 0,
    VMMON_LIL = 1,
    VMMON_GASM = 2,
    VMMON_LAFILLO = 3,
    VMMON_OSVM = 4,
    VMMON_LHA = 5,
    VMMON_COUNT = 6
};

typedef struct {
    char name[16];
    uint32_t runs;
    uint32_t failures;
    uint32_t budget_hits;
    uint32_t last_steps;
    uint32_t max_steps;
    int32_t last_rc;
} vmmon_entry_t;

const char* vmmon_name(uint32_t id);
uint32_t vmmon_budget(uint32_t id);
void vmmon_record(uint32_t id, uint32_t steps, int rc);
void vmmon_reset(void);
int vmmon_info(uint32_t id, vmmon_entry_t* out);
int vmmon_selftest(void);
