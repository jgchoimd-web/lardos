#pragma once

#include <stdint.h>

#define BOOTPROF_NAME_MAX 15u

typedef struct {
    char name[BOOTPROF_NAME_MAX + 1u];
    uint32_t force_post;
    uint32_t network;
    uint32_t dev_mode;
    uint32_t safe_mode;
    uint32_t awakening_mode;
} bootprof_info_t;

void bootprof_init(void);
void bootprof_load(void);
int bootprof_set(const char* name);
void bootprof_info(bootprof_info_t* out);
int bootprof_network_enabled(void);
int bootprof_force_post(void);
int bootprof_dev_mode(void);
int bootprof_safe_mode(void);
int bootprof_awakening_mode(void);
int bootprof_selftest(void);
