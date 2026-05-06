#include "lar.h"

#include <stddef.h>

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int name_eq(const uint8_t* entry_name, uint8_t entry_len, const char* name)
{
    uint8_t i = 0;
    if (!name) return 0;
    while (i < entry_len && name[i]) {
        if ((char)entry_name[i] != name[i]) return 0;
        i++;
    }
    return i == entry_len && name[i] == '\0';
}

static int validate_header(const uint8_t* data, uint32_t len, uint16_t* count, uint32_t* dir_end)
{
    if (!data || len < 8) return -1;
    if (rd32(data) != LAR_MAGIC) return -2;
    *count = rd16(data + 4);
    uint16_t dir_size = rd16(data + 6);
    if ((uint32_t)8 + dir_size > len) return -3;
    *dir_end = 8u + dir_size;
    return 0;
}

int lar_list(const uint8_t* data, uint32_t len, lar_list_cb cb, void* user)
{
    uint16_t count;
    uint32_t dir_end;
    int r = validate_header(data, len, &count, &dir_end);
    if (r != 0) return r;

    uint32_t p = 8;
    for (uint16_t i = 0; i < count; i++) {
        if (p + 16 > dir_end) return -4;
        uint8_t name_len = data[p];
        uint8_t method = data[p + 1];
        uint32_t offset = rd32(data + p + 4);
        uint32_t packed_size = rd32(data + p + 8);
        uint32_t unpacked_size = rd32(data + p + 12);
        p += 16;
        if (name_len == 0 || p + name_len > dir_end) return -5;
        if (offset > len || packed_size > len - offset) return -6;
        if (cb) {
            lar_entry_t entry;
            entry.name = (const char*)(data + p);
            entry.name_len = name_len;
            entry.method = method;
            entry.packed_size = packed_size;
            entry.unpacked_size = unpacked_size;
            cb(&entry, user);
        }
        p += name_len;
    }
    return p == dir_end ? 0 : -7;
}

int lar_extract(const uint8_t* data, uint32_t len, const char* name, uint8_t* out, uint32_t* out_len)
{
    uint16_t count;
    uint32_t dir_end;
    int r = validate_header(data, len, &count, &dir_end);
    if (r != 0) return r;
    if (!out || !out_len) return -8;

    uint32_t p = 8;
    for (uint16_t i = 0; i < count; i++) {
        if (p + 16 > dir_end) return -4;
        uint8_t name_len = data[p];
        uint8_t method = data[p + 1];
        uint32_t offset = rd32(data + p + 4);
        uint32_t packed_size = rd32(data + p + 8);
        uint32_t unpacked_size = rd32(data + p + 12);
        p += 16;
        if (name_len == 0 || p + name_len > dir_end) return -5;
        if (offset > len || packed_size > len - offset) return -6;
        if (name_eq(data + p, name_len, name)) {
            if (method != LAR_METHOD_STORE || packed_size != unpacked_size) return -9;
            if (*out_len < unpacked_size) return -10;
            for (uint32_t j = 0; j < unpacked_size; j++) {
                out[j] = data[offset + j];
            }
            *out_len = unpacked_size;
            return 0;
        }
        p += name_len;
    }
    return -11;
}
