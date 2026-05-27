#pragma once

#include <stdint.h>

typedef struct {
    uint32_t initialized;
    uint32_t active;
    uint32_t module_independent;
    uint32_t panicroom_entries;
    uint32_t lockdowns;
    uint32_t key_discards;
    uint32_t reports;
    uint32_t media_sync_attempts;
    uint32_t media_sync_failures;
    uint32_t last_action;
    int32_t last_result;
    char last_reason[96];
} auxkernel_info_t;

void auxkernel_init(void);
void auxkernel_enter_panicroom(const char* reason);
int auxkernel_lockdown(const char* reason);
int auxkernel_discard_keys(const char* reason);
int auxkernel_report(void);
void auxkernel_info(auxkernel_info_t* out);
int auxkernel_selftest(void);
