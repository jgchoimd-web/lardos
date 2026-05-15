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
    uint8_t format_version;
    char lang[DRFL_MAX_LANG];
    char code[DRFL_MAX_CODE];
    uint32_t code_len;
    uint32_t code_hash;
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

static int cstr_eq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static uint32_t hash_bytes(const char* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static int line_eq(const char* p, const char* end, const char* s)
{
    while (p < end && *s && *p == *s) { p++; s++; }
    return p == end && *s == '\0';
}

static int line_prefix(const char* p, const char* end, const char* prefix, const char** value)
{
    while (p < end && *prefix && *p == *prefix) { p++; prefix++; }
    if (*prefix) return 0;
    if (p < end && *p != ' ' && *p != '\t') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (value) *value = p;
    return 1;
}

static int next_line(const char** p, const char* end, const char** ls, const char** le)
{
    const char* s = *p;
    if (s >= end) return 0;
    while (s < end && (*s == '\r' || *s == '\n')) s++;
    if (s >= end) {
        *p = s;
        return 0;
    }
    *ls = s;
    while (s < end && *s != '\r' && *s != '\n') s++;
    *le = s;
    while (s < end && (*s == '\r' || *s == '\n')) s++;
    *p = s;
    return 1;
}

static void copy_word(char* out, uint32_t cap, const char* p, const char* end)
{
    uint32_t n = 0;
    if (!out || cap == 0) return;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (p < end && *p != ' ' && *p != '\t' && n + 1u < cap) out[n++] = *p++;
    out[n] = '\0';
}

static int parse_hex16_word(const char** p, const char* end, uint16_t* out)
{
    uint32_t v = 0;
    uint32_t digits = 0;
    const char* s = *p;
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (s < end) {
        char c = *s;
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');
        else break;
        v = (v << 4) | d;
        digits++;
        s++;
        if (v > 0xFFFFu) return -1;
    }
    if (digits == 0) return -2;
    *out = (uint16_t)v;
    *p = s;
    return 0;
}

static int type_from_word(const char* p, const char* end, uint8_t* out)
{
    if (line_eq(p, end, "net")) { *out = DRFL_TYPE_NET; return 0; }
    if (line_eq(p, end, "block")) { *out = DRFL_TYPE_BLOCK; return 0; }
    return -1;
}

static int append_code(drfl_entry_t* e, const char* p, const char* end)
{
    uint32_t n = (uint32_t)(end > p ? end - p : 0);
    if (e->code_len + n + 1u >= DRFL_MAX_CODE) return -1;
    for (uint32_t i = 0; i < n; i++) e->code[e->code_len++] = p[i];
    e->code[e->code_len++] = '\n';
    e->code[e->code_len] = '\0';
    return 0;
}

static int drfl_load_text(const char* file_name, const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    drfl_entry_t* e;
    uint32_t saw_pci = 0;
    uint32_t saw_type = 0;
    if (s_entry_count >= DRFL_MAX_ENTRIES) return -10;
    if (!next_line(&p, end, &ls, &le) || !line_eq(ls, le, "DRFL 2")) return -11;

    e = &s_entries[s_entry_count];
    for (uint32_t i = 0; i < sizeof(*e); i++) ((uint8_t*)e)[i] = 0;
    e->format_version = 2;
    (void)file_name;
    copy_word(e->name, sizeof(e->name), "driver", "driver" + 6);
    copy_word(e->lang, sizeof(e->lang), "DRFL-C", "DRFL-C" + 6);

    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_eq(ls, le, "END")) break;
        if (line_prefix(ls, le, "ID", &v) || line_prefix(ls, le, "NAME", &v) ||
            line_prefix(ls, le, "ENTRY", &v) || line_prefix(ls, le, "BIND", &v)) {
            copy_word(e->name, sizeof(e->name), v, le);
        } else if (line_prefix(ls, le, "LANG", &v)) {
            copy_word(e->lang, sizeof(e->lang), v, le);
        } else if (line_prefix(ls, le, "TYPE", &v)) {
            if (type_from_word(v, le, &e->type) != 0) return -12;
            saw_type = 1u;
        } else if (line_prefix(ls, le, "PCI", &v)) {
            if (parse_hex16_word(&v, le, &e->vendor_id) != 0 ||
                parse_hex16_word(&v, le, &e->device_id) != 0) return -13;
            saw_pci = 1u;
        } else if (line_prefix(ls, le, "CODE", &v)) {
            if (append_code(e, v, le) != 0) return -14;
        }
    }
    if (!e->name[0] || !saw_pci || !saw_type || e->code_len == 0) return -15;
    e->name_len = 0;
    while (e->name[e->name_len] && e->name_len < DRFL_MAX_NAME - 1) e->name_len++;
    e->code_hash = hash_bytes(e->code, e->code_len);
    s_entry_count++;
    return 0;
}

static int drfl_load_binary_v1(const uint8_t* d, uint32_t size)
{
    uint32_t mag = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (mag != DRFL_MAGIC) return -2;
    if (d[4] != 1) return -3;

    uint16_t entry_count = (uint16_t)d[8] | ((uint16_t)d[9] << 8);
    if (entry_count > DRFL_MAX_ENTRIES) entry_count = DRFL_MAX_ENTRIES;
    if (s_entry_count + entry_count > DRFL_MAX_ENTRIES) entry_count = (uint16_t)(DRFL_MAX_ENTRIES - s_entry_count);

    uint32_t off = 10;
    for (uint16_t i = 0; i < entry_count && off + 6 < size; i++) {
        uint16_t vendor = (uint16_t)d[off] | ((uint16_t)d[off + 1] << 8);
        uint16_t device = (uint16_t)d[off + 2] | ((uint16_t)d[off + 3] << 8);
        uint8_t type = d[off + 4];
        uint8_t name_len = d[off + 5];
        off += 6;
        if (name_len > DRFL_MAX_NAME - 1) name_len = DRFL_MAX_NAME - 1;
        if (off + name_len > size) break;

        drfl_entry_t* e = &s_entries[s_entry_count++];
        for (uint32_t z = 0; z < sizeof(*e); z++) ((uint8_t*)e)[z] = 0;
        e->vendor_id = vendor;
        e->device_id = device;
        e->type = type;
        e->name_len = name_len;
        e->format_version = 1;
        for (uint8_t j = 0; j < name_len; j++) e->name[j] = (char)d[off + j];
        e->name[name_len] = '\0';
        off += name_len;
    }
    return 0;
}

int drfl_load(const char* name)
{
    const FsFile* f = fs_open(name);
    if (!f || f->size < 6) return -1;
    if (f->size >= 7 && line_eq((const char*)f->data, (const char*)f->data + 6, "DRFL 2")) {
        return drfl_load_text(name, f->data, f->size);
    }
    if (f->size < 12) return -1;
    return drfl_load_binary_v1(f->data, f->size);
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

uint32_t drfl_list(drfl_list_cb cb, void* user)
{
    if (cb) {
        for (uint16_t i = 0; i < s_entry_count; i++) {
            drfl_entry_t* e = &s_entries[i];
            cb(e->vendor_id, e->device_id, e->type, e->name, user);
        }
    }
    return s_entry_count;
}

int drfl_info(uint32_t index, drfl_info_t* out)
{
    drfl_entry_t* e;
    if (!out || index >= s_entry_count) return -1;
    e = &s_entries[index];
    out->vendor_id = e->vendor_id;
    out->device_id = e->device_id;
    out->type = e->type;
    out->format_version = e->format_version;
    for (uint32_t i = 0; i < DRFL_MAX_NAME; i++) out->name[i] = e->name[i];
    for (uint32_t i = 0; i < DRFL_MAX_LANG; i++) out->lang[i] = e->lang[i];
    out->code_len = e->code_len;
    out->code_hash = e->code_hash;
    out->code = e->code;
    return 0;
}

int drfl_probe_net(void* nic_ctx, const char* driver_name, drfl_net_init_fn init_fn)
{
    pci_addr_t dev;

    /* Try DRFL entries first */
    for (uint16_t i = 0; i < s_entry_count; i++) {
        drfl_entry_t* e = &s_entries[i];
        if (e->type != DRFL_TYPE_NET) continue;
        if (driver_name && !cstr_eq(e->name, driver_name)) continue;
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

int drfl_probe_block(void* block_ctx, const char* driver_name, drfl_block_init_fn init_fn)
{
    pci_addr_t dev;

    for (uint16_t i = 0; i < s_entry_count; i++) {
        drfl_entry_t* e = &s_entries[i];
        if (e->type != DRFL_TYPE_BLOCK) continue;
        if (driver_name && !cstr_eq(e->name, driver_name)) continue;
        if (pci_find_device(e->vendor_id, e->device_id, &dev) == 0) {
            if (init_fn(block_ctx) == 0) return 0;
        }
    }

    if (pci_find_device(0x8086, 0x7010, &dev) == 0) {
        return init_fn(block_ctx);
    }
    return -1;
}
