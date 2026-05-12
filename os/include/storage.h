#pragma once

#include <stdint.h>

#define STORAGE_SECTOR_SIZE 512u

int storage_init(void);
int storage_available(void);
const char* storage_driver_name(void);
uint32_t storage_sector_count(void);
int storage_read_sector(uint32_t lba, uint8_t* out);
int storage_write_sector(uint32_t lba, const uint8_t* data);
