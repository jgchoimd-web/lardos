#pragma once

#include <stdint.h>

#define MEGACLIP_SLOTS 10u
#define MEGACLIP_PIN_SLOTS 10u
#define MEGACLIP_KIND_MAX 15u
#define MEGACLIP_LABEL_MAX 47u
#define MEGACLIP_DATA_MAX 2048u

#define MEGACLIP_MODE_STACK 0u
#define MEGACLIP_MODE_SINGLE 1u
#define MEGACLIP_MODE_ORDER 2u

typedef struct {
    uint32_t used;
    uint32_t slot;
    uint32_t sequence;
    uint32_t size;
    char kind[MEGACLIP_KIND_MAX + 1u];
    char label[MEGACLIP_LABEL_MAX + 1u];
    uint8_t data[MEGACLIP_DATA_MAX + 1u];
} megaclip_item_t;

typedef struct {
    uint32_t mode;
    uint32_t count;
    uint32_t capacity;
    uint32_t pushes;
    uint32_t pulls;
    uint32_t dropped;
    uint32_t last_error;
    uint32_t pin_count;
    uint32_t pin_sets;
    uint32_t pin_pulls;
} megaclip_status_t;

void megaclip_init(void);
int megaclip_set_mode(uint32_t mode);
uint32_t megaclip_mode(void);
const char* megaclip_mode_name(uint32_t mode);
int megaclip_push(const char* kind, const char* label, const uint8_t* data, uint32_t size);
int megaclip_push_text(const char* label, const char* text);
int megaclip_pull(uint32_t view_index, megaclip_item_t* out);
int megaclip_pull_latest(megaclip_item_t* out);
uint32_t megaclip_count(void);
void megaclip_clear(void);
int megaclip_pin_set(uint32_t slot, const char* kind, const char* label, const uint8_t* data, uint32_t size);
int megaclip_pin_set_text(uint32_t slot, const char* label, const char* text);
int megaclip_pin_pull(uint32_t slot, megaclip_item_t* out);
int megaclip_pin_clear(uint32_t slot);
uint32_t megaclip_pin_count(void);
int megaclip_pin_reload(void);
void megaclip_status(megaclip_status_t* out);
int megaclip_selftest(void);
