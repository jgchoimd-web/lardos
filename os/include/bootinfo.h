#pragma once

#include <stdint.h>

// Written by stage2 at physical 0x1000 (identity-mapped).
// Keep the framebuffer prefix stable; loader-capacity fields are append-only.
typedef struct {
    uint32_t magic;      // 'BINF'
    uint16_t version;    // 1=framebuffer only, 2=loader capacity metadata
    uint16_t reserved0;

    uint32_t fb_addr_lo; // physical address of linear framebuffer (low 32)
    uint16_t fb_width;
    uint16_t fb_height;
    uint16_t fb_pitch;   // bytes per scanline
    uint8_t fb_bpp;      // bits per pixel (24 or 32)
    uint8_t reserved1;
    uint16_t reserved2;

    uint32_t boot_image_copy_paddr;
    uint32_t kernel_file_size;
    uint32_t kernel_total_sectors;
    uint32_t kernel_lba_base;
    uint32_t boot_chunk_sectors;
    uint32_t boot_image_sectors;
    uint32_t boot_persist_start_sector;
    uint32_t boot_persist_sectors;
    uint32_t boot_kernel_capacity_bytes;
    uint32_t loader_flags;
    uint32_t boot_stage2_sectors;
    uint32_t reserved3;
} bootinfo_t;

#define BOOTINFO_PADDR 0x00001000u
#define BOOTINFO_MAGIC 0x464E4942u
#define BOOTINFO_VERSION 2u

#define BOOTINFO_FLAG_HIGH_COPY      0x00000001u
#define BOOTINFO_FLAG_LOADER_META    0x00000002u
#define BOOTINFO_FLAG_RESIZABLE_IMG  0x00000004u

#define BOOTINFO_DEFAULT_IMAGE_SECTORS 2880u
#define BOOTINFO_DEFAULT_STAGE2_SECTORS 8u
#define BOOTINFO_DEFAULT_KERNEL_LBA 9u
#define BOOTINFO_DEFAULT_PERSIST_START_SECTOR 2752u
#define BOOTINFO_DEFAULT_PERSIST_SECTORS 128u
#define BOOTINFO_DEFAULT_BOOT_CHUNK_SECTORS 32u
#define BOOTINFO_DEFAULT_IMAGE_COPY_PADDR 0x01000000u
#define BOOTINFO_DEFAULT_KERNEL_CAPACITY_BYTES \
    ((BOOTINFO_DEFAULT_PERSIST_START_SECTOR - BOOTINFO_DEFAULT_KERNEL_LBA) * 512u)
