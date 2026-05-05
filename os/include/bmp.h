#pragma once

#include <stdint.h>

typedef struct {
    uint32_t* pixels;
    uint32_t w;
    uint32_t h;
    int has_alpha;
} bmp_result_t;

/* Decode BMP. If out->pixels is NULL, only fills w/h. Otherwise decodes to ARGB. */
int bmp_decode(const uint8_t* data, uint32_t len, bmp_result_t* out);
