#pragma once

#include <stdint.h>

typedef struct {
    uint32_t present;
    uint32_t version;
    uint32_t flags;
    uint32_t high_copy_paddr;
    uint32_t kernel_file_size;
    uint32_t kernel_total_sectors;
    uint32_t kernel_lba_base;
    uint32_t boot_chunk_sectors;
    uint32_t boot_image_sectors;
    uint32_t boot_persist_start_sector;
    uint32_t boot_persist_sectors;
    uint32_t boot_stage2_sectors;
    uint32_t kernel_capacity_bytes;
    uint32_t headroom_bytes;
    uint32_t headroom_percent;
} bootmeta_info_t;

void bootmeta_info(bootmeta_info_t* out);
int bootmeta_selftest(void);
