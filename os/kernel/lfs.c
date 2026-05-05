/*
 * LFS - Lard File System implementation.
 */
#include "lfs.h"
#include <stddef.h>
#include <stdint.h>

static const uint8_t* s_lfs_image;
static uint32_t s_lfs_size;

int lfs_mount(const uint8_t* image, uint32_t size)
{
    if (!image || size < 10) return -1;
    uint32_t mag = (uint32_t)image[0] | ((uint32_t)image[1] << 8) |
                   ((uint32_t)image[2] << 16) | ((uint32_t)image[3] << 24);
    if (mag != LFS_MAGIC) return -2;
    if (image[4] != 1) return -3;

    s_lfs_image = image;
    s_lfs_size = size;
    return 0;
}

int lfs_lookup(const char* name, const uint8_t** out_data, uint32_t* out_size)
{
    if (!s_lfs_image || !name || !out_data || !out_size) return 0;

    uint16_t file_count = (uint16_t)s_lfs_image[8] | ((uint16_t)s_lfs_image[9] << 8);
    uint32_t off = 10;

    for (uint16_t i = 0; i < file_count && off + 2 < s_lfs_size; i++) {
        uint8_t nlen = s_lfs_image[off++];
        if (off + nlen + 8 > s_lfs_size) break;

        const char* ename = (const char*)&s_lfs_image[off];
        off += nlen;

        uint32_t data_off = (uint32_t)s_lfs_image[off] | ((uint32_t)s_lfs_image[off + 1] << 8) |
                            ((uint32_t)s_lfs_image[off + 2] << 16) | ((uint32_t)s_lfs_image[off + 3] << 24);
        off += 4;
        uint32_t data_sz = (uint32_t)s_lfs_image[off] | ((uint32_t)s_lfs_image[off + 1] << 8) |
                           ((uint32_t)s_lfs_image[off + 2] << 16) | ((uint32_t)s_lfs_image[off + 3] << 24);
        off += 4;

        /* Compare: name is nlen chars, we need to match with null-term */
        int match = 1;
        for (uint8_t j = 0; j < nlen; j++) {
            if (name[j] != ename[j]) { match = 0; break; }
        }
        if (match && name[nlen] == '\0') {
            if (data_off + data_sz > s_lfs_size) return 0;
            *out_data = s_lfs_image + data_off;
            *out_size = data_sz;
            return 1;
        }
    }
    return 0;
}

void lfs_list(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    if (!s_lfs_image || !cb) return;

    uint16_t file_count = (uint16_t)s_lfs_image[8] | ((uint16_t)s_lfs_image[9] << 8);
    uint32_t off = 10;
    static char name_buf[LFS_MAX_NAME];

    for (uint16_t i = 0; i < file_count && off + 2 < s_lfs_size; i++) {
        uint8_t nlen = s_lfs_image[off++];
        if (nlen >= LFS_MAX_NAME) nlen = LFS_MAX_NAME - 1;
        if (off + nlen + 8 > s_lfs_size) break;

        for (uint8_t j = 0; j < nlen; j++) name_buf[j] = (char)s_lfs_image[off + j];
        name_buf[nlen] = '\0';
        off += nlen;

        off += 4; /* skip offset */
        uint32_t data_sz = (uint32_t)s_lfs_image[off] | ((uint32_t)s_lfs_image[off + 1] << 8) |
                           ((uint32_t)s_lfs_image[off + 2] << 16) | ((uint32_t)s_lfs_image[off + 3] << 24);
        off += 4;

        cb(name_buf, data_sz, user);
    }
}
