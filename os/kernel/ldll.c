/*
 * LDLL - LardOS Dynamic Link Library loader.
 * Loads .ldll files from FS into user space.
 */
#include "ldll.h"
#include "fs.h"
#include "mmu.h"
#include "rxr.h"
#include "usermode.h"
#include <stddef.h>
#include <stdint.h>

#define LDLL_MAX_HANDLES  4
#define LDLL_MAX_NAME     64

static struct {
    int in_use;
    uintptr_t base;
    const FsFile* file;
} ldll_handles[LDLL_MAX_HANDLES];

static int is_user_ptr(uintptr_t p, uint32_t max_len)
{
    if (p < USER_VALID_LO || p >= USER_VALID_HI) return 0;
    if (p + max_len < p || p + max_len >= USER_VALID_HI) return 0;
    return 1;
}

static int copy_user_string(const char* user_src, char* dst, uint32_t cap)
{
    for (uint32_t i = 0; i < cap - 1; i++) {
        char c = *(char*)(uintptr_t)(user_src + i);
        dst[i] = c;
        if (c == '\0') return (int)i;
    }
    dst[cap - 1] = '\0';
    return -1;
}

static int find_free_handle(void)
{
    for (int i = 0; i < LDLL_MAX_HANDLES; i++) {
        if (!ldll_handles[i].in_use) return i;
    }
    return -1;
}

int ldll_load(const char* name)
{
    if (!is_user_ptr((uintptr_t)name, LDLL_MAX_NAME)) return -1;
    char kern_name[LDLL_MAX_NAME];
    if (copy_user_string(name, kern_name, LDLL_MAX_NAME) < 0) return -1;
    {
        char resolved[LDLL_MAX_NAME];
        if (rxr_resolve_path(kern_name, resolved, sizeof(resolved)) >= 0) {
            for (uint32_t i = 0; i < sizeof(kern_name); i++) {
                kern_name[i] = resolved[i];
                if (resolved[i] == '\0') break;
            }
            kern_name[sizeof(kern_name) - 1u] = '\0';
        }
    }

    const FsFile* f = fs_open(kern_name);
    if (!f || f->size < 16) return -1;

    const uint8_t* d = f->data;
    uint32_t mag = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (mag != LDLL_MAGIC) return -1;

    uint32_t code_sz = (uint32_t)d[8] | ((uint32_t)d[9] << 8) | ((uint32_t)d[10] << 16) | ((uint32_t)d[11] << 24);
    uint32_t data_sz = (uint32_t)d[12] | ((uint32_t)d[13] << 8) | ((uint32_t)d[14] << 16) | ((uint32_t)d[15] << 24);

    if (code_sz > 4096 || data_sz > 4096) return -1;
    if (16 + code_sz + data_sz + 2 > f->size) return -1;

    int h = find_free_handle();
    if (h < 0) return -1;

    uintptr_t base = USER_LDLL_BASE;
    mmu_map_user_ldll(base);

    uint8_t* dest = (uint8_t*)(uintptr_t)base;
    for (uint32_t i = 0; i < code_sz; i++) dest[i] = d[16 + i];
    for (uint32_t i = 0; i < data_sz; i++) dest[code_sz + i] = d[16 + code_sz + i];

    ldll_handles[h].in_use = 1;
    ldll_handles[h].base = base;
    ldll_handles[h].file = f;
    return h + 1;
}

void* ldll_sym(int handle, const char* name)
{
    if (handle < 1 || handle > LDLL_MAX_HANDLES) return 0;
    if (!is_user_ptr((uintptr_t)name, LDLL_MAX_NAME)) return 0;
    int h = handle - 1;
    if (!ldll_handles[h].in_use) return 0;

    char kern_name[LDLL_MAX_NAME];
    if (copy_user_string(name, kern_name, LDLL_MAX_NAME) < 0) return 0;

    const FsFile* f = ldll_handles[h].file;
    if (!f) return 0;
    const uint8_t* d = f->data;
    if (f->size < 18) return 0;

    uint32_t code_sz = (uint32_t)d[8] | ((uint32_t)d[9] << 8) | ((uint32_t)d[10] << 16) | ((uint32_t)d[11] << 24);
    uint32_t data_sz = (uint32_t)d[12] | ((uint32_t)d[13] << 8) | ((uint32_t)d[14] << 16) | ((uint32_t)d[15] << 24);
    uint32_t off = 16 + code_sz + data_sz;
    uint16_t exp_count = (uint16_t)d[off] | ((uint16_t)d[off + 1] << 8);
    off += 2;

    for (uint16_t i = 0; i < exp_count && off < f->size; i++) {
        uint8_t nlen = d[off++];
        if (off + nlen + 4 > f->size) break;
        int match = 1;
        for (uint8_t j = 0; j < nlen; j++) {
            if (kern_name[j] != d[off + j]) { match = 0; break; }
            if (kern_name[j] == '\0') break;
        }
        if (match && kern_name[nlen] == '\0') {
            uint32_t sym_off = (uint32_t)d[off + nlen] | ((uint32_t)d[off + nlen + 1] << 8) |
                               ((uint32_t)d[off + nlen + 2] << 16) | ((uint32_t)d[off + nlen + 3] << 24);
            return (void*)(ldll_handles[h].base + sym_off);
        }
        off += nlen + 4;
    }
    return 0;
}

void ldll_close(int handle)
{
    if (handle >= 1 && handle <= LDLL_MAX_HANDLES) {
        ldll_handles[handle - 1].in_use = 0;
    }
}
