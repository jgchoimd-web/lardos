#include "lar.h"
#include "hash.h"

#include <stddef.h>

#define LAR_PASS_MAGIC0 'L'
#define LAR_PASS_MAGIC1 'P'
#define LAR_PASS_MAGIC2 '1'
#define LAR_PASS_OVERHEAD 16u

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t st_len(const char* s)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static uint32_t pass_mix_str(uint32_t h, const char* s)
{
    while (s && *s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
        h ^= h >> 13;
    }
    return h;
}

static uint32_t pass_mix_name(uint32_t h, const uint8_t* name, uint8_t name_len)
{
    for (uint8_t i = 0; i < name_len; i++) {
        h ^= name[i];
        h *= 16777619u;
        h ^= h >> 11;
    }
    return h;
}

static void pass_key(const char* password, const uint8_t* name, uint8_t name_len,
                     uint32_t salt, uint32_t key[4])
{
    uint32_t base = 2166136261u ^ salt;
    base = pass_mix_str(base, password);
    base = pass_mix_name(base, name, name_len);
    key[0] = base ^ 0x4C415231u;
    key[1] = (base << 7) ^ (base >> 3) ^ 0x50415353u ^ salt;
    key[2] = (base << 13) ^ (base >> 5) ^ 0x55534552u ^ (salt << 1);
    key[3] = (base << 17) ^ (base >> 7) ^ 0x4F574E44u ^ (salt >> 1);
}

static void xtea_encipher(uint32_t v[2], const uint32_t key[4])
{
    uint32_t v0 = v[0];
    uint32_t v1 = v[1];
    uint32_t sum = 0;
    const uint32_t delta = 0x9E3779B9u;
    for (uint32_t i = 0; i < 32u; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3u]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3u]);
    }
    v[0] = v0;
    v[1] = v1;
}

static uint8_t pass_stream_byte(const uint32_t key[4], uint32_t salt, uint32_t index)
{
    uint32_t block = index >> 3;
    uint32_t shift = (index & 3u) * 8u;
    uint32_t v[2];
    uint32_t word;
    v[0] = salt ^ block ^ 0x4C415250u;
    v[1] = key[0] ^ key[2] ^ (block * 0x9E3779B9u);
    xtea_encipher(v, key);
    word = (index & 4u) ? v[1] : v[0];
    return (uint8_t)((word >> shift) & 0xFFu);
}

static int lar_valid_member_name(const char* member, uint8_t* out_len)
{
    uint32_t n = st_len(member);
    if (n == 0 || n > 63u) return 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = member[i];
        if (c == '/' || c == '\\' || c == ':' || c < 33 || c > 126) return 0;
    }
    if (out_len) *out_len = (uint8_t)n;
    return 1;
}

static int lar_contains_bytes(const uint8_t* data, uint32_t len,
                              const uint8_t* needle, uint32_t needle_len)
{
    if (!data || !needle || needle_len == 0 || needle_len > len) return 0;
    for (uint32_t i = 0; i <= len - needle_len; i++) {
        uint32_t j = 0;
        while (j < needle_len && data[i + j] == needle[j]) j++;
        if (j == needle_len) return 1;
    }
    return 0;
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

int lar_extract_password(const uint8_t* data, uint32_t len, const char* name,
                         const char* password, uint8_t* out, uint32_t* out_len)
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
            if (method == LAR_METHOD_STORE) {
                if (packed_size != unpacked_size) return -9;
                if (*out_len < unpacked_size) return -10;
                for (uint32_t j = 0; j < unpacked_size; j++) {
                    out[j] = data[offset + j];
                }
                *out_len = unpacked_size;
                return 0;
            }
            if (method == LAR_METHOD_PASS_STORE) {
                const uint8_t* payload = data + offset;
                uint32_t salt;
                uint32_t want_crc;
                uint32_t got_crc;
                uint32_t key[4];
                if (!password || !password[0]) return -12;
                if (packed_size < LAR_PASS_OVERHEAD ||
                    packed_size - LAR_PASS_OVERHEAD != unpacked_size) return -9;
                if (payload[0] != LAR_PASS_MAGIC0 || payload[1] != LAR_PASS_MAGIC1 ||
                    payload[2] != LAR_PASS_MAGIC2 || payload[3] != 0) return -9;
                if (*out_len < unpacked_size) return -10;
                salt = rd32(payload + 4);
                want_crc = rd32(payload + 8);
                pass_key(password, data + p, name_len, salt, key);
                for (uint32_t j = 0; j < unpacked_size; j++) {
                    out[j] = payload[LAR_PASS_OVERHEAD + j] ^ pass_stream_byte(key, salt, j);
                }
                got_crc = hash_crc32(out, unpacked_size);
                if (got_crc != want_crc) return -13;
                *out_len = unpacked_size;
                return 0;
            }
            return -9;
        }
        p += name_len;
    }
    return -11;
}

int lar_extract(const uint8_t* data, uint32_t len, const char* name, uint8_t* out, uint32_t* out_len)
{
    return lar_extract_password(data, len, name, NULL, out, out_len);
}

int lar_create_single(uint8_t* out, uint32_t* out_len, const char* member,
                      const uint8_t* payload, uint32_t payload_len,
                      const char* password)
{
    uint8_t name_len;
    uint32_t dir_size;
    uint32_t data_off;
    uint32_t packed_size;
    uint32_t total;
    uint8_t method;
    uint32_t p;
    uint32_t crc;
    uint32_t salt;
    uint32_t key[4];

    if (!out || !out_len || !payload) return -1;
    if (!lar_valid_member_name(member, &name_len)) return -2;
    method = (password && password[0]) ? LAR_METHOD_PASS_STORE : LAR_METHOD_STORE;
    packed_size = payload_len + (method == LAR_METHOD_PASS_STORE ? LAR_PASS_OVERHEAD : 0u);
    dir_size = 16u + name_len;
    data_off = 8u + dir_size;
    total = data_off + packed_size;
    if (dir_size > 0xFFFFu || total > *out_len || packed_size < payload_len) return -3;

    wr32(out, LAR_MAGIC);
    wr16(out + 4, 1u);
    wr16(out + 6, dir_size);
    p = 8u;
    out[p++] = name_len;
    out[p++] = method;
    out[p++] = 0;
    out[p++] = 0;
    wr32(out + p, data_off); p += 4u;
    wr32(out + p, packed_size); p += 4u;
    wr32(out + p, payload_len); p += 4u;
    for (uint32_t i = 0; i < name_len; i++) out[p++] = (uint8_t)member[i];

    if (method == LAR_METHOD_STORE) {
        for (uint32_t i = 0; i < payload_len; i++) out[data_off + i] = payload[i];
    } else {
        crc = hash_crc32(payload, payload_len);
        salt = crc ^ hash_fnv1a_str(password) ^ hash_fnv1a((const uint8_t*)member, name_len) ^
               payload_len ^ 0x4C415250u;
        out[data_off + 0] = LAR_PASS_MAGIC0;
        out[data_off + 1] = LAR_PASS_MAGIC1;
        out[data_off + 2] = LAR_PASS_MAGIC2;
        out[data_off + 3] = 0;
        wr32(out + data_off + 4, salt);
        wr32(out + data_off + 8, crc);
        wr32(out + data_off + 12, payload_len);
        pass_key(password, (const uint8_t*)member, name_len, salt, key);
        for (uint32_t i = 0; i < payload_len; i++) {
            out[data_off + LAR_PASS_OVERHEAD + i] = payload[i] ^ pass_stream_byte(key, salt, i);
        }
    }
    *out_len = total;
    return 0;
}

int lar_selftest(void)
{
    static const uint8_t msg[] = "secret lar payload";
    uint8_t archive[128];
    uint8_t plain[32];
    uint32_t archive_len = sizeof(archive);
    uint32_t plain_len;
    int r;

    r = lar_create_single(archive, &archive_len, "secret.txt", msg, sizeof(msg) - 1u, "open");
    if (r != 0) return -1;
    if (lar_list(archive, archive_len, NULL, NULL) != 0) return -2;
    if (lar_contains_bytes(archive, archive_len, msg, sizeof(msg) - 1u)) return -7;
    plain_len = sizeof(plain);
    if (lar_extract_password(archive, archive_len, "secret.txt", "bad", plain, &plain_len) != -13) return -3;
    plain_len = sizeof(plain);
    if (lar_extract_password(archive, archive_len, "secret.txt", "open", plain, &plain_len) != 0) return -4;
    if (plain_len != sizeof(msg) - 1u) return -5;
    for (uint32_t i = 0; i < plain_len; i++) {
        if (plain[i] != msg[i]) return -6;
    }
    return 0;
}
