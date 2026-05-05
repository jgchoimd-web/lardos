/*
 * LDI - LardOS Image format
 * Magic "LDIM", raw BGR/BGRA pixel data.
 */
#pragma once

#include <stdint.h>

#define LDI_MAGIC 0x4D49444Cu  /* "LDIM" LE */

typedef struct {
    uint32_t* pixels;  /* ARGB output; caller allocates w*h*4 */
    uint32_t w;
    uint32_t h;
    int bpp;
} ldi_result_t;

/* Decode LDI. Returns 0 on success. If pixels is NULL, only fills w/h/bpp. */
int ldi_decode(const uint8_t* data, uint32_t len, ldi_result_t* out);
