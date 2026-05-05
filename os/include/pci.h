#pragma once

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
} pci_addr_t;

uint32_t pci_read32(pci_addr_t a, uint8_t off);
uint16_t pci_read16(pci_addr_t a, uint8_t off);
uint8_t pci_read8(pci_addr_t a, uint8_t off);
void pci_write32(pci_addr_t a, uint8_t off, uint32_t v);

int pci_find_device(uint16_t vendor, uint16_t device, pci_addr_t* out);

