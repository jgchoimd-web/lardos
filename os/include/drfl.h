/*
 * DRFL - Device Driver For LardOS
 *
 * DRFL 1 binary format (little-endian, compatibility):
 *   0x00 magic[4]     = "DRFL"
 *   0x04 version u8   = 1
 *   0x05 reserved[3]
 *   0x08 entry_count u16
 *   For each entry:
 *     vendor_id  u16   PCI vendor ID
 *     device_id  u16   PCI device ID
 *     type       u8    0=net, 1=block, ...
 *     name_len   u8
 *     name[]     driver name (e.g. "rtl8139")
 *
 * DRFL 2 text format (preferred, code-carrying):
 *   DRFL 2
 *   ID rtl8139
 *   TYPE net
 *   PCI 10EC 8139
 *   LANG DRFL-C
 *   CODE int drfl_init(void* ctx) { ... }
 *   END
 *
 * DRFL 2 files must contain CODE lines. The loaded source stays visible through
 * drivers show and is hashed, so the .drfl is the driver source/control body,
 * not only a PCI descriptor.
 *
 * Load with drfl_load() from FS. Probe with drfl_probe_net() etc.
 */
#pragma once

#include <stdint.h>

#define DRFL_MAGIC  0x4C464452u  /* "DRFL" LE */

#define DRFL_TYPE_NET   0
#define DRFL_TYPE_BLOCK 1

#define DRFL_MAX_ENTRIES  16
#define DRFL_MAX_NAME     32
#define DRFL_MAX_LANG     16
#define DRFL_MAX_CODE     1024

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t type;
    uint8_t format_version;
    char name[DRFL_MAX_NAME];
    char lang[DRFL_MAX_LANG];
    uint32_t code_len;
    uint32_t code_hash;
    const char* code;
} drfl_info_t;

/* Load all .drfl files from FS. Call after fs_init. */
void drfl_load_all(void);

/* Load single DRFL file by name. Returns 0 on success. */
int drfl_load(const char* name);

/* Net driver init: takes nic context (e.g. rtl8139_t*). Returns 0 on success. */
typedef int (*drfl_net_init_fn)(void* nic_ctx);
typedef int (*drfl_block_init_fn)(void* block_ctx);
typedef void (*drfl_list_cb)(uint16_t vendor_id, uint16_t device_id, uint8_t type,
                             const char* name, void* user);

/* Enumerate loaded driver files. Returns entry count. */
uint32_t drfl_list(drfl_list_cb cb, void* user);

/* Get one loaded driver file entry with code/source metadata. Returns 0 on success. */
int drfl_info(uint32_t index, drfl_info_t* out);

/* Probe for net device. Tries DRFL entries (vendor/device + driver name), then built-in fallback.
   For each DRFL entry with type=net, tries pci_find. If found and name matches, calls init_fn.
   Built-in fallback: vendor 0x10EC device 0x8139 uses init_fn (rtl8139). */
int drfl_probe_net(void* nic_ctx, const char* driver_name, drfl_net_init_fn init_fn);

/* Probe for block device. Built-in fallback: Intel PIIX3 IDE (0x8086, 0x7010). */
int drfl_probe_block(void* block_ctx, const char* driver_name, drfl_block_init_fn init_fn);
