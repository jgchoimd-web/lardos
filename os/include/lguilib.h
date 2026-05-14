#pragma once

#include <stdint.h>

#define LGUILIB_NAME_MAX    31u
#define LGUILIB_WIDGET_MAX  24u

typedef struct {
    char name[LGUILIB_NAME_MAX + 1u];
    uint32_t title_bg;
    uint32_t title_fg;
    uint32_t title_accent;
    uint32_t panel_bg;
    uint32_t border;
    uint32_t tab_active;
    uint32_t tab_idle;
    uint32_t tab_hover;
    uint32_t tab_accent;
    uint32_t button_border;
    uint32_t button_hover;
    uint32_t button_inner;
    uint32_t output_frame;
    uint32_t hint_fg;
    uint32_t shadow;
    uint32_t widget_count;
    uint32_t last_error;
} lguilib_theme_t;

typedef struct {
    uint32_t valid;
    lguilib_theme_t theme;
} lguilib_info_t;

void lguilib_init(void);
int lguilib_parse(const uint8_t* data, uint32_t len, lguilib_theme_t* out);
int lguilib_load_active(const uint8_t* data, uint32_t len);
void lguilib_active(lguilib_info_t* out);
const lguilib_theme_t* lguilib_active_theme(void);
const char* lguilib_error_name(uint32_t error);
int lguilib_selftest(void);
