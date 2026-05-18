#pragma once

#include <stdint.h>

#define LARD_INSTALL_IMAGE_SECTORS 2880u
#define LARD_INSTALL_STAGE2_SECTORS 8u
#define LARD_INSTALL_KERNEL_LBA 9u
#define LARD_INSTALL_LPST_LBA 2752u
#define LARD_INSTALL_BOOT_IMAGE_COPY_PADDR 0x01000000u

int lard_install_selftest(void);
void lard_install_status(char* out, uint32_t cap);
int lard_install_hdd_ssd(char* out, uint32_t cap);
