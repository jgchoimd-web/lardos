/*
 * DRFL - Device Driver For LardOS loader.
 */
#include "drfl.h"
#include "fs.h"
#include "pci.h"
#include <stddef.h>
#include <stdint.h>

#define DRFL_MAX_ENTRIES  16
#define DRFL_MAX_NAME     32

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t type;
    uint8_t name_len;
    char name[DRFL_MAX_NAME];
} drfl_entry_t;

static drfl_entry_t s_entries[DRFL_MAX_ENTRIES];
static uint16_t s_entry_count;

static int match_suffix(const char* fn, const char* suffix)
{
    const char* p = fn;
    while (*fn) fn++;
    while (fn > p && *fn != '/' && *fn != '\\') fn--;
    if (*fn == '/' || *fn == '\\') fn++;
    while (*fn && *suffix && *fn == *suffix) { fn++; suffix++; }
    return (*suffix == '\0' && *fn == '\0');
}

int drfl_load(const char* name)
{
    const FsFile* f = fs_open(name);
    if (!f || f->size < 12) return -1;

    const uint8_t* d = f->data;
    uint32_t mag = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (mag != DRFL_MAGIC) return -2;
    if (d[4] != 1) return -3;

    uint16_t entry_count = (uint16_t)d[8] | ((uint16_t)d[9] << 8);
    if (entry_count > DRFL_MAX_ENTRIES) entry_count = DRFL_MAX_ENTRIES;
    if (s_entry_count + entry_count > DRFL_MAX_ENTRIES) entry_count = (uint16_t)(DRFL_MAX_ENTRIES - s_entry_count);

    uint32_t off = 10;
    for (uint16_t i = 0; i < entry_count && off + 6 < f->size; i++) {
        uint16_t vendor = (uint16_t)d[off] | ((uint16_t)d[off + 1] << 8);
        uint16_t device = (uint16_t)d[off + 2] | ((uint16_t)d[off + 3] << 8);
        uint8_t type = d[off + 4];
        uint8_t name_len = d[off + 5];
        off += 6;
        if (name_len > DRFL_MAX_NAME - 1) name_len = DRFL_MAX_NAME - 1;
        if (off + name_len > f->size) break;

        drfl_entry_t* e = &s_entries[s_entry_count++];
        e->vendor_id = vendor;
        e->device_id = device;
        e->type = type;
        e->name_len = name_len;
        for (uint8_t j = 0; j < name_len; j++) e->name[j] = (char)d[off + j];
        e->name[name_len] = '\0';
        off += name_len;
    }
    return 0;
}

static void load_cb(const char* name, uint32_t size, void* user)
{
    (void)size;
    (void)user;
    if (match_suffix(name, ".drfl")) {
        drfl_load(name);
    }
}

void drfl_load_all(void)
{
    s_entry_count = 0;
    fs_list(load_cb, NULL);
}

int drfl_probe_net(void* nic_ctx, drfl_net_init_fn init_fn)
{
    pci_addr_t dev;

    /* Try DRFL entries first */
    for (uint16_t i = 0; i < s_entry_count; i++) {
        drfl_entry_t* e = &s_entries[i];
        if (e->type != DRFL_TYPE_NET) continue;
        if (pci_find_device(e->vendor_id, e->device_id, &dev) == 0) {
            if (init_fn(nic_ctx) == 0) return 0;
        }
    }

    /* Fallback: built-in RTL8139 (0x10EC, 0x8139) */
    if (pci_find_device(0x10EC, 0x8139, &dev) == 0) {
        return init_fn(nic_ctx);
    }
    return -1;
}
