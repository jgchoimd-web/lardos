#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_cfg_addr(pci_addr_t a, uint8_t off)
{
    return 0x80000000u | ((uint32_t)a.bus << 16) | ((uint32_t)a.slot << 11) | ((uint32_t)a.func << 8) |
           (off & 0xFC);
}

uint32_t pci_read32(pci_addr_t a, uint8_t off)
{
    outl(PCI_CONFIG_ADDR, pci_cfg_addr(a, off));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(pci_addr_t a, uint8_t off)
{
    uint32_t v = pci_read32(a, (uint8_t)(off & 0xFC));
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(pci_addr_t a, uint8_t off)
{
    uint32_t v = pci_read32(a, (uint8_t)(off & 0xFC));
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

void pci_write32(pci_addr_t a, uint8_t off, uint32_t v)
{
    outl(PCI_CONFIG_ADDR, pci_cfg_addr(a, off));
    outl(PCI_CONFIG_DATA, v);
}

int pci_find_device(uint16_t vendor, uint16_t device, pci_addr_t* out)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                pci_addr_t a = {(uint8_t)bus, slot, func};
                uint16_t ven = pci_read16(a, 0x00);
                if (ven == 0xFFFF) {
                    if (func == 0) {
                        break;
                    }
                    continue;
                }
                uint16_t dev = pci_read16(a, 0x02);
                if (ven == vendor && dev == device) {
                    *out = a;
                    return 0;
                }

                if (func == 0) {
                    uint8_t hdr = pci_read8(a, 0x0E);
                    if ((hdr & 0x80) == 0) {
                        break;
                    }
                }
            }
        }
    }
    return -1;
}

