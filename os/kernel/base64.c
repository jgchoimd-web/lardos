/*
 * libbase64 - Minimal Base64 encode/decode. Freestanding, no alloc.
 */
#include "base64.h"

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t base64_encode(const uint8_t* in, uint32_t len, char* out)
{
    uint32_t j = 0;
    for (uint32_t i = 0; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i+1] << 8 | in[i+2];
        out[j++] = b64_chars[(v >> 18) & 63];
        out[j++] = b64_chars[(v >> 12) & 63];
        out[j++] = b64_chars[(v >> 6) & 63];
        out[j++] = b64_chars[v & 63];
    }
    uint32_t rem = len % 3;
    if (rem == 0) {
        out[j] = '\0';
        return j;
    }
    uint32_t v = (uint32_t)in[len - rem] << 16;
    if (rem == 2) v |= (uint32_t)in[len-1] << 8;
    out[j++] = b64_chars[(v >> 18) & 63];
    out[j++] = b64_chars[(v >> 12) & 63];
    out[j++] = (rem == 2) ? b64_chars[(v >> 6) & 63] : '=';
    out[j++] = '=';
    out[j] = '\0';
    return j;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

uint32_t base64_decode(const char* in, uint32_t len, uint8_t* out)
{
    uint32_t j = 0;
    uint32_t i = 0;
    while (i + 4 <= len) {
        int a = b64_val(in[i++]);
        int b = b64_val(in[i++]);
        int c = (in[i] == '=') ? -1 : b64_val(in[i]);
        int d = (in[i+1] == '=') ? -1 : b64_val(in[i+1]);
        i += 2;
        if (a < 0 || b < 0) return (uint32_t)-1;
        uint32_t v = (a << 18) | (b << 12) | ((c < 0 ? 0 : c) << 6) | (d < 0 ? 0 : d);
        out[j++] = (uint8_t)(v >> 16);
        if (c >= 0) out[j++] = (uint8_t)(v >> 8);
        if (d >= 0) out[j++] = (uint8_t)v;
    }
    return j;
}
