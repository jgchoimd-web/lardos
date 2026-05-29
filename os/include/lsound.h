#pragma once

#include <stdint.h>

#define LSOUND_NAME_MAX 31u

typedef struct {
    uint32_t enabled;
    uint32_t boot_enabled;
    uint32_t fx_enabled;
    uint32_t events;
    uint32_t notes;
    uint32_t rests;
    uint32_t sweeps;
    uint32_t last_error;
    char boot_file[LSOUND_NAME_MAX + 1u];
    char last_file[LSOUND_NAME_MAX + 1u];
} lsound_info_t;

void lsound_init(void);
int lsound_reload(void);
int lsound_set_enabled(int on);
int lsound_set_boot_enabled(int on);
int lsound_set_fx_enabled(int on);
int lsound_set_boot_file(const char* file);
int lsound_play_file(const char* file);
int lsound_play_effect(const char* name);
int lsound_boot(void);
int lsound_write_template(const char* file);
void lsound_info(lsound_info_t* out);
int lsound_selftest(void);
