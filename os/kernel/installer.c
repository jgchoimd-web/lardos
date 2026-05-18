#include "installer.h"

#include "storage.h"
#include "version.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "boot_stage1.inc"
#include "boot_stage2.inc"

#define SECTOR_SIZE STORAGE_SECTOR_SIZE

static uint8_t s_install_sector[SECTOR_SIZE];

static void inst_append(char* out, uint32_t cap, const char* s)
{
    uint32_t i = 0;
    if (!out || cap == 0 || !s) return;
    while (i + 1u < cap && out[i]) i++;
    while (*s && i + 1u < cap) out[i++] = *s++;
    out[i] = '\0';
}

static void inst_append_u32(char* out, uint32_t cap, uint32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", v);
    inst_append(out, cap, tmp);
}

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t* boot_kernel_image(void)
{
    return (const uint8_t*)(uintptr_t)LARD_INSTALL_BOOT_IMAGE_COPY_PADDR;
}

static int boot_kernel_size(uint32_t* out_size)
{
    const uint8_t* k = boot_kernel_image();
    uint32_t sz;
    if (!k) return -1;
    if (k[0] == 'L' && k[1] == 'A' && k[2] == 'R' && k[3] == 'D' && rd16(k + 4) >= 2u) {
        sz = rd32(k + 0x12);
    } else if (k[0] == 'B' && k[1] == 'O' && k[2] == 'S' && k[3] == 'X') {
        sz = rd32(k + 0x10);
    } else {
        return -2;
    }
    if (sz == 0 || sz > (LARD_INSTALL_LPST_LBA - LARD_INSTALL_KERNEL_LBA) * SECTOR_SIZE) return -3;
    if (out_size) *out_size = sz;
    return 0;
}

static void zero_sector(void)
{
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) s_install_sector[i] = 0;
}

static int write_padded_sector(uint32_t lba, const uint8_t* data, uint32_t len)
{
    zero_sector();
    if (len > SECTOR_SIZE) len = SECTOR_SIZE;
    for (uint32_t i = 0; i < len; i++) s_install_sector[i] = data[i];
    return storage_write_sector(lba, s_install_sector);
}

int lard_install_selftest(void)
{
    if (sizeof(lardos_boot_stage1) != SECTOR_SIZE) return -1;
    if (lardos_boot_stage1[510] != 0x55 || lardos_boot_stage1[511] != 0xAA) return -2;
    if (sizeof(lardos_boot_stage2) == 0 || sizeof(lardos_boot_stage2) > LARD_INSTALL_STAGE2_SECTORS * SECTOR_SIZE) return -3;
    if (LARD_INSTALL_KERNEL_LBA != 1u + LARD_INSTALL_STAGE2_SECTORS) return -4;
    return 0;
}

void lard_install_status(char* out, uint32_t cap)
{
    uint32_t ksize = 0;
    uint32_t ksectors;
    uint32_t total;
    uint32_t disk_sectors;

    if (!out || cap == 0) return;
    out[0] = '\0';
    (void)storage_init();
    inst_append(out, cap, "LardOS HDD/SSD Installer\n");
    inst_append(out, cap, "  version: ");
    inst_append(out, cap, LARDOS_VERSION);
    inst_append(out, cap, "\n  driver: ");
    inst_append(out, cap, storage_driver_name());
    disk_sectors = storage_sector_count();
    inst_append(out, cap, "\n  target sectors: ");
    if (disk_sectors) inst_append_u32(out, cap, disk_sectors);
    else inst_append(out, cap, "unknown");

    if (boot_kernel_size(&ksize) != 0) {
        inst_append(out, cap, "\n  preserved boot image: unavailable\n");
        inst_append(out, cap, "  install: blocked until stage2 preserves the boot image\n");
        return;
    }
    ksectors = (ksize + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    total = LARD_INSTALL_KERNEL_LBA + ksectors;
    inst_append(out, cap, "\n  layout: LBA0 stage1, LBA1..");
    inst_append_u32(out, cap, LARD_INSTALL_KERNEL_LBA - 1u);
    inst_append(out, cap, " stage2, LBA");
    inst_append_u32(out, cap, LARD_INSTALL_KERNEL_LBA);
    inst_append(out, cap, "..");
    inst_append_u32(out, cap, total ? total - 1u : 0u);
    inst_append(out, cap, " kernel\n  kernel bytes: ");
    inst_append_u32(out, cap, ksize);
    inst_append(out, cap, ", sectors: ");
    inst_append_u32(out, cap, ksectors);
    inst_append(out, cap, "\n  LPST writable store starts at LBA ");
    inst_append_u32(out, cap, LARD_INSTALL_LPST_LBA);
    inst_append(out, cap, "\n  command: install hdd yes  or  install ssd yes\n");
    inst_append(out, cap, "  warning: this overwrites the target boot sectors by design.\n");
}

int lard_install_hdd_ssd(char* out, uint32_t cap)
{
    const uint8_t* k = boot_kernel_image();
    uint32_t ksize = 0;
    uint32_t ksectors;
    uint32_t total;
    uint32_t disk_sectors;
    int r;

    if (out && cap) out[0] = '\0';
    if (lard_install_selftest() != 0) {
        inst_append(out, cap, "install: embedded boot stages failed selftest.\n");
        return -1;
    }
    if (boot_kernel_size(&ksize) != 0) {
        inst_append(out, cap, "install: preserved boot image at 0x01000000 is unavailable.\n");
        return -2;
    }
    if (storage_init() != 0) {
        inst_append(out, cap, "install: no ATA HDD/SSD target is available.\n");
        return -3;
    }
    ksectors = (ksize + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    total = LARD_INSTALL_KERNEL_LBA + ksectors;
    if (total > LARD_INSTALL_LPST_LBA) {
        inst_append(out, cap, "install: kernel would overlap LPST writable store.\n");
        return -4;
    }
    disk_sectors = storage_sector_count();
    if (disk_sectors && disk_sectors < LARD_INSTALL_IMAGE_SECTORS) {
        inst_append(out, cap, "install: target disk is smaller than the LardOS boot image.\n");
        return -5;
    }

    r = write_padded_sector(0, lardos_boot_stage1, (uint32_t)sizeof(lardos_boot_stage1));
    if (r != 0) {
        inst_append(out, cap, "install: failed writing stage1 LBA0.\n");
        return -10 + r;
    }
    for (uint32_t i = 0; i < LARD_INSTALL_STAGE2_SECTORS; i++) {
        uint32_t off = i * SECTOR_SIZE;
        uint32_t remain = sizeof(lardos_boot_stage2) > off ? (uint32_t)sizeof(lardos_boot_stage2) - off : 0u;
        uint32_t len = remain > SECTOR_SIZE ? SECTOR_SIZE : remain;
        const uint8_t* src = len ? lardos_boot_stage2 + off : lardos_boot_stage2;
        r = write_padded_sector(1u + i, src, len);
        if (r != 0) {
            inst_append(out, cap, "install: failed writing stage2.\n");
            return -20 + r;
        }
    }
    for (uint32_t i = 0; i < ksectors; i++) {
        uint32_t off = i * SECTOR_SIZE;
        uint32_t remain = ksize > off ? ksize - off : 0u;
        uint32_t len = remain > SECTOR_SIZE ? SECTOR_SIZE : remain;
        r = write_padded_sector(LARD_INSTALL_KERNEL_LBA + i, k + off, len);
        if (r != 0) {
            inst_append(out, cap, "install: failed writing kernel payload.\n");
            return -30 + r;
        }
    }

    inst_append(out, cap, "install: wrote LardOS ");
    inst_append(out, cap, LARDOS_VERSION);
    inst_append(out, cap, " to HDD/SSD target.\n  sectors written: ");
    inst_append_u32(out, cap, total);
    inst_append(out, cap, "\n  restart or set firmware boot order to boot from the installed disk.\n");
    return 0;
}
