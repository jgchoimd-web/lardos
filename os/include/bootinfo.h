#pragma once

#include <stdint.h>

// Written by the boot sector at physical 0x90000 (identity-mapped).
// Keep this struct tiny and stable.
typedef struct {
    uint32_t magic;      // 'BINF'
    uint16_t version;    // 1
    uint16_t reserved0;

    uint32_t fb_addr_lo; // physical address of linear framebuffer (low 32)
    uint16_t fb_width;
    uint16_t fb_height;
    uint16_t fb_pitch;   // bytes per scanline
    uint8_t fb_bpp;      // bits per pixel (expect 32)
    uint8_t reserved1;
    uint16_t reserved2;
} bootinfo_t;

#define BOOTINFO_PADDR 0x00090000u

