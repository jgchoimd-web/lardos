#pragma once

#include <stdint.h>

#define LASSIST_TEXT_MAX 95u

typedef struct {
    uint32_t enabled;
    uint32_t jokes;
    uint32_t tick;
    uint32_t mood;
    char message[LASSIST_TEXT_MAX + 1u];
} lassist_info_t;

void lassist_init(void);
void lassist_enable(int on);
int lassist_enabled(void);
void lassist_tick(uint32_t app_id);
void lassist_draw(uint32_t app_id, uint32_t mouse_x, uint32_t mouse_y,
                  uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void lassist_next(uint32_t app_id);
void lassist_joke(void);
void lassist_info(lassist_info_t* out);
int lassist_selftest(void);
