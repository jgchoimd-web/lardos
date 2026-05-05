/*
 * DRFL - Device Driver For LardOS
 *
 * Binary format (little-endian):
 *   0x00 magic[4]     = "DRFL"
 *   0x04 version u8   = 1
 *   0x05 reserved[3]
 *   0x08 entry_count u16
 *   For each entry:
 *     vendor_id  u16   PCI vendor ID
 *     device_id  u16   PCI device ID
 *     type       u8    0=net, 1=block, ...
 *     name_len   u8
 *     name[]     driver name (e.g. "rtl8139") - used to select built-in init
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

/* Load all .drfl files from FS. Call after fs_init. */
void drfl_load_all(void);

/* Load single DRFL file by name. Returns 0 on success. */
int drfl_load(const char* name);

/* Net driver init: takes nic context (e.g. rtl8139_t*). Returns 0 on success. */
typedef int (*drfl_net_init_fn)(void* nic_ctx);

/* Probe for net device. Tries DRFL entries (vendor/device + driver name), then built-in fallback.
   For each DRFL entry with type=net, tries pci_find. If found and name matches, calls init_fn.
   Built-in fallback: vendor 0x10EC device 0x8139 uses init_fn (rtl8139). */
int drfl_probe_net(void* nic_ctx, drfl_net_init_fn init_fn);
