#pragma once

#include <stdint.h>

#define LARDSEC_KEY_TEXT_MAX 39u

typedef struct {
    uint32_t enabled;
    uint32_t locked;
    uint32_t ecc_enabled;
    uint32_t sealed_writes;
    uint32_t opened_seals;
    uint32_t ecc_corrections;
    uint32_t ecc_failures;
    uint32_t scrubbed_bytes;
    uint32_t last_error;
    uint32_t key_hash;
    uint32_t key_discarded;
    char recovery_key[LARDSEC_KEY_TEXT_MAX + 1u];
} lardsec_info_t;

void lardsec_init(void);
int lardsec_enable(int on);
int lardsec_set_ecc(int on);
int lardsec_regen_key(uint32_t seed);
int lardsec_lock(void);
int lardsec_unlock(const char* recovery_key);
int lardsec_emergency_forget_key(uint32_t seed);
int lardsec_locked(void);
int lardsec_is_sealed_image(const uint8_t* image, uint32_t image_size);
int lardsec_seal_media_image(char drive, uint32_t base_lba,
                             const uint8_t* plain, uint8_t* sealed,
                             uint32_t image_size, uint32_t used_size);
int lardsec_open_media_image(char drive, uint32_t base_lba,
                             const uint8_t* sealed, uint8_t* plain,
                             uint32_t image_size, uint32_t used_size);
void lardsec_info(lardsec_info_t* out);
int lardsec_selftest(void);
