#include "storage.h"

#include "io.h"

#include <stddef.h>

#define ATA_DATA     0x1F0
#define ATA_ERROR    0x1F1
#define ATA_SECCNT   0x1F2
#define ATA_LBA0     0x1F3
#define ATA_LBA1     0x1F4
#define ATA_LBA2     0x1F5
#define ATA_DRIVE    0x1F6
#define ATA_STATUS   0x1F7
#define ATA_COMMAND  0x1F7
#define ATA_CONTROL  0x3F6

#define ATA_SR_ERR   0x01
#define ATA_SR_DRQ   0x08
#define ATA_SR_DF    0x20
#define ATA_SR_BSY   0x80

static int s_inited;
static int s_present;
static uint32_t s_sector_count;

static void ata_delay(void)
{
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
}

static int ata_wait_not_busy(void)
{
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st == 0xFF) return -1;
        if ((st & ATA_SR_BSY) == 0) return 0;
        __asm__ __volatile__("pause");
    }
    return -2;
}

static int ata_wait_drq(void)
{
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) return 0;
        __asm__ __volatile__("pause");
    }
    return -2;
}

static int ata_identify(void)
{
    uint16_t ident[256];
    outb(ATA_CONTROL, 0x02); /* nIEN: polling mode */
    outb(ATA_DRIVE, 0xA0);
    ata_delay();
    outb(ATA_SECCNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, 0xEC);
    ata_delay();

    uint8_t st = inb(ATA_STATUS);
    if (st == 0 || st == 0xFF) return -1;
    if (ata_wait_not_busy() != 0) return -2;
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0) return -3;
    if (ata_wait_drq() != 0) return -4;

    for (uint32_t i = 0; i < 256u; i++) ident[i] = inw(ATA_DATA);
    s_sector_count = (uint32_t)ident[60] | ((uint32_t)ident[61] << 16);
    return 0;
}

int storage_init(void)
{
    if (s_inited) return s_present ? 0 : -1;
    s_inited = 1;
    s_present = (ata_identify() == 0) ? 1 : 0;
    return s_present ? 0 : -1;
}

int storage_available(void)
{
    if (!s_inited) (void)storage_init();
    return s_present;
}

const char* storage_driver_name(void)
{
    return storage_available() ? "ata-pio" : "none";
}

uint32_t storage_sector_count(void)
{
    if (!s_inited) (void)storage_init();
    return s_present ? s_sector_count : 0;
}

static void ata_select_lba(uint32_t lba)
{
    outb(ATA_DRIVE, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(ATA_SECCNT, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFFu));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
}

int storage_read_sector(uint32_t lba, uint8_t* out)
{
    if (!out) return -1;
    if (storage_init() != 0) return -2;
    if (ata_wait_not_busy() != 0) return -3;
    ata_select_lba(lba);
    outb(ATA_COMMAND, 0x20);
    if (ata_wait_drq() != 0) return -4;

    for (uint32_t i = 0; i < 256u; i++) {
        uint16_t w = inw(ATA_DATA);
        out[i * 2u] = (uint8_t)(w & 0xFFu);
        out[i * 2u + 1u] = (uint8_t)(w >> 8);
    }
    return 0;
}

int storage_write_sector(uint32_t lba, const uint8_t* data)
{
    if (!data) return -1;
    if (storage_init() != 0) return -2;
    if (ata_wait_not_busy() != 0) return -3;
    ata_select_lba(lba);
    outb(ATA_COMMAND, 0x30);
    if (ata_wait_drq() != 0) return -4;

    for (uint32_t i = 0; i < 256u; i++) {
        uint16_t w = (uint16_t)data[i * 2u] | ((uint16_t)data[i * 2u + 1u] << 8);
        outw(ATA_DATA, w);
    }
    if (ata_wait_not_busy() != 0) return -5;
    outb(ATA_COMMAND, 0xE7);
    if (ata_wait_not_busy() != 0) return -6;
    return 0;
}
