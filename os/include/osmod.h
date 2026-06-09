#pragma once

#include <stdint.h>

#define OSMOD_NAME_MAX 31u
#define OSMOD_BOOT_MAX 15u
#define OSMOD_TEXT_MAX 95u

#define OSMOD_UNSET (-1)

typedef struct {
    char name[OSMOD_NAME_MAX + 1u];
    char boot[OSMOD_BOOT_MAX + 1u];
    uint32_t have_boot;
    int32_t awake;
    int32_t aa;
    int32_t brightness;
    int32_t resize;
    int32_t lsb;
    int32_t vblank;
    int32_t sound;
    int32_t bluetooth;
    int32_t lconnect;
    uint32_t directives;
    uint32_t warnings;
    uint32_t lines;
    char note[OSMOD_TEXT_MAX + 1u];
} osmod_profile_t;

void osmod_init_profile(osmod_profile_t* out);
const char* osmod_last_error(void);
const char* osmod_aa_name(int32_t mode);
const char* osmod_resize_name(int32_t mode);
const char* osmod_default_sample(void);
uint32_t osmod_default_sample_size(void);
int osmod_parse(const uint8_t* data, uint32_t size, osmod_profile_t* out);
int osmod_load_file(const char* name, osmod_profile_t* out);
int osmod_apply(const osmod_profile_t* profile);
int osmod_write_report(const osmod_profile_t* profile, int applied);
int osmod_selftest(void);
