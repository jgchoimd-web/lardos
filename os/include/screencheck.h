#pragma once

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t changed_samples;
    uint32_t tiles_checked;
    uint32_t bad_tiles;
    int window_inside;
    int response_view_ok;
    uint32_t last_error;
} screencheck_info_t;

int screencheck_probe(screencheck_info_t* out);
void screencheck_draw_retro(void);
int screencheck_selftest(void);
