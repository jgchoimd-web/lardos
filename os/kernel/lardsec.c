#include "lardsec.h"

#include <stddef.h>
#include <stdint.h>

#define LARDSEC_MAGIC 0x4345534Cu /* "LSEC" */
#define LARDSEC_VERSION 1u
#define LARDSEC_FLAG_ECC 1u
#define LARDSEC_HEADER_BYTES 512u
#define LARDSEC_MAX_BLOCKS 32u

static uint32_t s_enabled;
static uint32_t s_locked;
static uint32_t s_ecc_enabled;
static uint32_t s_key[4];
static uint32_t s_key_hash;
static uint32_t s_sealed_writes;
static uint32_t s_opened_seals;
static uint32_t s_ecc_corrections;
static uint32_t s_ecc_failures;
static uint32_t s_scrubbed_bytes;
static uint32_t s_last_error;
static char s_recovery_key[LARDSEC_KEY_TEXT_MAX + 1u];

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t rotl32(uint32_t v, uint32_t n)
{
    return (v << n) | (v >> (32u - n));
}

static uint32_t mix32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v ? v : 0xA5A55A5Au;
}

static void lardsec_make_key(uint32_t seed)
{
    uint32_t x = seed ^ 0x4C415244u;
    s_key[0] = mix32(x ^ 0x53454331u);
    s_key[1] = mix32(x ^ 0x4C4F434Bu);
    s_key[2] = mix32(x ^ 0x45525243u);
    s_key[3] = mix32(x ^ 0x55534552u);
    s_key_hash = mix32(s_key[0] ^ rotl32(s_key[1], 7) ^ rotl32(s_key[2], 13) ^ rotl32(s_key[3], 19));
}

static char hex_digit(uint32_t v)
{
    v &= 0xFu;
    return (char)(v < 10u ? '0' + v : 'A' + (v - 10u));
}

static void key_group(char* out, uint32_t* pos, uint32_t v)
{
    for (int i = 12; i >= 0; i -= 4) out[(*pos)++] = hex_digit(v >> (uint32_t)i);
}

static void lardsec_update_key_text(void)
{
    uint32_t p = 0;
    s_recovery_key[p++] = 'L';
    s_recovery_key[p++] = 'A';
    s_recovery_key[p++] = 'R';
    s_recovery_key[p++] = 'D';
    s_recovery_key[p++] = '-';
    key_group(s_recovery_key, &p, s_key[0] ^ (s_key_hash >> 16));
    s_recovery_key[p++] = '-';
    key_group(s_recovery_key, &p, s_key[1] ^ s_key_hash);
    s_recovery_key[p++] = '-';
    key_group(s_recovery_key, &p, s_key[2] ^ rotl32(s_key_hash, 5));
    s_recovery_key[p++] = '-';
    key_group(s_recovery_key, &p, s_key[3] ^ rotl32(s_key_hash, 11));
    s_recovery_key[p] = '\0';
}

static char up(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static int key_matches(const char* key)
{
    uint32_t i = 0;
    uint32_t j = 0;
    if (!key) return 0;
    while (key[i] || s_recovery_key[j]) {
        while (key[i] == ' ' || key[i] == '\t') i++;
        if (up(key[i]) != s_recovery_key[j]) return 0;
        if (key[i]) i++;
        if (s_recovery_key[j]) j++;
    }
    return 1;
}

static uint32_t stream_word(char drive, uint32_t base_lba, uint32_t index)
{
    uint32_t v = s_key[index & 3u] ^ ((uint32_t)(uint8_t)drive << 24) ^
                 (base_lba * 0x9E3779B9u) ^ (index * 0x85EBCA6Bu);
    return mix32(v ^ rotl32(s_key[(index + 1u) & 3u], (index & 15u) + 1u));
}

static void crypt_payload(char drive, uint32_t base_lba,
                          const uint8_t* in, uint8_t* out, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        uint32_t w = stream_word(drive, base_lba, i >> 2);
        out[i] = (uint8_t)(in[i] ^ (uint8_t)(w >> ((i & 3u) * 8u)));
    }
}

static uint32_t ecc_block(const uint8_t* data, uint32_t size)
{
    uint32_t syndrome = 0;
    uint32_t parity = 0;
    for (uint32_t byte = 0; byte < size; byte++) {
        uint8_t v = data[byte];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            if (v & (uint8_t)(1u << bit)) {
                uint32_t pos = byte * 8u + bit + 1u;
                syndrome ^= pos;
                parity ^= 1u;
            }
        }
    }
    return (parity << 16) | (syndrome & 0x1FFFu);
}

static int ecc_fix_block(uint8_t* data, uint32_t size, uint32_t expected)
{
    uint32_t got = ecc_block(data, size);
    uint32_t syndrome = (expected ^ got) & 0x1FFFu;
    uint32_t parity = ((expected ^ got) >> 16) & 1u;
    if (got == expected) return 0;
    if (parity && syndrome >= 1u && syndrome <= size * 8u) {
        uint32_t bitpos = syndrome - 1u;
        data[bitpos >> 3] ^= (uint8_t)(1u << (bitpos & 7u));
        return ecc_block(data, size) == expected ? 1 : -1;
    }
    return -1;
}

static uint32_t image_hash(const uint8_t* data, uint32_t size)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static void fill_scrub(char drive, uint32_t base_lba, uint8_t* p, uint32_t size, uint32_t start_index)
{
    for (uint32_t i = 0; i < size; i++) {
        uint32_t w = stream_word(drive, base_lba ^ 0x5ECu, start_index + (i >> 2));
        p[i] = (uint8_t)(w >> ((i & 3u) * 8u));
    }
    s_scrubbed_bytes += size;
}

void lardsec_init(void)
{
    s_enabled = 1;
    s_locked = 0;
    s_ecc_enabled = 1;
    s_sealed_writes = 0;
    s_opened_seals = 0;
    s_ecc_corrections = 0;
    s_ecc_failures = 0;
    s_scrubbed_bytes = 0;
    s_last_error = 0;
    lardsec_make_key(0x105EA5EDu);
    lardsec_update_key_text();
}

int lardsec_enable(int on)
{
    s_enabled = on ? 1u : 0u;
    if (!s_enabled) s_locked = 0;
    s_last_error = 0;
    return 0;
}

int lardsec_set_ecc(int on)
{
    s_ecc_enabled = on ? 1u : 0u;
    s_last_error = 0;
    return 0;
}

int lardsec_regen_key(uint32_t seed)
{
    if (seed == 0) seed = 0x105EA5EDu;
    lardsec_make_key(seed);
    lardsec_update_key_text();
    s_locked = 0;
    s_last_error = 0;
    return 0;
}

int lardsec_lock(void)
{
    s_enabled = 1;
    s_locked = 1;
    s_last_error = 0;
    return 0;
}

int lardsec_unlock(const char* recovery_key)
{
    if (!key_matches(recovery_key)) {
        s_last_error = 1;
        return -1;
    }
    s_locked = 0;
    s_last_error = 0;
    return 0;
}

int lardsec_locked(void)
{
    return s_locked ? 1 : 0;
}

int lardsec_is_sealed_image(const uint8_t* image, uint32_t image_size)
{
    return image && image_size >= LARDSEC_HEADER_BYTES && rd32(image) == LARDSEC_MAGIC;
}

int lardsec_seal_media_image(char drive, uint32_t base_lba,
                             const uint8_t* plain, uint8_t* sealed,
                             uint32_t image_size, uint32_t used_size)
{
    uint32_t blocks;
    if (!s_enabled) return 1;
    if (!plain || !sealed || image_size < LARDSEC_HEADER_BYTES || used_size + LARDSEC_HEADER_BYTES > image_size) {
        s_last_error = 2;
        return -1;
    }
    for (uint32_t i = 0; i < image_size; i++) sealed[i] = 0;
    wr32(sealed + 0, LARDSEC_MAGIC);
    wr32(sealed + 4, LARDSEC_VERSION);
    wr32(sealed + 8, s_ecc_enabled ? LARDSEC_FLAG_ECC : 0u);
    wr32(sealed + 12, (uint32_t)(uint8_t)drive);
    wr32(sealed + 16, base_lba);
    wr32(sealed + 20, used_size);
    wr32(sealed + 24, s_key_hash);
    wr32(sealed + 28, image_hash(plain, used_size));
    blocks = (used_size + 511u) / 512u;
    if (blocks > LARDSEC_MAX_BLOCKS) {
        s_last_error = 3;
        return -1;
    }
    wr32(sealed + 32, blocks);
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t off = i * 512u;
        uint32_t n = used_size - off;
        if (n > 512u) n = 512u;
        wr32(sealed + 64u + i * 4u, ecc_block(plain + off, n));
    }
    crypt_payload(drive, base_lba, plain, sealed + LARDSEC_HEADER_BYTES, used_size);
    if (LARDSEC_HEADER_BYTES + used_size < image_size) {
        fill_scrub(drive, base_lba, sealed + LARDSEC_HEADER_BYTES + used_size,
                   image_size - LARDSEC_HEADER_BYTES - used_size, used_size >> 2);
    }
    s_sealed_writes++;
    s_last_error = 0;
    return 0;
}

int lardsec_open_media_image(char drive, uint32_t base_lba,
                             const uint8_t* sealed, uint8_t* plain,
                             uint32_t image_size, uint32_t used_size)
{
    uint32_t flags;
    uint32_t stored_used;
    uint32_t blocks;
    if (!sealed || !plain || image_size < LARDSEC_HEADER_BYTES) {
        s_last_error = 4;
        return -1;
    }
    if (!lardsec_is_sealed_image(sealed, image_size)) return 1;
    if (s_locked) {
        s_last_error = 5;
        return -1;
    }
    if (rd32(sealed + 4) != LARDSEC_VERSION || rd32(sealed + 24) != s_key_hash) {
        s_last_error = 6;
        return -1;
    }
    if ((char)(uint8_t)rd32(sealed + 12) != drive || rd32(sealed + 16) != base_lba) {
        s_last_error = 7;
        return -1;
    }
    stored_used = rd32(sealed + 20);
    if (stored_used > used_size || stored_used + LARDSEC_HEADER_BYTES > image_size) {
        s_last_error = 8;
        return -1;
    }
    for (uint32_t i = 0; i < image_size; i++) plain[i] = 0;
    crypt_payload(drive, base_lba, sealed + LARDSEC_HEADER_BYTES, plain, stored_used);
    flags = rd32(sealed + 8);
    blocks = rd32(sealed + 32);
    if ((flags & LARDSEC_FLAG_ECC) && blocks <= LARDSEC_MAX_BLOCKS) {
        for (uint32_t i = 0; i < blocks; i++) {
            uint32_t off = i * 512u;
            uint32_t n = stored_used - off;
            int fix;
            if (off >= stored_used) break;
            if (n > 512u) n = 512u;
            fix = ecc_fix_block(plain + off, n, rd32(sealed + 64u + i * 4u));
            if (fix > 0) s_ecc_corrections++;
            if (fix < 0) {
                s_ecc_failures++;
                s_last_error = 9;
                return -1;
            }
        }
    }
    if (image_hash(plain, stored_used) != rd32(sealed + 28)) {
        s_ecc_failures++;
        s_last_error = 10;
        return -1;
    }
    s_opened_seals++;
    s_last_error = 0;
    return 0;
}

void lardsec_info(lardsec_info_t* out)
{
    uint32_t i = 0;
    if (!out) return;
    out->enabled = s_enabled;
    out->locked = s_locked;
    out->ecc_enabled = s_ecc_enabled;
    out->sealed_writes = s_sealed_writes;
    out->opened_seals = s_opened_seals;
    out->ecc_corrections = s_ecc_corrections;
    out->ecc_failures = s_ecc_failures;
    out->scrubbed_bytes = s_scrubbed_bytes;
    out->last_error = s_last_error;
    out->key_hash = s_key_hash;
    while (s_recovery_key[i] && i < LARDSEC_KEY_TEXT_MAX) {
        out->recovery_key[i] = s_recovery_key[i];
        i++;
    }
    out->recovery_key[i] = '\0';
}

int lardsec_selftest(void)
{
    uint8_t plain[1024];
    uint8_t sealed[1536];
    uint8_t opened[1536];
    uint32_t old_enabled = s_enabled;
    uint32_t old_locked = s_locked;
    uint32_t old_ecc = s_ecc_enabled;
    uint32_t old_key[4] = { s_key[0], s_key[1], s_key[2], s_key[3] };
    uint32_t old_hash = s_key_hash;
    uint32_t old_sealed_writes = s_sealed_writes;
    uint32_t old_opened_seals = s_opened_seals;
    uint32_t old_ecc_corrections = s_ecc_corrections;
    uint32_t old_ecc_failures = s_ecc_failures;
    uint32_t old_scrubbed_bytes = s_scrubbed_bytes;
    uint32_t old_last_error = s_last_error;
    char old_text[LARDSEC_KEY_TEXT_MAX + 1u];
    int ok = 1;
    for (uint32_t i = 0; i <= LARDSEC_KEY_TEXT_MAX; i++) old_text[i] = s_recovery_key[i];
    for (uint32_t i = 0; i < sizeof(plain); i++) plain[i] = (uint8_t)(i * 3u + 7u);
    lardsec_regen_key(1234u);
    lardsec_enable(1);
    lardsec_set_ecc(1);
    if (lardsec_seal_media_image('Z', 100u, plain, sealed, sizeof(sealed), sizeof(plain)) != 0) ok = 0;
    if (ok && !lardsec_is_sealed_image(sealed, sizeof(sealed))) ok = 0;
    if (ok) sealed[LARDSEC_HEADER_BYTES + 21u] ^= 0x04u;
    if (ok && lardsec_open_media_image('Z', 100u, sealed, opened, sizeof(sealed), sizeof(plain)) != 0) ok = 0;
    for (uint32_t i = 0; ok && i < sizeof(plain); i++) {
        if (plain[i] != opened[i]) ok = 0;
    }
    if (ok && lardsec_lock() != 0) ok = 0;
    if (ok && lardsec_open_media_image('Z', 100u, sealed, opened, sizeof(sealed), sizeof(plain)) == 0) ok = 0;
    if (ok && lardsec_unlock(s_recovery_key) != 0) ok = 0;
    s_key[0] = old_key[0];
    s_key[1] = old_key[1];
    s_key[2] = old_key[2];
    s_key[3] = old_key[3];
    s_key_hash = old_hash;
    for (uint32_t i = 0; i <= LARDSEC_KEY_TEXT_MAX; i++) s_recovery_key[i] = old_text[i];
    s_enabled = old_enabled;
    s_locked = old_locked;
    s_ecc_enabled = old_ecc;
    s_sealed_writes = old_sealed_writes;
    s_opened_seals = old_opened_seals;
    s_ecc_corrections = old_ecc_corrections;
    s_ecc_failures = old_ecc_failures;
    s_scrubbed_bytes = old_scrubbed_bytes;
    s_last_error = old_last_error;
    return ok ? 0 : -1;
}
