/*
 * LFS - Lard File System implementation.
 */
#include "lfs.h"
#include <stddef.h>
#include <stdint.h>

static const uint8_t* s_lfs_image;
static uint32_t s_lfs_size;
static uint8_t s_lfs_version;

typedef struct {
    uint32_t value;
    uint8_t overflow;
} lfs_varu_t;

static int lfs_rd_varu(const uint8_t* image, uint32_t size, uint32_t* off, lfs_varu_t* out)
{
    uint32_t value = 0;
    uint32_t shift = 0;
    uint8_t overflow = 0;
    if (!image || !off || !out) return 0;
    while (*off < size) {
        uint8_t b = image[(*off)++];
        uint32_t chunk = (uint32_t)(b & 0x7Fu);
        if (!overflow) {
            if (shift >= 32u) {
                if (chunk) overflow = 1;
            } else {
                if (chunk > (0xFFFFFFFFu >> shift)) overflow = 1;
                else value |= chunk << shift;
            }
        }
        if ((b & 0x80u) == 0) {
            out->value = value;
            out->overflow = overflow;
            return 1;
        }
        if (shift < 32u) shift += 7u;
        else overflow = 1;
    }
    return 0;
}

static int lfs_name_match(const char* name, const uint8_t* ename, uint32_t nlen)
{
    uint32_t i;
    if (!name || !ename) return 0;
    for (i = 0; i < nlen; i++) {
        if (!name[i] || name[i] != (char)ename[i]) return 0;
    }
    return name[i] == '\0';
}

static int lfs_range_in_image(uint32_t off, uint32_t size)
{
    return off <= s_lfs_size && size <= s_lfs_size - off;
}

int lfs_mount(const uint8_t* image, uint32_t size)
{
    if (!image || size < 8) return -1;
    uint32_t mag = (uint32_t)image[0] | ((uint32_t)image[1] << 8) |
                   ((uint32_t)image[2] << 16) | ((uint32_t)image[3] << 24);
    if (mag != LFS_MAGIC) return -2;
    if (image[4] != 1 && image[4] != 2) return -3;
    if (image[4] == 1 && size < 10) return -1;

    s_lfs_image = image;
    s_lfs_size = size;
    s_lfs_version = image[4];
    return 0;
}

static int lfs_lookup_v1(const char* name, const uint8_t** out_data, uint32_t* out_size)
{
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
            if (!lfs_range_in_image(data_off, data_sz)) return 0;
            *out_data = s_lfs_image + data_off;
            *out_size = data_sz;
            return 1;
        }
    }
    return 0;
}

static int lfs_lookup_v2(const char* name, const uint8_t** out_data, uint32_t* out_size)
{
    uint32_t off = 8;
    while (off < s_lfs_size) {
        lfs_varu_t tag;
        lfs_varu_t nlen;
        lfs_varu_t extent_count;
        const uint8_t* ename;
        int match;
        if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &tag) || tag.overflow) return 0;
        if (tag.value == 0) return 0;
        if (tag.value != 1) return 0;
        if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &nlen) || nlen.overflow) return 0;
        if (nlen.value > s_lfs_size - off) return 0;
        ename = s_lfs_image + off;
        match = lfs_name_match(name, ename, nlen.value);
        off += nlen.value;
        if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &extent_count) || extent_count.overflow) return 0;
        for (uint32_t i = 0; i < extent_count.value; i++) {
            lfs_varu_t data_off;
            lfs_varu_t data_sz;
            if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &data_off)) return 0;
            if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &data_sz)) return 0;
            if (match && extent_count.value == 1u && !data_off.overflow && !data_sz.overflow &&
                lfs_range_in_image(data_off.value, data_sz.value)) {
                *out_data = s_lfs_image + data_off.value;
                *out_size = data_sz.value;
                return 1;
            }
        }
        if (match) return 0;
    }
    return 0;
}

int lfs_lookup(const char* name, const uint8_t** out_data, uint32_t* out_size)
{
    if (!s_lfs_image || !name || !out_data || !out_size) return 0;
    if (s_lfs_version == 2) return lfs_lookup_v2(name, out_data, out_size);
    return lfs_lookup_v1(name, out_data, out_size);
}

static void lfs_list_v1(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
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

static void lfs_list_v2(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    uint32_t off = 8;
    static char name_buf[LFS_MAX_NAME];
    while (off < s_lfs_size) {
        lfs_varu_t tag;
        lfs_varu_t nlen;
        lfs_varu_t extent_count;
        uint32_t name_len;
        uint32_t shown_size = 0;
        uint8_t unbounded = 0;
        if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &tag) || tag.overflow) return;
        if (tag.value == 0) return;
        if (tag.value != 1) return;
        if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &nlen) || nlen.overflow) return;
        if (nlen.value > s_lfs_size - off) return;
        name_len = nlen.value;
        if (name_len >= LFS_MAX_NAME) name_len = LFS_MAX_NAME - 1;
        for (uint32_t j = 0; j < name_len; j++) name_buf[j] = (char)s_lfs_image[off + j];
        name_buf[name_len] = '\0';
        off += nlen.value;
        if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &extent_count) || extent_count.overflow) return;
        for (uint32_t i = 0; i < extent_count.value; i++) {
            lfs_varu_t data_off;
            lfs_varu_t data_sz;
            if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &data_off)) return;
            if (!lfs_rd_varu(s_lfs_image, s_lfs_size, &off, &data_sz)) return;
            (void)data_off;
            if (data_sz.overflow || shown_size > 0xFFFFFFFFu - data_sz.value) unbounded = 1;
            else shown_size += data_sz.value;
        }
        cb(name_buf, unbounded ? LFS_SIZE_UNBOUNDED : shown_size, user);
    }
}

void lfs_list(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    if (!s_lfs_image || !cb) return;
    if (s_lfs_version == 2) lfs_list_v2(cb, user);
    else lfs_list_v1(cb, user);
}

static void enc_varu(uint8_t* out, uint32_t* pos, uint32_t v)
{
    do {
        uint8_t b = (uint8_t)(v & 0x7Fu);
        v >>= 7;
        if (v) b |= 0x80u;
        out[(*pos)++] = b;
    } while (v);
}

static void enc_huge_varu(uint8_t* out, uint32_t* pos)
{
    for (uint32_t i = 0; i < 11u; i++) out[(*pos)++] = 0x80u;
    out[(*pos)++] = 0x01u;
}

int lfs_selftest(void)
{
    static const uint8_t text[] = "LFS2!";
    const uint8_t* old_image = s_lfs_image;
    uint32_t old_size = s_lfs_size;
    uint8_t old_version = s_lfs_version;
    uint8_t image[96];
    uint32_t p = 0;
    uint32_t data_off;
    const uint8_t* got;
    uint32_t got_size;
    int ok;

    image[p++] = 'L'; image[p++] = 'F'; image[p++] = 'S'; image[p++] = 0;
    image[p++] = 2; image[p++] = 0; image[p++] = 0; image[p++] = 0;
    enc_varu(image, &p, 1); enc_varu(image, &p, 8);
    image[p++] = 'l'; image[p++] = 'f'; image[p++] = 's'; image[p++] = '2';
    image[p++] = '.'; image[p++] = 't'; image[p++] = 'x'; image[p++] = 't';
    enc_varu(image, &p, 1);
    data_off = p + 38u;
    enc_varu(image, &p, data_off);
    enc_varu(image, &p, sizeof(text) - 1u);
    enc_varu(image, &p, 1); enc_varu(image, &p, 8);
    image[p++] = 'h'; image[p++] = 'u'; image[p++] = 'g'; image[p++] = 'e';
    image[p++] = '.'; image[p++] = 'b'; image[p++] = 'i'; image[p++] = 'n';
    enc_varu(image, &p, 1);
    enc_huge_varu(image, &p);
    enc_huge_varu(image, &p);
    enc_varu(image, &p, 0);
    while (p < data_off) image[p++] = 0;
    for (uint32_t i = 0; i < sizeof(text) - 1u; i++) image[p++] = text[i];

    ok = lfs_mount(image, p) == 0 &&
         lfs_lookup("lfs2.txt", &got, &got_size) &&
         got_size == sizeof(text) - 1u &&
         got[0] == 'L' && got[1] == 'F' && got[2] == 'S' && got[3] == '2' && got[4] == '!' &&
         !lfs_lookup("huge.bin", &got, &got_size);

    s_lfs_image = old_image;
    s_lfs_size = old_size;
    s_lfs_version = old_version;
    return ok ? 0 : -1;
}
