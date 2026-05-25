#include "bootmeta.h"

#include "bootinfo.h"

#include <stddef.h>
#include <stdint.h>

static void bootmeta_zero(bootmeta_info_t* out)
{
    uint8_t* p = (uint8_t*)out;
    for (uint32_t i = 0; i < sizeof(*out); i++) p[i] = 0;
}

static uint32_t bootmeta_default_capacity(void)
{
    return BOOTINFO_DEFAULT_KERNEL_CAPACITY_BYTES;
}

void bootmeta_info(bootmeta_info_t* out)
{
    const bootinfo_t* bi = (const bootinfo_t*)(uintptr_t)BOOTINFO_PADDR;
    if (!out) return;
    bootmeta_zero(out);

    out->high_copy_paddr = BOOTINFO_DEFAULT_IMAGE_COPY_PADDR;
    out->boot_chunk_sectors = BOOTINFO_DEFAULT_BOOT_CHUNK_SECTORS;
    out->boot_image_sectors = BOOTINFO_DEFAULT_IMAGE_SECTORS;
    out->boot_persist_start_sector = BOOTINFO_DEFAULT_PERSIST_START_SECTOR;
    out->boot_persist_sectors = BOOTINFO_DEFAULT_PERSIST_SECTORS;
    out->boot_stage2_sectors = BOOTINFO_DEFAULT_STAGE2_SECTORS;
    out->kernel_lba_base = BOOTINFO_DEFAULT_KERNEL_LBA;
    out->kernel_capacity_bytes = bootmeta_default_capacity();

    if (bi->magic == BOOTINFO_MAGIC) {
        out->present = 1u;
        out->version = bi->version;
        if (bi->version >= 2u) {
            out->flags = bi->loader_flags;
            if (bi->boot_image_copy_paddr) out->high_copy_paddr = bi->boot_image_copy_paddr;
            out->kernel_file_size = bi->kernel_file_size;
            out->kernel_total_sectors = bi->kernel_total_sectors;
            if (bi->kernel_lba_base) out->kernel_lba_base = bi->kernel_lba_base;
            if (bi->boot_chunk_sectors) out->boot_chunk_sectors = bi->boot_chunk_sectors;
            if (bi->boot_image_sectors) out->boot_image_sectors = bi->boot_image_sectors;
            if (bi->boot_persist_start_sector) out->boot_persist_start_sector = bi->boot_persist_start_sector;
            if (bi->boot_persist_sectors) out->boot_persist_sectors = bi->boot_persist_sectors;
            if (bi->boot_stage2_sectors) out->boot_stage2_sectors = bi->boot_stage2_sectors;
            if (bi->boot_kernel_capacity_bytes) out->kernel_capacity_bytes = bi->boot_kernel_capacity_bytes;
        }
    }

    if (out->kernel_file_size == 0 && out->kernel_total_sectors) {
        out->kernel_file_size = out->kernel_total_sectors * 512u;
    }
    if (out->kernel_capacity_bytes > out->kernel_file_size) {
        out->headroom_bytes = out->kernel_capacity_bytes - out->kernel_file_size;
        out->headroom_percent = (out->headroom_bytes * 100u) / out->kernel_capacity_bytes;
    }
}

int bootmeta_selftest(void)
{
    bootmeta_info_t info;
    bootmeta_info(&info);
    if (!info.present) return -1;
    if (info.version < 2u) return -2;
    if ((info.flags & BOOTINFO_FLAG_LOADER_META) == 0) return -3;
    if ((info.flags & BOOTINFO_FLAG_HIGH_COPY) == 0) return -4;
    if (info.high_copy_paddr != BOOTINFO_DEFAULT_IMAGE_COPY_PADDR) return -5;
    if (info.kernel_file_size == 0 || info.kernel_total_sectors == 0) return -6;
    if (info.kernel_capacity_bytes <= info.kernel_file_size) return -7;
    if (info.boot_persist_start_sector <= info.kernel_lba_base) return -8;
    return 0;
}
