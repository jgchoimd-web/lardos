#include "lard_tls.h"
#include "rtc.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

#define TLS_CT_CHANGE_CIPHER 20
#define TLS_CT_ALERT         21
#define TLS_CT_HANDSHAKE     22
#define TLS_CT_APP_DATA      23

#define TLS_HS_SERVER_HELLO       2
#define TLS_HS_CERTIFICATE        11
#define TLS_HS_SERVER_KEY_EXCHANGE 12
#define TLS_HS_CERTIFICATE_REQUEST 13
#define TLS_HS_SERVER_HELLO_DONE  14
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_FINISHED           20

#define TLS12_VERSION 0x0303u
#define TLS_RSA_WITH_AES_128_CBC_SHA    0x002Fu
#define TLS_RSA_WITH_AES_128_CBC_SHA256 0x003Cu

#define TLS_RECORD_BODY_MAX 18432u
#define TLS_RECORD_WIRE_MAX (TLS_RECORD_BODY_MAX + 5u)
#define TLS_AES_BLOCK 16u

/*
 * The freestanding kernel has no allocator. These scratch buffers make the
 * native TLS path single-session-at-a-time, matching the current browser flow.
 */
static uint8_t g_tls_record[TLS_RECORD_WIRE_MAX];
static uint8_t g_tls_plain[TLS_RECORD_BODY_MAX];
static uint8_t g_tls_app_plain[TLS_RECORD_BODY_MAX];
static uint8_t g_tls_aes_sbox[256];
static uint8_t g_tls_aes_inv_sbox[256];
static int g_tls_aes_ready;

typedef struct {
    uint8_t modulus[LARD_TLS_RSA_MAX_BYTES];
    uint16_t modulus_len;
    uint8_t exponent[8];
    uint8_t exponent_len;
} rsa_pubkey_t;

typedef struct {
    const uint8_t* subject;
    uint16_t subject_len;
    const uint8_t* modulus;
    uint16_t modulus_len;
    const uint8_t* exponent;
    uint8_t exponent_len;
} lard_tls_trust_anchor_t;

#include "lard_tls_roots.inc"

typedef enum {
    X509_SIG_UNKNOWN = 0,
    X509_SIG_RSA_SHA1,
    X509_SIG_RSA_SHA256,
    X509_SIG_RSA_SHA384
} x509_sig_alg_t;

typedef struct {
    const uint8_t* tbs;
    uint32_t tbs_len;
    const uint8_t* issuer;
    uint32_t issuer_len;
    const uint8_t* subject;
    uint32_t subject_len;
    const uint8_t* signature;
    uint32_t signature_len;
    x509_sig_alg_t sig_alg;
    rsa_pubkey_t rsa;
    int has_rsa;
    int is_ca;
    int key_usage_present;
    int key_cert_sign;
} x509_cert_t;

static x509_cert_t g_tls_certs[8];

static uint16_t load_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t load_be24(const uint8_t* p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t load_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t load_be64(const uint8_t* p)
{
    uint64_t hi = load_be32(p);
    uint64_t lo = load_be32(p + 4);
    return (hi << 32) | lo;
}

static void store_be16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void store_be24(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void store_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void store_be64(uint8_t* p, uint64_t v)
{
    for (uint32_t i = 0; i < 8; i++) {
        p[7u - i] = (uint8_t)(v >> (i * 8u));
    }
}

static int put_u8(uint8_t* b, uint32_t cap, uint32_t* p, uint8_t v)
{
    if (*p >= cap) return LARD_TLS_ERR_OVERFLOW;
    b[(*p)++] = v;
    return 0;
}

static int put_u16(uint8_t* b, uint32_t cap, uint32_t* p, uint16_t v)
{
    if (*p + 2u > cap) return LARD_TLS_ERR_OVERFLOW;
    b[(*p)++] = (uint8_t)(v >> 8);
    b[(*p)++] = (uint8_t)v;
    return 0;
}

static int put_u24(uint8_t* b, uint32_t cap, uint32_t* p, uint32_t v)
{
    if (*p + 3u > cap || v > 0xFFFFFFu) return LARD_TLS_ERR_OVERFLOW;
    b[(*p)++] = (uint8_t)(v >> 16);
    b[(*p)++] = (uint8_t)(v >> 8);
    b[(*p)++] = (uint8_t)v;
    return 0;
}

static int put_u24_at(uint8_t* b, uint32_t cap, uint32_t p, uint32_t v)
{
    if (p + 3u > cap || v > 0xFFFFFFu) return LARD_TLS_ERR_OVERFLOW;
    store_be24(b + p, v);
    return 0;
}

static int put_bytes(uint8_t* b, uint32_t cap, uint32_t* p, const uint8_t* s, uint32_t n)
{
    if (*p + n > cap) return LARD_TLS_ERR_OVERFLOW;
    memcpy(b + *p, s, n);
    *p += n;
    return 0;
}

static int ct_memeq(const uint8_t* a, const uint8_t* b, uint32_t n)
{
    uint8_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint8_t)(a[i] ^ b[i]);
    return v == 0;
}

static uint64_t rdtsc64(void)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void fill_random_bytes(uint8_t* out, uint32_t len)
{
    uint64_t x = rdtsc64() ^ 0x4c415244544c5331ull;
    for (uint32_t i = 0; i < len; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        x += rdtsc64() + (uint64_t)i * 0x9e3779b97f4a7c15ull;
        out[i] = (uint8_t)(x >> ((i & 7u) * 8u));
    }
}

static void fill_random_nonzero(uint8_t* out, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        do {
            fill_random_bytes(out + i, 1);
        } while (out[i] == 0);
    }
}

static uint32_t copy_server_name(char* out, uint32_t cap, const char* in)
{
    uint32_t i = 0;
    if (!cap) return 0;
    while (in && in[i] && in[i] != ':' && i + 1u < cap) {
        out[i] = in[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

static uint8_t ascii_lower(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c + ('a' - 'A'));
    return c;
}

static int host_match_name(const char* host, const uint8_t* name, uint32_t name_len)
{
    uint32_t host_len = (uint32_t)strlen(host);
    if (!host_len || !name_len) return 0;

    if (name_len > 2u && name[0] == '*' && name[1] == '.') {
        uint32_t suffix_len = name_len - 1u;
        if (host_len <= suffix_len) return 0;
        uint32_t prefix_len = host_len - suffix_len;
        for (uint32_t i = 0; i < prefix_len; i++) {
            if (host[i] == '.') return 0;
        }
        for (uint32_t i = 0; i < suffix_len; i++) {
            if (ascii_lower((uint8_t)host[prefix_len + i]) != ascii_lower(name[1u + i])) return 0;
        }
        return 1;
    }

    if (host_len != name_len) return 0;
    for (uint32_t i = 0; i < name_len; i++) {
        if (ascii_lower((uint8_t)host[i]) != ascii_lower(name[i])) return 0;
    }
    return 1;
}

static uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static uint32_t rotl32(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32u - n));
}

static const uint32_t k_sha256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void sha256_transform(lard_tls_sha256_state_t* c, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, cc, d, e, f, g, h;
    for (uint32_t i = 0; i < 16u; i++) w[i] = load_be32(block + i * 4u);
    for (uint32_t i = 16u; i < 64u; i++) {
        uint32_t s0 = rotr32(w[i - 15u], 7) ^ rotr32(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotr32(w[i - 2u], 17) ^ rotr32(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k_sha256[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(lard_tls_sha256_state_t* c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u; c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu; c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->bytes = 0;
    c->used = 0;
}

static void sha256_update(lard_tls_sha256_state_t* c, const uint8_t* data, uint32_t len)
{
    c->bytes += len;
    while (len) {
        uint32_t n = 64u - c->used;
        if (n > len) n = len;
        memcpy(c->block + c->used, data, n);
        c->used += n;
        data += n;
        len -= n;
        if (c->used == 64u) {
            sha256_transform(c, c->block);
            c->used = 0;
        }
    }
}

static void sha256_final_mut(lard_tls_sha256_state_t* c, uint8_t out[32])
{
    uint64_t bits = c->bytes * 8u;
    c->block[c->used++] = 0x80u;
    if (c->used > 56u) {
        while (c->used < 64u) c->block[c->used++] = 0;
        sha256_transform(c, c->block);
        c->used = 0;
    }
    while (c->used < 56u) c->block[c->used++] = 0;
    store_be64(c->block + 56, bits);
    sha256_transform(c, c->block);
    for (uint32_t i = 0; i < 8u; i++) store_be32(out + i * 4u, c->h[i]);
}

static void sha256_finish_copy(const lard_tls_sha256_state_t* c, uint8_t out[32])
{
    lard_tls_sha256_state_t tmp = *c;
    sha256_final_mut(&tmp, out);
}

typedef struct {
    uint64_t h[8];
    uint64_t bytes;
    uint8_t block[128];
    uint32_t used;
} sha384_state_t;

static uint64_t rotr64(uint64_t x, uint32_t n)
{
    return (x >> n) | (x << (64u - n));
}

static const uint64_t k_sha512[80] = {
    0x428a2f98d728ae22ull,0x7137449123ef65cdull,0xb5c0fbcfec4d3b2full,0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull,0x59f111f1b605d019ull,0x923f82a4af194f9bull,0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull,0x12835b0145706fbeull,0x243185be4ee4b28cull,0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full,0x80deb1fe3b1696b1ull,0x9bdc06a725c71235ull,0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull,0xefbe4786384f25e3ull,0x0fc19dc68b8cd5b5ull,0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull,0x4a7484aa6ea6e483ull,0x5cb0a9dcbd41fbd4ull,0x76f988da831153b5ull,
    0x983e5152ee66dfabull,0xa831c66d2db43210ull,0xb00327c898fb213full,0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull,0xd5a79147930aa725ull,0x06ca6351e003826full,0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull,0x2e1b21385c26c926ull,0x4d2c6dfc5ac42aedull,0x53380d139d95b3dfull,
    0x650a73548baf63deull,0x766a0abb3c77b2a8ull,0x81c2c92e47edaee6ull,0x92722c851482353bull,
    0xa2bfe8a14cf10364ull,0xa81a664bbc423001ull,0xc24b8b70d0f89791ull,0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull,0xd69906245565a910ull,0xf40e35855771202aull,0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull,0x1e376c085141ab53ull,0x2748774cdf8eeb99ull,0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull,0x4ed8aa4ae3418acbull,0x5b9cca4f7763e373ull,0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull,0x78a5636f43172f60ull,0x84c87814a1f0ab72ull,0x8cc702081a6439ecull,
    0x90befffa23631e28ull,0xa4506cebde82bde9ull,0xbef9a3f7b2c67915ull,0xc67178f2e372532bull,
    0xca273eceea26619cull,0xd186b8c721c0c207ull,0xeada7dd6cde0eb1eull,0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull,0x0a637dc5a2c898a6ull,0x113f9804bef90daeull,0x1b710b35131c471bull,
    0x28db77f523047d84ull,0x32caab7b40c72493ull,0x3c9ebe0a15c9bebcull,0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull,0x597f299cfc657e2aull,0x5fcb6fab3ad6faecull,0x6c44198c4a475817ull
};

static void sha384_transform(sha384_state_t* c, const uint8_t block[128])
{
    uint64_t w[80];
    uint64_t a, b, cc, d, e, f, g, h;
    for (uint32_t i = 0; i < 16u; i++) w[i] = load_be64(block + i * 8u);
    for (uint32_t i = 16u; i < 80u; i++) {
        uint64_t s0 = rotr64(w[i - 15u], 1) ^ rotr64(w[i - 15u], 8) ^ (w[i - 15u] >> 7);
        uint64_t s1 = rotr64(w[i - 2u], 19) ^ rotr64(w[i - 2u], 61) ^ (w[i - 2u] >> 6);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (uint32_t i = 0; i < 80u; i++) {
        uint64_t s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t t1 = h + s1 + ch + k_sha512[i] + w[i];
        uint64_t s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint64_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha384_init(sha384_state_t* c)
{
    c->h[0] = 0xcbbb9d5dc1059ed8ull; c->h[1] = 0x629a292a367cd507ull;
    c->h[2] = 0x9159015a3070dd17ull; c->h[3] = 0x152fecd8f70e5939ull;
    c->h[4] = 0x67332667ffc00b31ull; c->h[5] = 0x8eb44a8768581511ull;
    c->h[6] = 0xdb0c2e0d64f98fa7ull; c->h[7] = 0x47b5481dbefa4fa4ull;
    c->bytes = 0;
    c->used = 0;
}

static void sha384_update(sha384_state_t* c, const uint8_t* data, uint32_t len)
{
    c->bytes += len;
    while (len) {
        uint32_t n = 128u - c->used;
        if (n > len) n = len;
        memcpy(c->block + c->used, data, n);
        c->used += n;
        data += n;
        len -= n;
        if (c->used == 128u) {
            sha384_transform(c, c->block);
            c->used = 0;
        }
    }
}

static void sha384_final(sha384_state_t* c, uint8_t out[48])
{
    uint64_t bits = c->bytes * 8u;
    c->block[c->used++] = 0x80u;
    if (c->used > 112u) {
        while (c->used < 128u) c->block[c->used++] = 0;
        sha384_transform(c, c->block);
        c->used = 0;
    }
    while (c->used < 112u) c->block[c->used++] = 0;
    store_be64(c->block + 112, 0);
    store_be64(c->block + 120, bits);
    sha384_transform(c, c->block);
    for (uint32_t i = 0; i < 6u; i++) store_be64(out + i * 8u, c->h[i]);
}

typedef struct {
    uint32_t h[5];
    uint64_t bytes;
    uint8_t block[64];
    uint32_t used;
} sha1_state_t;

static void sha1_transform(sha1_state_t* c, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a, b, cc, d, e;
    for (uint32_t i = 0; i < 16u; i++) w[i] = load_be32(block + i * 4u);
    for (uint32_t i = 16u; i < 80u; i++) w[i] = rotl32(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1);
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
    for (uint32_t i = 0; i < 80u; i++) {
        uint32_t f, k;
        if (i < 20u) { f = (b & cc) | ((~b) & d); k = 0x5a827999u; }
        else if (i < 40u) { f = b ^ cc ^ d; k = 0x6ed9eba1u; }
        else if (i < 60u) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdcu; }
        else { f = b ^ cc ^ d; k = 0xca62c1d6u; }
        uint32_t t = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rotl32(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(sha1_state_t* c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u; c->h[2] = 0x98badcfeu; c->h[3] = 0x10325476u; c->h[4] = 0xc3d2e1f0u;
    c->bytes = 0;
    c->used = 0;
}

static void sha1_update(sha1_state_t* c, const uint8_t* data, uint32_t len)
{
    c->bytes += len;
    while (len) {
        uint32_t n = 64u - c->used;
        if (n > len) n = len;
        memcpy(c->block + c->used, data, n);
        c->used += n;
        data += n;
        len -= n;
        if (c->used == 64u) {
            sha1_transform(c, c->block);
            c->used = 0;
        }
    }
}

static void sha1_final(sha1_state_t* c, uint8_t out[20])
{
    uint64_t bits = c->bytes * 8u;
    c->block[c->used++] = 0x80u;
    if (c->used > 56u) {
        while (c->used < 64u) c->block[c->used++] = 0;
        sha1_transform(c, c->block);
        c->used = 0;
    }
    while (c->used < 56u) c->block[c->used++] = 0;
    store_be64(c->block + 56, bits);
    sha1_transform(c, c->block);
    for (uint32_t i = 0; i < 5u; i++) store_be32(out + i * 4u, c->h[i]);
}

static void hmac_sha256_parts(const uint8_t* key, uint32_t key_len,
                              const uint8_t** parts, const uint32_t* lens,
                              uint32_t count, uint8_t out[32])
{
    uint8_t k0[64];
    uint8_t kh[32];
    uint8_t inner[32];
    lard_tls_sha256_state_t ctx;
    memset(k0, 0, sizeof(k0));
    if (key_len > 64u) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final_mut(&ctx, kh);
        memcpy(k0, kh, sizeof(kh));
    } else {
        memcpy(k0, key, key_len);
    }
    for (uint32_t i = 0; i < 64u; i++) k0[i] ^= 0x36u;
    sha256_init(&ctx);
    sha256_update(&ctx, k0, 64);
    for (uint32_t i = 0; i < count; i++) sha256_update(&ctx, parts[i], lens[i]);
    sha256_final_mut(&ctx, inner);
    for (uint32_t i = 0; i < 64u; i++) k0[i] ^= (uint8_t)(0x36u ^ 0x5cu);
    sha256_init(&ctx);
    sha256_update(&ctx, k0, 64);
    sha256_update(&ctx, inner, sizeof(inner));
    sha256_final_mut(&ctx, out);
}

static void hmac_sha1_parts(const uint8_t* key, uint32_t key_len,
                            const uint8_t** parts, const uint32_t* lens,
                            uint32_t count, uint8_t out[20])
{
    uint8_t k0[64];
    uint8_t kh[20];
    uint8_t inner[20];
    sha1_state_t ctx;
    memset(k0, 0, sizeof(k0));
    if (key_len > 64u) {
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, kh);
        memcpy(k0, kh, sizeof(kh));
    } else {
        memcpy(k0, key, key_len);
    }
    for (uint32_t i = 0; i < 64u; i++) k0[i] ^= 0x36u;
    sha1_init(&ctx);
    sha1_update(&ctx, k0, 64);
    for (uint32_t i = 0; i < count; i++) sha1_update(&ctx, parts[i], lens[i]);
    sha1_final(&ctx, inner);
    for (uint32_t i = 0; i < 64u; i++) k0[i] ^= (uint8_t)(0x36u ^ 0x5cu);
    sha1_init(&ctx);
    sha1_update(&ctx, k0, 64);
    sha1_update(&ctx, inner, sizeof(inner));
    sha1_final(&ctx, out);
}

static int tls_prf_sha256(const uint8_t* secret, uint32_t secret_len,
                          const char* label, const uint8_t* seed, uint32_t seed_len,
                          uint8_t* out, uint32_t out_len)
{
    uint8_t label_seed[128];
    uint8_t a[32];
    uint8_t next_a[32];
    uint8_t h[32];
    uint32_t label_len = (uint32_t)strlen(label);
    uint32_t ls_len = label_len + seed_len;
    if (ls_len > sizeof(label_seed)) return LARD_TLS_ERR_OVERFLOW;
    memcpy(label_seed, label, label_len);
    memcpy(label_seed + label_len, seed, seed_len);
    const uint8_t* parts1[1] = { label_seed };
    const uint32_t lens1[1] = { ls_len };
    hmac_sha256_parts(secret, secret_len, parts1, lens1, 1, a);
    uint32_t done = 0;
    while (done < out_len) {
        const uint8_t* parts2[2] = { a, label_seed };
        const uint32_t lens2[2] = { sizeof(a), ls_len };
        hmac_sha256_parts(secret, secret_len, parts2, lens2, 2, h);
        uint32_t n = out_len - done;
        if (n > sizeof(h)) n = sizeof(h);
        memcpy(out + done, h, n);
        done += n;
        const uint8_t* parts3[1] = { a };
        const uint32_t lens3[1] = { sizeof(a) };
        hmac_sha256_parts(secret, secret_len, parts3, lens3, 1, next_a);
        memcpy(a, next_a, sizeof(a));
    }
    return 0;
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1u) r ^= a;
        uint8_t hi = (uint8_t)(a & 0x80u);
        a <<= 1;
        if (hi) a ^= 0x1bu;
        b >>= 1;
    }
    return r;
}

static uint8_t gf_pow(uint8_t a, uint8_t e)
{
    uint8_t r = 1;
    while (e) {
        if (e & 1u) r = gf_mul(r, a);
        a = gf_mul(a, a);
        e >>= 1;
    }
    return r;
}

static uint8_t rotl8(uint8_t x, uint8_t n)
{
    return (uint8_t)((x << n) | (x >> (8u - n)));
}

static void aes_init_tables(void)
{
    if (g_tls_aes_ready) return;
    for (uint32_t i = 0; i < 256u; i++) {
        uint8_t inv = (i == 0) ? 0 : gf_pow((uint8_t)i, 254);
        uint8_t s = (uint8_t)(inv ^ rotl8(inv, 1) ^ rotl8(inv, 2) ^ rotl8(inv, 3) ^ rotl8(inv, 4) ^ 0x63u);
        g_tls_aes_sbox[i] = s;
        g_tls_aes_inv_sbox[s] = (uint8_t)i;
    }
    g_tls_aes_ready = 1;
}

static void aes128_expand(const uint8_t key[16], uint8_t rk[176])
{
    static const uint8_t rcon_init = 1;
    uint8_t temp[4];
    uint8_t rcon = rcon_init;
    aes_init_tables();
    memcpy(rk, key, 16);
    uint32_t bytes = 16;
    while (bytes < 176u) {
        temp[0] = rk[bytes - 4u];
        temp[1] = rk[bytes - 3u];
        temp[2] = rk[bytes - 2u];
        temp[3] = rk[bytes - 1u];
        if ((bytes & 15u) == 0) {
            uint8_t t = temp[0];
            temp[0] = g_tls_aes_sbox[temp[1]] ^ rcon;
            temp[1] = g_tls_aes_sbox[temp[2]];
            temp[2] = g_tls_aes_sbox[temp[3]];
            temp[3] = g_tls_aes_sbox[t];
            rcon = gf_mul(rcon, 2);
        }
        for (uint32_t i = 0; i < 4u; i++) {
            rk[bytes] = (uint8_t)(rk[bytes - 16u] ^ temp[i]);
            bytes++;
        }
    }
}

static void aes_add_round_key(uint8_t s[16], const uint8_t* rk)
{
    for (uint32_t i = 0; i < 16u; i++) s[i] ^= rk[i];
}

static void aes_sub_bytes(uint8_t s[16])
{
    for (uint32_t i = 0; i < 16u; i++) s[i] = g_tls_aes_sbox[s[i]];
}

static void aes_inv_sub_bytes(uint8_t s[16])
{
    for (uint32_t i = 0; i < 16u; i++) s[i] = g_tls_aes_inv_sbox[s[i]];
}

static void aes_shift_rows(uint8_t s[16])
{
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void aes_inv_shift_rows(uint8_t s[16])
{
    uint8_t t;
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

static void aes_mix_columns(uint8_t s[16])
{
    for (uint32_t c = 0; c < 4u; c++) {
        uint8_t* p = s + c * 4u;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
        p[1] = (uint8_t)(a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
        p[2] = (uint8_t)(a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
        p[3] = (uint8_t)(gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
    }
}

static void aes_inv_mix_columns(uint8_t s[16])
{
    for (uint32_t c = 0; c < 4u; c++) {
        uint8_t* p = s + c * 4u;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
        p[1] = (uint8_t)(gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
        p[2] = (uint8_t)(gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
        p[3] = (uint8_t)(gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
    }
}

static void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t rk[176];
    uint8_t s[16];
    aes128_expand(key, rk);
    memcpy(s, in, 16);
    aes_add_round_key(s, rk);
    for (uint32_t round = 1; round < 10u; round++) {
        aes_sub_bytes(s);
        aes_shift_rows(s);
        aes_mix_columns(s);
        aes_add_round_key(s, rk + round * 16u);
    }
    aes_sub_bytes(s);
    aes_shift_rows(s);
    aes_add_round_key(s, rk + 160);
    memcpy(out, s, 16);
}

static void aes128_decrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t rk[176];
    uint8_t s[16];
    aes128_expand(key, rk);
    memcpy(s, in, 16);
    aes_add_round_key(s, rk + 160);
    for (uint32_t round = 9; round > 0; round--) {
        aes_inv_shift_rows(s);
        aes_inv_sub_bytes(s);
        aes_add_round_key(s, rk + round * 16u);
        aes_inv_mix_columns(s);
    }
    aes_inv_shift_rows(s);
    aes_inv_sub_bytes(s);
    aes_add_round_key(s, rk);
    memcpy(out, s, 16);
}

static void aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                               const uint8_t* in, uint8_t* out, uint32_t len)
{
    uint8_t prev[16];
    uint8_t block[16];
    memcpy(prev, iv, 16);
    for (uint32_t off = 0; off < len; off += 16u) {
        for (uint32_t i = 0; i < 16u; i++) block[i] = (uint8_t)(in[off + i] ^ prev[i]);
        aes128_encrypt_block(key, block, out + off);
        memcpy(prev, out + off, 16);
    }
}

static void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                               const uint8_t* in, uint8_t* out, uint32_t len)
{
    uint8_t prev[16];
    uint8_t cur[16];
    uint8_t block[16];
    memcpy(prev, iv, 16);
    for (uint32_t off = 0; off < len; off += 16u) {
        memcpy(cur, in + off, 16);
        aes128_decrypt_block(key, cur, block);
        for (uint32_t i = 0; i < 16u; i++) out[off + i] = (uint8_t)(block[i] ^ prev[i]);
        memcpy(prev, cur, 16);
    }
}

static int bn_cmp(const uint8_t* a, const uint8_t* b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

static void bn_sub_mod(uint8_t* a, const uint8_t* mod, uint32_t n)
{
    int borrow = 0;
    for (uint32_t i = n; i > 0; i--) {
        int diff = (int)a[i - 1u] - (int)mod[i - 1u] - borrow;
        if (diff < 0) {
            diff += 256;
            borrow = 1;
        } else {
            borrow = 0;
        }
        a[i - 1u] = (uint8_t)diff;
    }
}

static void bn_add_mod(uint8_t* acc, const uint8_t* add, const uint8_t* mod, uint32_t n)
{
    uint16_t carry = 0;
    for (uint32_t i = n; i > 0; i--) {
        uint16_t sum = (uint16_t)acc[i - 1u] + add[i - 1u] + carry;
        acc[i - 1u] = (uint8_t)sum;
        carry = (uint16_t)(sum >> 8);
    }
    if (carry || bn_cmp(acc, mod, n) >= 0) bn_sub_mod(acc, mod, n);
}

static void bn_double_mod(uint8_t* a, const uint8_t* mod, uint32_t n)
{
    uint16_t carry = 0;
    for (uint32_t i = n; i > 0; i--) {
        uint16_t v = (uint16_t)(a[i - 1u] << 1) | carry;
        a[i - 1u] = (uint8_t)v;
        carry = (uint16_t)(v >> 8);
    }
    if (carry || bn_cmp(a, mod, n) >= 0) bn_sub_mod(a, mod, n);
}

static void bn_mod_mul(const uint8_t* a, const uint8_t* b, const uint8_t* mod, uint32_t n, uint8_t* out)
{
    uint8_t acc[LARD_TLS_RSA_MAX_BYTES];
    uint8_t tmp[LARD_TLS_RSA_MAX_BYTES];
    memset(acc, 0, n);
    memcpy(tmp, a, n);
    for (uint32_t byte = n; byte > 0; byte--) {
        uint8_t v = b[byte - 1u];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            if (v & (1u << bit)) bn_add_mod(acc, tmp, mod, n);
            bn_double_mod(tmp, mod, n);
        }
    }
    memcpy(out, acc, n);
}

static int rsa_modexp(const uint8_t* mod, uint32_t mod_len,
                      const uint8_t* exp, uint32_t exp_len,
                      const uint8_t* input, uint8_t* out)
{
    if (!mod_len || mod_len > LARD_TLS_RSA_MAX_BYTES || !exp_len || exp_len > 8u) {
        return LARD_TLS_ERR_UNSUPPORTED_CERT;
    }
    uint8_t result[LARD_TLS_RSA_MAX_BYTES];
    uint8_t base[LARD_TLS_RSA_MAX_BYTES];
    uint8_t tmp[LARD_TLS_RSA_MAX_BYTES];
    memset(result, 0, mod_len);
    result[mod_len - 1u] = 1;
    memcpy(base, input, mod_len);
    for (uint32_t i = 0; i < exp_len; i++) {
        uint8_t v = exp[i];
        for (int bit = 7; bit >= 0; bit--) {
            bn_mod_mul(result, result, mod, mod_len, tmp);
            memcpy(result, tmp, mod_len);
            if (v & (1u << bit)) {
                bn_mod_mul(result, base, mod, mod_len, tmp);
                memcpy(result, tmp, mod_len);
            }
        }
    }
    memcpy(out, result, mod_len);
    return 0;
}

static int rsa_pkcs1_encrypt(lard_tls_client_t* c, const uint8_t* msg, uint32_t msg_len, uint8_t* out)
{
    uint32_t n = c->rsa_modulus_len;
    if (n < msg_len + 11u || n > LARD_TLS_RSA_MAX_BYTES) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    uint8_t em[LARD_TLS_RSA_MAX_BYTES];
    em[0] = 0;
    em[1] = 2;
    uint32_t ps_len = n - msg_len - 3u;
    fill_random_nonzero(em + 2, ps_len);
    em[2u + ps_len] = 0;
    memcpy(em + 3u + ps_len, msg, msg_len);
    return rsa_modexp(c->rsa_modulus, n, c->rsa_exponent, c->rsa_exponent_len, em, out);
}

static int der_read_tlv(const uint8_t* der, uint32_t len, uint32_t* off,
                        uint8_t* tag, const uint8_t** val, uint32_t* vlen)
{
    if (*off + 2u > len) return 0;
    uint8_t t = der[(*off)++];
    uint8_t l0 = der[(*off)++];
    uint32_t l = 0;
    if ((t & 0x1fu) == 0x1fu) return 0;
    if (l0 & 0x80u) {
        uint32_t n = l0 & 0x7fu;
        if (n == 0 || n > 4u || *off + n > len) return 0;
        for (uint32_t i = 0; i < n; i++) l = (l << 8) | der[(*off)++];
    } else {
        l = l0;
    }
    if (*off + l > len) return 0;
    *tag = t;
    *val = der + *off;
    *vlen = l;
    *off += l;
    return 1;
}

static int der_skip_tlv(const uint8_t* der, uint32_t len, uint32_t* off)
{
    uint8_t tag;
    const uint8_t* val;
    uint32_t vlen;
    return der_read_tlv(der, len, off, &tag, &val, &vlen);
}

static int oid_eq(const uint8_t* oid, uint32_t len, const uint8_t* want, uint32_t want_len)
{
    return len == want_len && memcmp(oid, want, len) == 0;
}

static int parse_rsa_spki_key(rsa_pubkey_t* key, const uint8_t* spki, uint32_t spki_len)
{
    static const uint8_t oid_rsa[] = { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01 };
    uint8_t tag;
    const uint8_t* val;
    uint32_t len;
    const uint8_t* seq = spki;
    uint32_t seq_len = spki_len;
    uint32_t so = 0;
    if (!der_read_tlv(seq, seq_len, &so, &tag, &val, &len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    uint32_t ao = 0;
    uint8_t atag;
    const uint8_t* aval;
    uint32_t alen;
    if (!der_read_tlv(val, len, &ao, &atag, &aval, &alen) || atag != 0x06) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    if (!oid_eq(aval, alen, oid_rsa, sizeof(oid_rsa))) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    if (!der_read_tlv(seq, seq_len, &so, &tag, &val, &len) || tag != 0x03 || len < 2u || val[0] != 0) {
        return LARD_TLS_ERR_UNSUPPORTED_CERT;
    }
    uint32_t ro = 1;
    const uint8_t* rseq;
    uint32_t rseq_len;
    if (!der_read_tlv(val, len, &ro, &tag, &rseq, &rseq_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    uint32_t rso = 0;
    const uint8_t* mod;
    uint32_t mod_len;
    const uint8_t* exp;
    uint32_t exp_len;
    if (!der_read_tlv(rseq, rseq_len, &rso, &tag, &mod, &mod_len) || tag != 0x02) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    while (mod_len > 1u && *mod == 0) { mod++; mod_len--; }
    if (!der_read_tlv(rseq, rseq_len, &rso, &tag, &exp, &exp_len) || tag != 0x02) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    while (exp_len > 1u && *exp == 0) { exp++; exp_len--; }
    if (mod_len > LARD_TLS_RSA_MAX_BYTES || exp_len > sizeof(key->exponent) || mod_len < 128u) {
        return LARD_TLS_ERR_UNSUPPORTED_CERT;
    }
    memcpy(key->modulus, mod, mod_len);
    key->modulus_len = (uint16_t)mod_len;
    memcpy(key->exponent, exp, exp_len);
    key->exponent_len = (uint8_t)exp_len;
    return 0;
}

static int parse_name_cn(const uint8_t* name, uint32_t name_len, const char* host)
{
    static const uint8_t oid_cn[] = { 0x55,0x04,0x03 };
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* setv;
    uint32_t set_len;
    while (der_read_tlv(name, name_len, &off, &tag, &setv, &set_len)) {
        if (tag != 0x31) continue;
        uint32_t so = 0;
        const uint8_t* av;
        uint32_t av_len;
        if (!der_read_tlv(setv, set_len, &so, &tag, &av, &av_len) || tag != 0x30) continue;
        uint32_t ao = 0;
        const uint8_t* oid;
        uint32_t oid_len;
        const uint8_t* value;
        uint32_t value_len;
        if (!der_read_tlv(av, av_len, &ao, &tag, &oid, &oid_len) || tag != 0x06) continue;
        if (!der_read_tlv(av, av_len, &ao, &tag, &value, &value_len)) continue;
        if (oid_eq(oid, oid_len, oid_cn, sizeof(oid_cn)) &&
            (tag == 0x0c || tag == 0x13 || tag == 0x16) &&
            host_match_name(host, value, value_len)) {
            return 1;
        }
    }
    return 0;
}

static int parse_general_names(const uint8_t* der, uint32_t der_len, const char* host,
                               int* had_dns, int* matched_dns)
{
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* seq;
    uint32_t seq_len;
    if (!der_read_tlv(der, der_len, &off, &tag, &seq, &seq_len) || tag != 0x30) return 0;
    uint32_t so = 0;
    while (der_read_tlv(seq, seq_len, &so, &tag, &der, &der_len)) {
        if (tag == 0x82) {
            *had_dns = 1;
            if (host_match_name(host, der, der_len)) *matched_dns = 1;
        }
    }
    return 1;
}

static int tls_is_leap(int y)
{
    return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

static int64_t tls_civil_to_unix(int y, int mo, int d, int hh, int mm, int ss)
{
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int64_t days = 0;
    for (int yy = 1970; yy < y; yy++) {
        days += 365;
        if (tls_is_leap(yy)) days++;
    }
    for (int m = 1; m < mo; m++) {
        int dim = mdays[m - 1];
        if (m == 2 && tls_is_leap(y)) dim = 29;
        days += dim;
    }
    days += d - 1;
    return days * 86400LL + (int64_t)hh * 3600 + (int64_t)mm * 60 + ss;
}

static int dec2(const uint8_t* p)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    return (p[0] - '0') * 10 + (p[1] - '0');
}

static int dec4(const uint8_t* p)
{
    int a = dec2(p);
    int b = dec2(p + 2);
    if (a < 0 || b < 0) return -1;
    return a * 100 + b;
}

static int parse_asn1_time(uint8_t tag, const uint8_t* v, uint32_t len, int64_t* out)
{
    int year;
    uint32_t p;
    if (tag == 0x17) {
        if (len != 13u || v[12] != 'Z') return 0;
        int yy = dec2(v);
        if (yy < 0) return 0;
        year = yy >= 50 ? 1900 + yy : 2000 + yy;
        p = 2;
    } else if (tag == 0x18) {
        if (len != 15u || v[14] != 'Z') return 0;
        year = dec4(v);
        if (year < 0) return 0;
        p = 4;
    } else {
        return 0;
    }
    int mo = dec2(v + p); p += 2;
    int d = dec2(v + p); p += 2;
    int hh = dec2(v + p); p += 2;
    int mm = dec2(v + p); p += 2;
    int ss = dec2(v + p);
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return 0;
    *out = tls_civil_to_unix(year, mo, d, hh, mm, ss);
    return 1;
}

static int parse_validity_window(const uint8_t* validity, uint32_t validity_len)
{
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* not_before;
    const uint8_t* not_after;
    uint32_t nb_len;
    uint32_t na_len;
    int64_t nb;
    int64_t na;
    if (!der_read_tlv(validity, validity_len, &off, &tag, &not_before, &nb_len)) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    if (!parse_asn1_time(tag, not_before, nb_len, &nb)) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    if (!der_read_tlv(validity, validity_len, &off, &tag, &not_after, &na_len)) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    if (!parse_asn1_time(tag, not_after, na_len, &na)) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    int64_t now = rtc_unix_seconds();
    if (now <= 0 || now < nb || now > na) return LARD_TLS_ERR_CERT_VERIFY;
    return 0;
}

static int parse_sig_alg(const uint8_t* alg, uint32_t alg_len, x509_sig_alg_t* out)
{
    static const uint8_t oid_sha1_rsa[] = { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x05 };
    static const uint8_t oid_sha256_rsa[] = { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b };
    static const uint8_t oid_sha384_rsa[] = { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0c };
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* oid;
    uint32_t oid_len;
    if (!der_read_tlv(alg, alg_len, &off, &tag, &oid, &oid_len) || tag != 0x06) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    if (oid_eq(oid, oid_len, oid_sha1_rsa, sizeof(oid_sha1_rsa))) *out = X509_SIG_RSA_SHA1;
    else if (oid_eq(oid, oid_len, oid_sha256_rsa, sizeof(oid_sha256_rsa))) *out = X509_SIG_RSA_SHA256;
    else if (oid_eq(oid, oid_len, oid_sha384_rsa, sizeof(oid_sha384_rsa))) *out = X509_SIG_RSA_SHA384;
    else return LARD_TLS_ERR_UNSUPPORTED_CERT;
    return 0;
}

static void parse_basic_constraints_value(x509_cert_t* cert, const uint8_t* val, uint32_t val_len)
{
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* seq;
    uint32_t seq_len;
    if (!der_read_tlv(val, val_len, &off, &tag, &seq, &seq_len) || tag != 0x30) return;
    uint32_t so = 0;
    const uint8_t* bv;
    uint32_t blen;
    if (der_read_tlv(seq, seq_len, &so, &tag, &bv, &blen) && tag == 0x01 && blen == 1u) {
        cert->is_ca = bv[0] != 0;
    }
}

static void parse_key_usage_value(x509_cert_t* cert, const uint8_t* val, uint32_t val_len)
{
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* bits;
    uint32_t bits_len;
    if (!der_read_tlv(val, val_len, &off, &tag, &bits, &bits_len) || tag != 0x03 || bits_len < 2u) return;
    cert->key_usage_present = 1;
    cert->key_cert_sign = (bits[1] & 0x04u) != 0;
}

static void parse_x509_extensions(x509_cert_t* cert, const uint8_t* ext_outer, uint32_t ext_outer_len,
                                  const char* host, int is_leaf, int* had_dns, int* matched_dns)
{
    static const uint8_t oid_san[] = { 0x55,0x1d,0x11 };
    static const uint8_t oid_basic_constraints[] = { 0x55,0x1d,0x13 };
    static const uint8_t oid_key_usage[] = { 0x55,0x1d,0x0f };
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* exts;
    uint32_t exts_len;
    if (!der_read_tlv(ext_outer, ext_outer_len, &off, &tag, &exts, &exts_len) || tag != 0x30) return;
    uint32_t eo = 0;
    while (der_read_tlv(exts, exts_len, &eo, &tag, &ext_outer, &ext_outer_len)) {
        if (tag != 0x30) continue;
        uint32_t xo = 0;
        const uint8_t* oid;
        uint32_t oid_len;
        const uint8_t* val;
        uint32_t val_len;
        if (!der_read_tlv(ext_outer, ext_outer_len, &xo, &tag, &oid, &oid_len) || tag != 0x06) continue;
        if (!der_read_tlv(ext_outer, ext_outer_len, &xo, &tag, &val, &val_len)) continue;
        if (tag == 0x01) {
            if (!der_read_tlv(ext_outer, ext_outer_len, &xo, &tag, &val, &val_len)) continue;
        }
        if (tag != 0x04) continue;
        if (is_leaf && oid_eq(oid, oid_len, oid_san, sizeof(oid_san))) {
            parse_general_names(val, val_len, host, had_dns, matched_dns);
        } else if (oid_eq(oid, oid_len, oid_basic_constraints, sizeof(oid_basic_constraints))) {
            parse_basic_constraints_value(cert, val, val_len);
        } else if (oid_eq(oid, oid_len, oid_key_usage, sizeof(oid_key_usage))) {
            parse_key_usage_value(cert, val, val_len);
        }
    }
}

static int parse_x509_cert(x509_cert_t* out, const uint8_t* cert, uint32_t cert_len,
                           const char* host, int is_leaf)
{
    memset(out, 0, sizeof(*out));
    uint32_t off = 0;
    uint8_t tag;
    const uint8_t* val;
    uint32_t len;
    if (!der_read_tlv(cert, cert_len, &off, &tag, &val, &len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    const uint8_t* cert_seq = val;
    uint32_t cert_seq_len = len;
    uint32_t co = 0;
    const uint8_t* tbs;
    uint32_t tbs_len;
    uint32_t tbs_start = co;
    if (!der_read_tlv(cert_seq, cert_seq_len, &co, &tag, &tbs, &tbs_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    out->tbs = cert_seq + tbs_start;
    out->tbs_len = co - tbs_start;
    const uint8_t* alg;
    uint32_t alg_len;
    if (!der_read_tlv(cert_seq, cert_seq_len, &co, &tag, &alg, &alg_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    int ar = parse_sig_alg(alg, alg_len, &out->sig_alg);
    if (ar != 0) return ar;
    const uint8_t* sig;
    uint32_t sig_len;
    if (!der_read_tlv(cert_seq, cert_seq_len, &co, &tag, &sig, &sig_len) || tag != 0x03 || sig_len < 2u || sig[0] != 0) {
        return LARD_TLS_ERR_UNSUPPORTED_CERT;
    }
    out->signature = sig + 1;
    out->signature_len = sig_len - 1u;

    uint32_t to = 0;
    if (to < tbs_len && tbs[to] == 0xa0) {
        if (!der_skip_tlv(tbs, tbs_len, &to)) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    }
    if (!der_skip_tlv(tbs, tbs_len, &to)) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    const uint8_t* tbs_alg;
    uint32_t tbs_alg_len;
    if (!der_read_tlv(tbs, tbs_len, &to, &tag, &tbs_alg, &tbs_alg_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    x509_sig_alg_t tbs_sig_alg = X509_SIG_UNKNOWN;
    int tr = parse_sig_alg(tbs_alg, tbs_alg_len, &tbs_sig_alg);
    if (tr != 0) return tr;
    if (tbs_sig_alg != out->sig_alg) return LARD_TLS_ERR_CERT_VERIFY;
    uint32_t issuer_start = to;
    if (!der_read_tlv(tbs, tbs_len, &to, &tag, &val, &len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    out->issuer = tbs + issuer_start;
    out->issuer_len = to - issuer_start;
    const uint8_t* validity;
    uint32_t validity_len;
    if (!der_read_tlv(tbs, tbs_len, &to, &tag, &validity, &validity_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    int vr = parse_validity_window(validity, validity_len);
    if (vr != 0) return vr;
    const uint8_t* subject;
    uint32_t subject_len;
    uint32_t subject_start = to;
    if (!der_read_tlv(tbs, tbs_len, &to, &tag, &subject, &subject_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    out->subject = tbs + subject_start;
    out->subject_len = to - subject_start;
    const uint8_t* spki;
    uint32_t spki_len;
    if (!der_read_tlv(tbs, tbs_len, &to, &tag, &spki, &spki_len) || tag != 0x30) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    int r = parse_rsa_spki_key(&out->rsa, spki, spki_len);
    if (r == 0) out->has_rsa = 1;

    int cn_match = is_leaf ? parse_name_cn(subject, subject_len, host) : 0;
    int had_dns = 0;
    int san_match = 0;

    while (to < tbs_len) {
        if (!der_read_tlv(tbs, tbs_len, &to, &tag, &val, &len)) break;
        if (tag == 0xa3) parse_x509_extensions(out, val, len, host, is_leaf, &had_dns, &san_match);
    }
    if (is_leaf && ((had_dns && !san_match) || (!had_dns && !cn_match))) return LARD_TLS_ERR_CERT_VERIFY;
    if (!out->has_rsa) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    return 0;
}

static int cert_name_eq(const uint8_t* a, uint32_t alen, const uint8_t* b, uint32_t blen)
{
    return alen == blen && memcmp(a, b, alen) == 0;
}

static int cert_is_usable_ca(const x509_cert_t* cert)
{
    if (!cert->is_ca) return 0;
    if (cert->key_usage_present && !cert->key_cert_sign) return 0;
    return 1;
}

static int cert_digest(const x509_cert_t* cert, uint8_t* out, uint32_t* out_len,
                       const uint8_t** prefix, uint32_t* prefix_len)
{
    static const uint8_t sha1_prefix[] = {
        0x30,0x21,0x30,0x09,0x06,0x05,0x2b,0x0e,0x03,0x02,0x1a,0x05,0x00,0x04,0x14
    };
    static const uint8_t sha256_prefix[] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    static const uint8_t sha384_prefix[] = {
        0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30
    };
    if (cert->sig_alg == X509_SIG_RSA_SHA1) {
        sha1_state_t s;
        sha1_init(&s);
        sha1_update(&s, cert->tbs, cert->tbs_len);
        sha1_final(&s, out);
        *out_len = 20;
        *prefix = sha1_prefix;
        *prefix_len = sizeof(sha1_prefix);
        return 0;
    }
    if (cert->sig_alg == X509_SIG_RSA_SHA256) {
        lard_tls_sha256_state_t s;
        sha256_init(&s);
        sha256_update(&s, cert->tbs, cert->tbs_len);
        sha256_final_mut(&s, out);
        *out_len = 32;
        *prefix = sha256_prefix;
        *prefix_len = sizeof(sha256_prefix);
        return 0;
    }
    if (cert->sig_alg == X509_SIG_RSA_SHA384) {
        sha384_state_t s;
        sha384_init(&s);
        sha384_update(&s, cert->tbs, cert->tbs_len);
        sha384_final(&s, out);
        *out_len = 48;
        *prefix = sha384_prefix;
        *prefix_len = sizeof(sha384_prefix);
        return 0;
    }
    return LARD_TLS_ERR_UNSUPPORTED_CERT;
}

static int rsa_pkcs1_verify_cert(const uint8_t* mod, uint32_t mod_len,
                                 const uint8_t* exp, uint32_t exp_len,
                                 const x509_cert_t* cert)
{
    if (cert->signature_len > mod_len || mod_len > LARD_TLS_RSA_MAX_BYTES) return LARD_TLS_ERR_BAD_CERT_SIGNATURE;
    uint8_t sig[LARD_TLS_RSA_MAX_BYTES];
    uint8_t em[LARD_TLS_RSA_MAX_BYTES];
    uint8_t digest[48];
    uint32_t digest_len = 0;
    const uint8_t* prefix = NULL;
    uint32_t prefix_len = 0;
    memset(sig, 0, mod_len);
    memcpy(sig + (mod_len - cert->signature_len), cert->signature, cert->signature_len);
    int r = rsa_modexp(mod, mod_len, exp, exp_len, sig, em);
    if (r != 0) return r;
    if (em[0] != 0 || em[1] != 1) return LARD_TLS_ERR_BAD_CERT_SIGNATURE;
    uint32_t p = 2;
    while (p < mod_len && em[p] == 0xff) p++;
    if (p < 10u || p >= mod_len || em[p] != 0) return LARD_TLS_ERR_BAD_CERT_SIGNATURE;
    p++;
    r = cert_digest(cert, digest, &digest_len, &prefix, &prefix_len);
    if (r != 0) return r;
    if (p + prefix_len + digest_len != mod_len) return LARD_TLS_ERR_BAD_CERT_SIGNATURE;
    if (memcmp(em + p, prefix, prefix_len) != 0) return LARD_TLS_ERR_BAD_CERT_SIGNATURE;
    if (!ct_memeq(em + p + prefix_len, digest, digest_len)) return LARD_TLS_ERR_BAD_CERT_SIGNATURE;
    return 0;
}

static int verify_cert_with_cert(const x509_cert_t* cert, const x509_cert_t* issuer)
{
    if (!issuer->has_rsa) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    return rsa_pkcs1_verify_cert(issuer->rsa.modulus, issuer->rsa.modulus_len,
                                 issuer->rsa.exponent, issuer->rsa.exponent_len, cert);
}

static int anchor_matches_cert(const lard_tls_trust_anchor_t* a, const x509_cert_t* cert)
{
    if (!cert_name_eq(a->subject, a->subject_len, cert->subject, cert->subject_len)) return 0;
    if (a->modulus_len != cert->rsa.modulus_len || a->exponent_len != cert->rsa.exponent_len) return 0;
    if (memcmp(a->modulus, cert->rsa.modulus, a->modulus_len) != 0) return 0;
    if (memcmp(a->exponent, cert->rsa.exponent, a->exponent_len) != 0) return 0;
    return 1;
}

static int cert_matches_any_trust_anchor(const x509_cert_t* cert)
{
    for (uint32_t i = 0; i < LARD_TLS_TRUST_ANCHOR_COUNT; i++) {
        if (anchor_matches_cert(&lard_tls_trust_anchors[i], cert)) return 1;
    }
    return 0;
}

static int verify_cert_with_anchor(const x509_cert_t* cert, const lard_tls_trust_anchor_t* a)
{
    if (!cert_name_eq(cert->issuer, cert->issuer_len, a->subject, a->subject_len)) return LARD_TLS_ERR_UNTRUSTED_ROOT;
    return rsa_pkcs1_verify_cert(a->modulus, a->modulus_len, a->exponent, a->exponent_len, cert);
}

static int verify_x509_chain(lard_tls_client_t* c, x509_cert_t* certs, uint32_t cert_count)
{
    if (!cert_count || !certs[0].has_rsa) return LARD_TLS_ERR_UNSUPPORTED_CERT;
    for (uint32_t i = 1; i < cert_count; i++) {
        if (!cert_is_usable_ca(&certs[i]) && !cert_matches_any_trust_anchor(&certs[i])) {
            return LARD_TLS_ERR_CERT_VERIFY;
        }
    }
    for (uint32_t i = 0; i + 1u < cert_count; i++) {
        if (!cert_name_eq(certs[i].issuer, certs[i].issuer_len, certs[i + 1u].subject, certs[i + 1u].subject_len)) {
            return LARD_TLS_ERR_CERT_VERIFY;
        }
        int r = verify_cert_with_cert(&certs[i], &certs[i + 1u]);
        if (r != 0) return r;
    }

    x509_cert_t* last = &certs[cert_count - 1u];
    if (cert_count > 1u) {
        for (uint32_t i = 0; i < LARD_TLS_TRUST_ANCHOR_COUNT; i++) {
            if (anchor_matches_cert(&lard_tls_trust_anchors[i], last)) {
                memcpy(c->rsa_modulus, certs[0].rsa.modulus, certs[0].rsa.modulus_len);
                c->rsa_modulus_len = certs[0].rsa.modulus_len;
                memcpy(c->rsa_exponent, certs[0].rsa.exponent, certs[0].rsa.exponent_len);
                c->rsa_exponent_len = certs[0].rsa.exponent_len;
                c->cert_verified = 1;
                return 0;
            }
        }
    }
    for (uint32_t i = 0; i < LARD_TLS_TRUST_ANCHOR_COUNT; i++) {
        int r = verify_cert_with_anchor(last, &lard_tls_trust_anchors[i]);
        if (r == 0) {
            memcpy(c->rsa_modulus, certs[0].rsa.modulus, certs[0].rsa.modulus_len);
            c->rsa_modulus_len = certs[0].rsa.modulus_len;
            memcpy(c->rsa_exponent, certs[0].rsa.exponent, certs[0].rsa.exponent_len);
            c->rsa_exponent_len = certs[0].rsa.exponent_len;
            c->cert_verified = 1;
            return 0;
        }
        if (r != LARD_TLS_ERR_UNTRUSTED_ROOT) return r;
    }
    return LARD_TLS_ERR_UNTRUSTED_ROOT;
}

static uint32_t tls_mac_len(lard_tls_client_t* c)
{
    return c->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256 ? 32u : 20u;
}

static int tls_cipher_supported(uint16_t suite)
{
    return suite == TLS_RSA_WITH_AES_128_CBC_SHA || suite == TLS_RSA_WITH_AES_128_CBC_SHA256;
}

static void tls_record_mac(lard_tls_client_t* c, int sending, uint8_t type,
                           const uint8_t* data, uint32_t len, uint8_t* out)
{
    uint8_t hdr[13];
    const uint8_t* parts[2];
    uint32_t lens[2];
    uint64_t seq = sending ? c->client_seq : c->server_seq;
    store_be64(hdr, seq);
    hdr[8] = type;
    store_be16(hdr + 9, TLS12_VERSION);
    store_be16(hdr + 11, (uint16_t)len);
    parts[0] = hdr;
    lens[0] = sizeof(hdr);
    parts[1] = data;
    lens[1] = len;
    if (c->cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
        hmac_sha256_parts(sending ? c->client_write_mac : c->server_write_mac, 32, parts, lens, 2, out);
    } else {
        hmac_sha1_parts(sending ? c->client_write_mac : c->server_write_mac, 20, parts, lens, 2, out);
    }
}

static int derive_keys(lard_tls_client_t* c)
{
    uint8_t seed[64];
    uint8_t key_block[96];
    uint32_t mac_len = tls_mac_len(c);
    uint32_t need = mac_len * 2u + 32u;
    memcpy(seed, c->server_random, 32);
    memcpy(seed + 32, c->client_random, 32);
    int r = tls_prf_sha256(c->master_secret, sizeof(c->master_secret), "key expansion", seed, sizeof(seed), key_block, need);
    if (r != 0) return r;
    uint32_t p = 0;
    memcpy(c->client_write_mac, key_block + p, mac_len); p += mac_len;
    memcpy(c->server_write_mac, key_block + p, mac_len); p += mac_len;
    memcpy(c->client_write_key, key_block + p, 16); p += 16;
    memcpy(c->server_write_key, key_block + p, 16);
    return 0;
}

int lard_tls_client_init(lard_tls_client_t* c,
                         const char* server_name,
                         void* io,
                         lard_tls_send_fn send,
                         lard_tls_recv_fn recv)
{
    if (!c || !send || !recv) return LARD_TLS_ERR_BAD_ARG;
    memset(c, 0, sizeof(*c));
    c->io = io;
    c->send = send;
    c->recv = recv;
    copy_server_name(c->server_name, sizeof(c->server_name), server_name);
    fill_random_bytes(c->client_random, sizeof(c->client_random));
    c->protocol_version = TLS12_VERSION;
    sha256_init(&c->transcript_hash);
    return 0;
}

static int build_client_hello(lard_tls_client_t* c, uint8_t* out, uint32_t cap, uint32_t* out_len)
{
    uint32_t p = 5;
    uint32_t handshake_start;
    uint32_t handshake_len_pos;
    uint32_t ext_len_pos;
    uint32_t ext_start;
    uint32_t name_len = (uint32_t)strlen(c->server_name);
    const uint16_t suites[] = { TLS_RSA_WITH_AES_128_CBC_SHA, TLS_RSA_WITH_AES_128_CBC_SHA256, 0x00FFu };
    const uint16_t sigs[] = { 0x0401u, 0x0501u, 0x0201u };

    if (!out || !out_len || name_len == 0 || name_len > 253u) return LARD_TLS_ERR_BAD_ARG;

    out[0] = TLS_CT_HANDSHAKE;
    out[1] = 0x03;
    out[2] = 0x03;
    out[3] = 0;
    out[4] = 0;

    handshake_start = p;
    if (put_u8(out, cap, &p, 1) != 0) return LARD_TLS_ERR_OVERFLOW;
    handshake_len_pos = p;
    if (put_u24(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, TLS12_VERSION) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_bytes(out, cap, &p, c->client_random, sizeof(c->client_random)) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;

    if (put_u16(out, cap, &p, (uint16_t)(sizeof(suites))) != 0) return LARD_TLS_ERR_OVERFLOW;
    for (uint32_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (put_u16(out, cap, &p, suites[i]) != 0) return LARD_TLS_ERR_OVERFLOW;
    }

    if (put_u8(out, cap, &p, 1) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;

    ext_len_pos = p;
    if (put_u16(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;
    ext_start = p;

    if (put_u16(out, cap, &p, 0x0000u) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)(5u + name_len)) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)(3u + name_len)) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)name_len) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_bytes(out, cap, &p, (const uint8_t*)c->server_name, name_len) != 0) return LARD_TLS_ERR_OVERFLOW;

    if (put_u16(out, cap, &p, 0x000Du) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)(2u + sizeof(sigs))) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)sizeof(sigs)) != 0) return LARD_TLS_ERR_OVERFLOW;
    for (uint32_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        if (put_u16(out, cap, &p, sigs[i]) != 0) return LARD_TLS_ERR_OVERFLOW;
    }

    if (put_u16(out, cap, &p, 0xFF01u) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 1) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;

    if (p - ext_start > 0xFFFFu) return LARD_TLS_ERR_OVERFLOW;
    store_be16(out + ext_len_pos, (uint16_t)(p - ext_start));

    if (put_u24_at(out, cap, handshake_len_pos, p - handshake_start - 4u) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (p - 5u > 0xFFFFu) return LARD_TLS_ERR_OVERFLOW;
    store_be16(out + 3, (uint16_t)(p - 5u));
    *out_len = p;
    return 0;
}

static int read_exact(lard_tls_client_t* c, uint8_t* out, uint32_t len)
{
    uint32_t got = 0;
    uint32_t idle = 0;
    while (got < len && idle < 200000u) {
        uint32_t n = 0;
        int r = c->recv(c->io, out + got, len - got, &n);
        if (r != 0) return LARD_TLS_ERR_IO;
        if (n == 0) {
            idle++;
            continue;
        }
        got += n;
        idle = 0;
    }
    return got == len ? 0 : LARD_TLS_ERR_IO;
}

static int read_record(lard_tls_client_t* c, uint8_t* type, uint8_t* body, uint32_t cap, uint32_t* out_len)
{
    uint8_t hdr[5];
    int r = read_exact(c, hdr, sizeof(hdr));
    if (r != 0) return r;
    *type = hdr[0];
    uint16_t ver = load_be16(hdr + 1);
    uint16_t len = load_be16(hdr + 3);
    if (ver < 0x0301u || ver > TLS12_VERSION || len > cap || len > TLS_RECORD_BODY_MAX) {
        return LARD_TLS_ERR_BAD_RECORD;
    }
    r = read_exact(c, body, len);
    if (r != 0) return r;
    *out_len = len;
    return 0;
}

static int read_handshake_bytes(lard_tls_client_t* c, uint8_t* out, uint32_t len)
{
    uint32_t copied = 0;
    while (copied < len) {
        if (c->pending_record_pos >= c->pending_record_len) {
            uint8_t type = 0;
            uint32_t body_len = 0;
            int r = read_record(c, &type, g_tls_record, TLS_RECORD_BODY_MAX, &body_len);
            if (r != 0) return r;
            if (type == TLS_CT_ALERT) return LARD_TLS_ERR_ALERT;
            if (type != TLS_CT_HANDSHAKE) return LARD_TLS_ERR_UNEXPECTED;
            c->pending_record_pos = 0;
            c->pending_record_len = body_len;
        }
        uint32_t avail = c->pending_record_len - c->pending_record_pos;
        uint32_t n = len - copied;
        if (n > avail) n = avail;
        memcpy(out + copied, g_tls_record + c->pending_record_pos, n);
        c->pending_record_pos += n;
        copied += n;
    }
    return 0;
}

static int read_handshake_msg(lard_tls_client_t* c, uint8_t* hs_type, uint8_t* body,
                              uint32_t cap, uint32_t* body_len, int transcript)
{
    uint8_t hdr[4];
    int r = read_handshake_bytes(c, hdr, sizeof(hdr));
    if (r != 0) return r;
    uint32_t len = load_be24(hdr + 1);
    if (len > cap) return LARD_TLS_ERR_OVERFLOW;
    r = read_handshake_bytes(c, body, len);
    if (r != 0) return r;
    *hs_type = hdr[0];
    *body_len = len;
    if (transcript) {
        sha256_update(&c->transcript_hash, hdr, sizeof(hdr));
        sha256_update(&c->transcript_hash, body, len);
    }
    return 0;
}

static int parse_server_hello(lard_tls_client_t* c, const uint8_t* body, uint32_t len)
{
    if (len < 38u) return LARD_TLS_ERR_BAD_RECORD;
    uint32_t p = 0;
    c->protocol_version = load_be16(body + p);
    p += 2;
    if (c->protocol_version != TLS12_VERSION) return LARD_TLS_ERR_UNSUPPORTED_CIPHER;
    memcpy(c->server_random, body + p, sizeof(c->server_random));
    p += 32;
    if (p >= len) return LARD_TLS_ERR_BAD_RECORD;
    uint8_t session_len = body[p++];
    if (p + session_len + 3u > len) return LARD_TLS_ERR_BAD_RECORD;
    p += session_len;
    c->cipher_suite = load_be16(body + p);
    p += 2;
    if (!tls_cipher_supported(c->cipher_suite)) return LARD_TLS_ERR_UNSUPPORTED_CIPHER;
    if (body[p] != 0) return LARD_TLS_ERR_UNEXPECTED;
    c->got_server_hello = 1;
    return 0;
}

static int parse_certificate_msg(lard_tls_client_t* c, const uint8_t* body, uint32_t len)
{
    if (len < 6u) return LARD_TLS_ERR_BAD_RECORD;
    uint32_t list_len = load_be24(body);
    if (list_len + 3u > len || list_len < 3u) return LARD_TLS_ERR_BAD_RECORD;
    uint32_t p = 3;
    uint32_t end = 3u + list_len;
    uint32_t cert_count = 0;
    while (p < end) {
        if (p + 3u > end) return LARD_TLS_ERR_BAD_RECORD;
        uint32_t cert_len = load_be24(body + p);
        p += 3;
        if (cert_len == 0 || p + cert_len > end) return LARD_TLS_ERR_BAD_RECORD;
        if (cert_count >= sizeof(g_tls_certs) / sizeof(g_tls_certs[0])) return LARD_TLS_ERR_UNSUPPORTED_CERT;
        int r = parse_x509_cert(&g_tls_certs[cert_count], body + p, cert_len, c->server_name, cert_count == 0);
        if (r != 0) return r;
        cert_count++;
        p += cert_len;
    }
    return verify_x509_chain(c, g_tls_certs, cert_count);
}

static int build_client_key_exchange(lard_tls_client_t* c, uint8_t* out, uint32_t cap, uint32_t* out_len)
{
    if (!c->cert_verified || !c->rsa_modulus_len) return LARD_TLS_ERR_CERT_VERIFY;
    uint8_t enc[LARD_TLS_RSA_MAX_BYTES];
    c->pre_master[0] = 0x03;
    c->pre_master[1] = 0x03;
    fill_random_bytes(c->pre_master + 2, sizeof(c->pre_master) - 2u);
    int r = rsa_pkcs1_encrypt(c, c->pre_master, sizeof(c->pre_master), enc);
    if (r != 0) return r;
    uint32_t p = 0;
    if (put_u8(out, cap, &p, TLS_HS_CLIENT_KEY_EXCHANGE) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u24(out, cap, &p, 2u + c->rsa_modulus_len) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, c->rsa_modulus_len) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_bytes(out, cap, &p, enc, c->rsa_modulus_len) != 0) return LARD_TLS_ERR_OVERFLOW;
    *out_len = p;
    return 0;
}

static int send_plain_handshake(lard_tls_client_t* c, const uint8_t* hs, uint32_t hs_len)
{
    if (hs_len + 5u > TLS_RECORD_WIRE_MAX || hs_len > 0xFFFFu) return LARD_TLS_ERR_OVERFLOW;
    g_tls_record[0] = TLS_CT_HANDSHAKE;
    g_tls_record[1] = 0x03;
    g_tls_record[2] = 0x03;
    store_be16(g_tls_record + 3, (uint16_t)hs_len);
    memcpy(g_tls_record + 5, hs, hs_len);
    int r = c->send(c->io, g_tls_record, hs_len + 5u);
    if (r != 0) return LARD_TLS_ERR_IO;
    sha256_update(&c->transcript_hash, hs, hs_len);
    return 0;
}

static int send_change_cipher(lard_tls_client_t* c)
{
    uint8_t rec[6] = { TLS_CT_CHANGE_CIPHER, 0x03, 0x03, 0x00, 0x01, 0x01 };
    int r = c->send(c->io, rec, sizeof(rec));
    if (r != 0) return LARD_TLS_ERR_IO;
    c->write_encrypted = 1;
    return 0;
}

static int send_encrypted_record(lard_tls_client_t* c, uint8_t type, const uint8_t* data, uint32_t len)
{
    if (!c->write_encrypted) return LARD_TLS_ERR_UNEXPECTED;
    uint32_t mac_len = tls_mac_len(c);
    uint8_t mac[32];
    if (len + mac_len + 256u > TLS_RECORD_BODY_MAX) return LARD_TLS_ERR_OVERFLOW;
    tls_record_mac(c, 1, type, data, len, mac);
    memcpy(g_tls_plain, data, len);
    memcpy(g_tls_plain + len, mac, mac_len);
    uint32_t base = len + mac_len;
    uint8_t pad_len = (uint8_t)(15u - (base & 15u));
    for (uint32_t i = 0; i <= pad_len; i++) g_tls_plain[base + i] = pad_len;
    uint32_t enc_len = base + (uint32_t)pad_len + 1u;
    uint32_t body_len = TLS_AES_BLOCK + enc_len;
    if (body_len + 5u > TLS_RECORD_WIRE_MAX) return LARD_TLS_ERR_OVERFLOW;
    g_tls_record[0] = type;
    g_tls_record[1] = 0x03;
    g_tls_record[2] = 0x03;
    store_be16(g_tls_record + 3, (uint16_t)body_len);
    fill_random_bytes(g_tls_record + 5, TLS_AES_BLOCK);
    aes128_cbc_encrypt(c->client_write_key, g_tls_record + 5, g_tls_plain, g_tls_record + 5u + TLS_AES_BLOCK, enc_len);
    int r = c->send(c->io, g_tls_record, body_len + 5u);
    if (r != 0) return LARD_TLS_ERR_IO;
    c->client_seq++;
    return 0;
}

static int read_encrypted_record(lard_tls_client_t* c, uint8_t* out_type, uint8_t* out, uint32_t cap, uint32_t* out_len)
{
    uint8_t type = 0;
    uint32_t body_len = 0;
    int r = read_record(c, &type, g_tls_record, TLS_RECORD_BODY_MAX, &body_len);
    if (r != 0) return r;
    if (!c->read_encrypted) return LARD_TLS_ERR_UNEXPECTED;
    if (body_len < TLS_AES_BLOCK * 2u || ((body_len - TLS_AES_BLOCK) & 15u) != 0) return LARD_TLS_ERR_BAD_RECORD;
    uint32_t enc_len = body_len - TLS_AES_BLOCK;
    aes128_cbc_decrypt(c->server_write_key, g_tls_record, g_tls_record + TLS_AES_BLOCK, g_tls_plain, enc_len);
    uint8_t pad_len = g_tls_plain[enc_len - 1u];
    uint32_t pad_count = (uint32_t)pad_len + 1u;
    if (pad_count > enc_len) return LARD_TLS_ERR_DECRYPT;
    for (uint32_t i = 0; i < pad_count; i++) {
        if (g_tls_plain[enc_len - 1u - i] != pad_len) return LARD_TLS_ERR_DECRYPT;
    }
    uint32_t mac_len = tls_mac_len(c);
    if (enc_len < pad_count + mac_len) return LARD_TLS_ERR_DECRYPT;
    uint32_t content_len = enc_len - pad_count - mac_len;
    uint8_t got_mac[32];
    uint8_t want_mac[32];
    memcpy(got_mac, g_tls_plain + content_len, mac_len);
    tls_record_mac(c, 0, type, g_tls_plain, content_len, want_mac);
    if (!ct_memeq(got_mac, want_mac, mac_len)) return LARD_TLS_ERR_DECRYPT;
    c->server_seq++;
    if (content_len > cap) return LARD_TLS_ERR_OVERFLOW;
    memcpy(out, g_tls_plain, content_len);
    *out_type = type;
    *out_len = content_len;
    return 0;
}

static int send_finished(lard_tls_client_t* c)
{
    uint8_t digest[32];
    uint8_t verify[12];
    uint8_t hs[16];
    sha256_finish_copy(&c->transcript_hash, digest);
    int r = tls_prf_sha256(c->master_secret, sizeof(c->master_secret), "client finished", digest, sizeof(digest), verify, sizeof(verify));
    if (r != 0) return r;
    hs[0] = TLS_HS_FINISHED;
    store_be24(hs + 1, sizeof(verify));
    memcpy(hs + 4, verify, sizeof(verify));
    r = send_encrypted_record(c, TLS_CT_HANDSHAKE, hs, sizeof(hs));
    if (r != 0) return r;
    sha256_update(&c->transcript_hash, hs, sizeof(hs));
    return 0;
}

static int read_server_finished(lard_tls_client_t* c)
{
    uint8_t type = 0;
    uint32_t len = 0;
    int r = read_record(c, &type, g_tls_record, TLS_RECORD_BODY_MAX, &len);
    if (r != 0) return r;
    if (type == TLS_CT_ALERT) return LARD_TLS_ERR_ALERT;
    if (type != TLS_CT_CHANGE_CIPHER || len != 1u || g_tls_record[0] != 1) return LARD_TLS_ERR_UNEXPECTED;
    c->read_encrypted = 1;

    r = read_encrypted_record(c, &type, g_tls_plain, TLS_RECORD_BODY_MAX, &len);
    if (r != 0) return r;
    if (type != TLS_CT_HANDSHAKE || len != 16u || g_tls_plain[0] != TLS_HS_FINISHED || load_be24(g_tls_plain + 1) != 12u) {
        return LARD_TLS_ERR_BAD_FINISHED;
    }
    uint8_t digest[32];
    uint8_t verify[12];
    sha256_finish_copy(&c->transcript_hash, digest);
    r = tls_prf_sha256(c->master_secret, sizeof(c->master_secret), "server finished", digest, sizeof(digest), verify, sizeof(verify));
    if (r != 0) return r;
    if (!ct_memeq(verify, g_tls_plain + 4, sizeof(verify))) return LARD_TLS_ERR_BAD_FINISHED;
    sha256_update(&c->transcript_hash, g_tls_plain, len);
    return 0;
}

int lard_tls_client_handshake(lard_tls_client_t* c)
{
    uint32_t rec_len = 0;
    uint8_t hs_type = 0;
    uint32_t body_len = 0;
    int r;

    if (!c || !c->send || !c->recv) return LARD_TLS_ERR_BAD_ARG;
    r = build_client_hello(c, g_tls_record, TLS_RECORD_WIRE_MAX, &rec_len);
    if (r != 0) return r;
    r = c->send(c->io, g_tls_record, rec_len);
    if (r != 0) return LARD_TLS_ERR_IO;
    sha256_update(&c->transcript_hash, g_tls_record + 5, rec_len - 5u);

    r = read_handshake_msg(c, &hs_type, g_tls_plain, TLS_RECORD_BODY_MAX, &body_len, 1);
    if (r != 0) return r;
    if (hs_type != TLS_HS_SERVER_HELLO) return LARD_TLS_ERR_UNEXPECTED;
    r = parse_server_hello(c, g_tls_plain, body_len);
    if (r != 0) return r;

    int got_cert = 0;
    for (;;) {
        r = read_handshake_msg(c, &hs_type, g_tls_plain, TLS_RECORD_BODY_MAX, &body_len, 1);
        if (r != 0) return r;
        if (hs_type == TLS_HS_CERTIFICATE) {
            r = parse_certificate_msg(c, g_tls_plain, body_len);
            if (r != 0) return r;
            got_cert = 1;
            continue;
        }
        if (hs_type == TLS_HS_SERVER_HELLO_DONE) break;
        if (hs_type == TLS_HS_SERVER_KEY_EXCHANGE || hs_type == TLS_HS_CERTIFICATE_REQUEST) {
            return LARD_TLS_ERR_UNSUPPORTED_CIPHER;
        }
        return LARD_TLS_ERR_UNEXPECTED;
    }
    if (!got_cert || !c->cert_verified) return LARD_TLS_ERR_CERT_VERIFY;

    r = build_client_key_exchange(c, g_tls_plain, TLS_RECORD_BODY_MAX, &body_len);
    if (r != 0) return r;
    r = send_plain_handshake(c, g_tls_plain, body_len);
    if (r != 0) return r;

    uint8_t seed[64];
    memcpy(seed, c->client_random, 32);
    memcpy(seed + 32, c->server_random, 32);
    r = tls_prf_sha256(c->pre_master, sizeof(c->pre_master), "master secret", seed, sizeof(seed), c->master_secret, sizeof(c->master_secret));
    if (r != 0) return r;
    r = derive_keys(c);
    if (r != 0) return r;

    r = send_change_cipher(c);
    if (r != 0) return r;
    r = send_finished(c);
    if (r != 0) return r;
    r = read_server_finished(c);
    if (r != 0) return r;

    c->handshake_done = 1;
    return 0;
}

int lard_tls_write(lard_tls_client_t* c, const uint8_t* data, uint32_t len)
{
    if (!c || !data) return LARD_TLS_ERR_BAD_ARG;
    if (!c->handshake_done) return LARD_TLS_ERR_UNEXPECTED;
    if (len == 0) return 0;
    uint32_t n = len;
    if (n > 2048u) n = 2048u;
    int r = send_encrypted_record(c, TLS_CT_APP_DATA, data, n);
    if (r != 0) return r;
    return (int)n;
}

int lard_tls_read(lard_tls_client_t* c, uint8_t* data, uint32_t cap, uint32_t* out_len)
{
    if (!c || !data || !out_len) return LARD_TLS_ERR_BAD_ARG;
    *out_len = 0;
    if (!c->handshake_done) return LARD_TLS_ERR_UNEXPECTED;
    if (cap == 0) return 0;
    if (c->rx_plain_pos < c->rx_plain_len) {
        uint32_t n = c->rx_plain_len - c->rx_plain_pos;
        if (n > cap) n = cap;
        memcpy(data, g_tls_app_plain + c->rx_plain_pos, n);
        c->rx_plain_pos += n;
        *out_len = n;
        return 0;
    }
    for (;;) {
        uint8_t type = 0;
        uint32_t len = 0;
        int r = read_encrypted_record(c, &type, g_tls_app_plain, TLS_RECORD_BODY_MAX, &len);
        if (r != 0) return r;
        if (type == TLS_CT_ALERT) return LARD_TLS_ERR_ALERT;
        if (type == TLS_CT_HANDSHAKE) continue;
        if (type != TLS_CT_APP_DATA) return LARD_TLS_ERR_UNEXPECTED;
        c->rx_plain_pos = 0;
        c->rx_plain_len = len;
        if (len == 0) return 0;
        uint32_t n = len;
        if (n > cap) n = cap;
        memcpy(data, g_tls_app_plain, n);
        c->rx_plain_pos = n;
        *out_len = n;
        return 0;
    }
}

const char* lard_tls_cipher_name(uint16_t suite)
{
    if (suite == TLS_RSA_WITH_AES_128_CBC_SHA) return "TLS_RSA_WITH_AES_128_CBC_SHA";
    if (suite == TLS_RSA_WITH_AES_128_CBC_SHA256) return "TLS_RSA_WITH_AES_128_CBC_SHA256";
    return "unsupported";
}

void lard_tls_info(lard_tls_info_t* out)
{
    if (!out) return;
    out->trust_anchors = LARD_TLS_TRUST_ANCHOR_COUNT;
    out->supported_ciphers = 2u;
    out->rsa_max_bytes = LARD_TLS_RSA_MAX_BYTES;
    out->sni_max = 127u;
}

static int tls_selftest_send(void* io, const uint8_t* data, uint32_t len)
{
    (void)io;
    (void)data;
    return (int)len;
}

static int tls_selftest_recv(void* io, uint8_t* data, uint32_t cap, uint32_t* out_len)
{
    (void)io;
    (void)data;
    (void)cap;
    if (out_len) *out_len = 0;
    return 0;
}

int lard_tls_selftest(void)
{
    lard_tls_client_t c;
    lard_tls_info_t info;
    int r = lard_tls_client_init(&c, "example.com", NULL, tls_selftest_send, tls_selftest_recv);
    if (r != 0) return -1;
    if (strcmp(c.server_name, "example.com") != 0) return -2;
    if (c.protocol_version != TLS12_VERSION) return -3;
    if (!tls_cipher_supported(TLS_RSA_WITH_AES_128_CBC_SHA)) return -4;
    if (!tls_cipher_supported(TLS_RSA_WITH_AES_128_CBC_SHA256)) return -5;
    if (tls_cipher_supported(0x1301u)) return -6;
    lard_tls_info(&info);
    if (info.trust_anchors == 0u || info.supported_ciphers != 2u) return -7;
    if (info.rsa_max_bytes != LARD_TLS_RSA_MAX_BYTES || info.sni_max != 127u) return -8;
    if (lard_tls_status_text(LARD_TLS_OK)[0] == '\0') return -9;
    if (lard_tls_cipher_name(TLS_RSA_WITH_AES_128_CBC_SHA256)[0] == 'u') return -10;
    return 0;
}

const char* lard_tls_status_text(int code)
{
    switch (code) {
    case LARD_TLS_OK:
        return "ok";
    case LARD_TLS_ERR_BAD_ARG:
        return "bad argument";
    case LARD_TLS_ERR_OVERFLOW:
        return "buffer overflow";
    case LARD_TLS_ERR_IO:
        return "network I/O failed";
    case LARD_TLS_ERR_BAD_RECORD:
        return "bad TLS record";
    case LARD_TLS_ERR_ALERT:
        return "peer sent TLS alert";
    case LARD_TLS_ERR_UNEXPECTED:
        return "unexpected TLS message";
    case LARD_TLS_ERR_UNSUPPORTED_CIPHER:
        return "TLS cipher or handshake mode is not supported by native RSA/AES-CBC path";
    case LARD_TLS_ERR_UNSUPPORTED_CERT:
        return "certificate key format is not supported";
    case LARD_TLS_ERR_CERT_VERIFY:
        return "certificate leaf identity or validity validation failed";
    case LARD_TLS_ERR_DECRYPT:
        return "TLS record authentication or decryption failed";
    case LARD_TLS_ERR_BAD_FINISHED:
        return "TLS Finished verification failed";
    case LARD_TLS_ERR_UNTRUSTED_ROOT:
        return "certificate chain does not end at a native trusted root";
    case LARD_TLS_ERR_BAD_CERT_SIGNATURE:
        return "certificate chain signature verification failed";
    default:
        return "unknown TLS error";
    }
}
