/*
 * LARDX - LardOS executable loader for user-mode programs.
 * Loads LARDX (LARD magic, v2, IMAGE_USER) from FS, maps segments, builds argv.
 */
#include "lardx_load.h"
#include "fs.h"
#include "mmu.h"
#include "usermode.h"
#include "syscall.h"
#include <stdint.h>

#define LARD_MAGIC  0x4452414Cu  /* "LARD" LE */
#define LARDX_VERSION 2
#define IMAGE_USER 1
#define PHDR_SIZE 20
#define MAX_SEGMENTS 16

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int lardx_run(const char* path, int argc, const char** argv)
{
    const FsFile* f = fs_open(path);
    if (!f || f->size < 32) return -1;

    const uint8_t* d = f->data;
    uint32_t mag = rd_u32(d);
    if (mag != LARD_MAGIC) return -2;

    uint16_t version = (uint16_t)d[4] | ((uint16_t)d[5] << 8);
    if (version != LARDX_VERSION) return -3;

    uint8_t image_type = d[6];
    if (image_type != IMAGE_USER) return -4;

    uint16_t seg_count = (uint16_t)d[8] | ((uint16_t)d[9] << 8);
    if (seg_count == 0 || seg_count > MAX_SEGMENTS) return -5;

    uint32_t entry = rd_u32(d + 0x0A);
    uint32_t phoff = rd_u32(d + 0x0E);

    if (phoff + (uint32_t)seg_count * PHDR_SIZE > f->size) return -6;

    uint32_t paddrs[MAX_SEGMENTS];
    uint32_t sizes[MAX_SEGMENTS];
    uint32_t seg_flags[MAX_SEGMENTS];

    for (uint16_t i = 0; i < seg_count; i++) {
        uint32_t off = phoff + i * PHDR_SIZE;
        paddrs[i] = rd_u32(d + off);
        uint32_t file_off = rd_u32(d + off + 4);
        uint32_t file_sz = rd_u32(d + off + 8);
        uint32_t mem_sz = rd_u32(d + off + 12);
        seg_flags[i] = rd_u32(d + off + 16) & 7u;

        if (file_off + file_sz > f->size) return -7;

        uint8_t* dest = (uint8_t*)(uintptr_t)paddrs[i];
        const uint8_t* src = d + file_off;
        for (uint32_t j = 0; j < file_sz && j < mem_sz; j++) dest[j] = src[j];
        for (uint32_t j = file_sz; j < mem_sz; j++) dest[j] = 0;

        sizes[i] = mem_sz;
    }

    mmu_map_user_segments(paddrs, sizes, seg_flags, (int)seg_count);
    syscall_reset_process_state();
    usermode_run_lardx(entry, argc, argv);
    return 0;
}

int lardx_run_sandbox(const char* path, int argc, const char** argv)
{
    uint32_t old_caps = syscall_get_caps();
    syscall_set_sandbox(1);
    int r = lardx_run(path, argc, argv);
    syscall_set_caps(old_caps);
    return r;
}
