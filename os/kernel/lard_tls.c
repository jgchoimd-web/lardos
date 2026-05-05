#include "lard_tls.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

#define TLS_CT_ALERT     21
#define TLS_CT_HANDSHAKE 22
#define TLS12_VERSION    0x0303u
#define TLS_MAX_RECORD   2048u

static uint16_t load_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t load_be24(const uint8_t* p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static int put_u8(uint8_t* b, uint32_t cap, uint32_t* p, uint8_t v)
{
    if (*p >= cap) return LARD_TLS_ERR_OVERFLOW;
    b[(*p)++] = v;
    return 0;
}

static int put_u16(uint8_t* b, uint32_t cap, uint32_t* p, uint16_t v)
{
    if (*p + 2 > cap) return LARD_TLS_ERR_OVERFLOW;
    b[(*p)++] = (uint8_t)(v >> 8);
    b[(*p)++] = (uint8_t)v;
    return 0;
}

static int put_u24_at(uint8_t* b, uint32_t cap, uint32_t p, uint32_t v)
{
    if (p + 3 > cap || v > 0xFFFFFFu) return LARD_TLS_ERR_OVERFLOW;
    b[p + 0] = (uint8_t)(v >> 16);
    b[p + 1] = (uint8_t)(v >> 8);
    b[p + 2] = (uint8_t)v;
    return 0;
}

static int put_bytes(uint8_t* b, uint32_t cap, uint32_t* p, const uint8_t* s, uint32_t n)
{
    if (*p + n > cap) return LARD_TLS_ERR_OVERFLOW;
    for (uint32_t i = 0; i < n; i++) b[(*p)++] = s[i];
    return 0;
}

static uint64_t rdtsc64(void)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void fill_random(uint8_t out[32])
{
    uint64_t x = rdtsc64() ^ 0x4c415244544c5331ull;
    for (uint32_t i = 0; i < 32; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out[i] = (uint8_t)(x >> ((i & 7u) * 8u));
        x += rdtsc64() + (uint64_t)i * 0x9e3779b97f4a7c15ull;
    }
}

static uint32_t copy_server_name(char* out, uint32_t cap, const char* in)
{
    uint32_t i = 0;
    if (!cap) return 0;
    while (in && in[i] && in[i] != ':' && i + 1 < cap) {
        out[i] = in[i];
        i++;
    }
    out[i] = '\0';
    return i;
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
    fill_random(c->client_random);
    c->protocol_version = TLS12_VERSION;
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
    const uint16_t suites[] = { 0xC02Fu, 0xC02Bu, 0x009Cu, 0x003Cu };
    const uint16_t sigs[] = { 0x0403u, 0x0804u, 0x0401u, 0x0501u, 0x0601u };

    if (!out || !out_len || name_len == 0 || name_len > 253u) return LARD_TLS_ERR_BAD_ARG;

    out[0] = TLS_CT_HANDSHAKE;
    out[1] = 0x03;
    out[2] = 0x03;
    out[3] = 0;
    out[4] = 0;

    handshake_start = p;
    if (put_u8(out, cap, &p, 1) != 0) return LARD_TLS_ERR_OVERFLOW;
    handshake_len_pos = p;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;
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

    if (put_u16(out, cap, &p, 0x000Au) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 8) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 6) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 0x001Du) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 0x0017u) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 0x0018u) != 0) return LARD_TLS_ERR_OVERFLOW;

    if (put_u16(out, cap, &p, 0x000Bu) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, 2) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 1) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u8(out, cap, &p, 0) != 0) return LARD_TLS_ERR_OVERFLOW;

    if (put_u16(out, cap, &p, 0x000Du) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)(2u + sizeof(sigs))) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (put_u16(out, cap, &p, (uint16_t)sizeof(sigs)) != 0) return LARD_TLS_ERR_OVERFLOW;
    for (uint32_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        if (put_u16(out, cap, &p, sigs[i]) != 0) return LARD_TLS_ERR_OVERFLOW;
    }

    if (p - ext_start > 0xFFFFu) return LARD_TLS_ERR_OVERFLOW;
    out[ext_len_pos + 0] = (uint8_t)((p - ext_start) >> 8);
    out[ext_len_pos + 1] = (uint8_t)(p - ext_start);

    if (put_u24_at(out, cap, handshake_len_pos, p - handshake_start - 4u) != 0) return LARD_TLS_ERR_OVERFLOW;
    if (p - 5u > 0xFFFFu) return LARD_TLS_ERR_OVERFLOW;
    out[3] = (uint8_t)((p - 5u) >> 8);
    out[4] = (uint8_t)(p - 5u);
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
    if (ver < 0x0301u || ver > TLS12_VERSION || len > cap || len > TLS_MAX_RECORD) {
        return LARD_TLS_ERR_BAD_RECORD;
    }
    r = read_exact(c, body, len);
    if (r != 0) return r;
    *out_len = len;
    return 0;
}

static int parse_server_hello(lard_tls_client_t* c, const uint8_t* body, uint32_t len)
{
    if (len < 42u || body[0] != 2) return LARD_TLS_ERR_UNEXPECTED;
    uint32_t hs_len = load_be24(body + 1);
    if (hs_len + 4u > len || hs_len < 38u) return LARD_TLS_ERR_BAD_RECORD;
    uint32_t p = 4;
    c->protocol_version = load_be16(body + p);
    p += 2;
    for (uint32_t i = 0; i < 32u; i++) c->server_random[i] = body[p + i];
    p += 32;
    if (p >= len) return LARD_TLS_ERR_BAD_RECORD;
    uint8_t session_len = body[p++];
    if (p + session_len + 3u > len) return LARD_TLS_ERR_BAD_RECORD;
    p += session_len;
    c->cipher_suite = load_be16(body + p);
    p += 2;
    if (body[p] != 0) return LARD_TLS_ERR_UNEXPECTED;
    c->got_server_hello = 1;
    return 0;
}

int lard_tls_client_handshake(lard_tls_client_t* c)
{
    uint8_t rec[768];
    uint32_t rec_len = 0;
    uint8_t body[TLS_MAX_RECORD];
    uint8_t type = 0;
    uint32_t body_len = 0;
    int r;

    if (!c || !c->send || !c->recv) return LARD_TLS_ERR_BAD_ARG;
    r = build_client_hello(c, rec, sizeof(rec), &rec_len);
    if (r != 0) return r;
    r = c->send(c->io, rec, rec_len);
    if (r != 0) return LARD_TLS_ERR_IO;

    r = read_record(c, &type, body, sizeof(body), &body_len);
    if (r != 0) return r;
    if (type == TLS_CT_ALERT) return LARD_TLS_ERR_ALERT;
    if (type != TLS_CT_HANDSHAKE) return LARD_TLS_ERR_UNEXPECTED;

    r = parse_server_hello(c, body, body_len);
    if (r != 0) return r;

    /*
     * External TLS has deliberately been removed. The native path can now
     * speak the first TLS record and parse ServerHello; key schedule,
     * certificate validation, and encrypted records are the next owned pieces.
     */
    return LARD_TLS_ERR_CRYPTO_TODO;
}

int lard_tls_write(lard_tls_client_t* c, const uint8_t* data, uint32_t len)
{
    (void)c;
    (void)data;
    (void)len;
    return LARD_TLS_ERR_CRYPTO_TODO;
}

int lard_tls_read(lard_tls_client_t* c, uint8_t* data, uint32_t cap, uint32_t* out_len)
{
    (void)c;
    (void)data;
    (void)cap;
    if (out_len) *out_len = 0;
    return LARD_TLS_ERR_CRYPTO_TODO;
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
    case LARD_TLS_ERR_CRYPTO_TODO:
        return "native TLS crypto is not finished";
    default:
        return "unknown TLS error";
    }
}
