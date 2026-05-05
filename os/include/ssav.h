/*
 * SSAV - LardOS Screensaver format
 *
 * Binary (little-endian):
 *   0x00 magic[4]   = "SSAV"
 *   0x04 version u8 = 1
 *   0x05 delay_cs u8  delay between frames (centiseconds, 0=static)
 *   0x06 reserved[2]
 *   0x08 frame_count u16
 *   0x0A w u16, h u16
 *   0x0E bpp u8 (24 or 32)
 *   0x0F reserved
 *   0x10 frames: frame_count * (w*h*bytes_per_pixel) RGBA/BGRA
 */
#pragma once

#include <stdint.h>

#define SSAV_MAGIC  0x56415353u  /* "SSAV" LE */

#define SSAV_MAX_FRAMES  64
#define SSAV_MAX_W       128
#define SSAV_MAX_H       128
#define SSAV_MAX_PIXELS  (SSAV_MAX_W * SSAV_MAX_H)

typedef struct {
    uint16_t frame_count;
    uint16_t w;
    uint16_t h;
    uint8_t bpp;
    uint8_t delay_cs;
    const uint8_t* frame_data;  /* points into file data */
} ssav_t;

/* Parse SSAV. out->frame_data points into data. Returns 0 on success. */
int ssav_decode(const uint8_t* data, uint32_t len, ssav_t* out);

/* Decode frame N into ARGB buffer (out must hold w*h uint32_t). Returns 0 on success. */
int ssav_decode_frame(const ssav_t* s, uint16_t frame, uint32_t* out);
