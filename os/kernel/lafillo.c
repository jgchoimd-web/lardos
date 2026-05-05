/**
 * lafillo.c - LardOS 최적화 브라우저 엔진
 *
 * Dillo 기반 제거, OS 최적화:
 * - 외부 의존성 없음 (third_party 제거)
 * - 스택/정적 버퍼만 사용 (kmalloc 없음)
 * - 최소 엔티티 테이블 (amp, lt, gt, quot, nbsp, apos)
 * - 블록 요소: br, div, p, h1-h6, li, table, tr, td, th
 */
#include "lafillo.h"
#include <stddef.h>
#include <stdint.h>

/* 공통 엔티티 (OS 최소화) */
static const struct { const char* name; const char* repl; } s_entities[] = {
    {"amp", "&"}, {"lt", "<"}, {"gt", ">"},
    {"quot", "\""}, {"nbsp", " "}, {"apos", "'"},
    {"AMP", "&"}, {"LT", "<"}, {"GT", ">"},
    {"QUOT", "\""},
};
#define N_ENT 10

static int is_block_tag(const char* tag, uint32_t len)
{
    if (len < 1 || len > 10) return 0;
    switch (tag[0]) {
        case 'b': return len == 2 && tag[1] == 'r';
        case 'd': return (len == 3 && tag[1] == 'i' && tag[2] == 'v') || (len == 2 && tag[1] == 'l');
        case 'h': return len >= 2 && tag[1] >= '1' && tag[1] <= '6';
        case 'p': return len == 1;
        case 'l': return len == 2 && tag[1] == 'i';
        case 't': return (len == 2 && tag[1] == 'r') || (len == 2 && tag[1] == 'd') || (len == 2 && tag[1] == 'h') ||
                      (len >= 4 && tag[1] == 'a' && tag[2] == 'b' && tag[3] == 'l');
        case 'o': return len == 2 && tag[1] == 'l';
        case 'u': return len == 2 && tag[1] == 'l';
        case 's': return (len == 5 && tag[1] == 'c' && tag[2] == 'r' && tag[3] == 'i' && tag[4] == 'p') ||
                      (len == 5 && tag[1] == 't' && tag[2] == 'y' && tag[3] == 'l' && tag[4] == 'e');
        case 'f': return len == 4 && tag[1] == 'o' && tag[2] == 'r' && tag[3] == 'm';
        case 'n': return len == 3 && tag[1] == 'a' && tag[2] == 'v';
        case 'a': return len == 4 && tag[1] == 'r' && tag[2] == 't' && tag[3] == 'i';
        default: return 0;
    }
}

static const char* entity_lookup(const char* name, uint32_t nlen)
{
    for (int i = 0; i < N_ENT; i++) {
        const char* r = s_entities[i].name;
        uint32_t j = 0;
        while (j < nlen && r[j] && r[j] == name[j]) j++;
        if (j == nlen && !r[j]) return s_entities[i].repl;
    }
    return NULL;
}

static int decode_num_entity(const char* p, uint32_t lim, uint32_t* consumed, char* out, uint32_t out_cap)
{
    if (lim < 4 || p[0] != '&' || p[1] != '#') return 0;
    uint32_t val = 0;
    uint32_t i = 2;
    int hex = 0;
    if (i < lim && (p[i] == 'x' || p[i] == 'X')) { hex = 1; i++; }
    for (; i < lim && i < 14; i++) {
        char c = p[i];
        if (c == ';') {
            *consumed = i + 1;
            if (val >= 0x110000 || (val >= 0xD800 && val <= 0xDFFF)) return 0;
            if (val < 128) {
                if (out_cap > 0) { out[0] = (char)val; return 1; }
                return 0;
            }
            if (val < 0x800) {
                if (out_cap < 2) return 0;
                out[0] = (char)(0xC0u | (val >> 6));
                out[1] = (char)(0x80u | (val & 0x3Fu));
                return 2;
            }
            if (val < 0x10000) {
                if (out_cap < 3) return 0;
                out[0] = (char)(0xE0u | (val >> 12));
                out[1] = (char)(0x80u | ((val >> 6) & 0x3Fu));
                out[2] = (char)(0x80u | (val & 0x3Fu));
                return 3;
            }
            if (val < 0x110000 && out_cap >= 4) {
                out[0] = (char)(0xF0u | (val >> 18));
                out[1] = (char)(0x80u | ((val >> 12) & 0x3Fu));
                out[2] = (char)(0x80u | ((val >> 6) & 0x3Fu));
                out[3] = (char)(0x80u | (val & 0x3Fu));
                return 4;
            }
            return 0;
        }
        if (hex) {
            if (c >= '0' && c <= '9') val = val * 16 + (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = val * 16 + (uint32_t)(c - 'A' + 10);
            else return 0;
        } else {
            if (c >= '0' && c <= '9') val = val * 10 + (uint32_t)(c - '0');
            else return 0;
        }
    }
    return 0;
}

static uint32_t html_to_text(const char* in, uint32_t in_len, char* out, uint32_t out_cap)
{
    uint32_t o = 0;
    int in_tag = 0;
    int last_was_space = 1;
    char tag_buf[16];
    uint32_t tag_len = 0;
    int tag_is_close = 0;

    for (uint32_t i = 0; i < in_len && o + 1 < out_cap; i++) {
        char c = in[i];

        if (in_tag) {
            if (c == '>') {
                in_tag = 0;
                tag_buf[tag_len < 15 ? tag_len : 15] = 0;
                if (is_block_tag(tag_buf, tag_len) && o > 0 && out[o - 1] != '\n') {
                    if (o + 1 < out_cap) { out[o++] = '\n'; last_was_space = 1; }
                }
                if (tag_is_close && is_block_tag(tag_buf, tag_len) && o > 0 && out[o - 1] != '\n') {
                    if (o + 1 < out_cap) { out[o++] = '\n'; last_was_space = 1; }
                }
                tag_len = 0;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '/') {
                if (c == '/' && tag_len == 0) tag_is_close = 1;
                continue;
            }
            if (tag_len < 15 && (((unsigned char)c >= 'a' && (unsigned char)c <= 'z') ||
                                 ((unsigned char)c >= 'A' && (unsigned char)c <= 'Z'))) {
                tag_buf[tag_len++] = (char)((unsigned char)c >= 'A' && (unsigned char)c <= 'Z' ? c + 32 : c);
            }
            continue;
        }

        if (c == '<') {
            in_tag = 1;
            tag_len = 0;
            tag_is_close = 0;
            if (i + 2 < in_len && in[i + 1] == '!' && in[i + 2] == '-') {
                i += 3;
                while (i + 2 < in_len && !(in[i] == '-' && in[i + 1] == '-' && in[i + 2] == '>')) i++;
                i += 2;
            } else if (i + 1 < in_len && in[i + 1] == '?') {
                i++;
                while (i + 1 < in_len && !(in[i] == '?' && in[i + 1] == '>')) i++;
                i++;
            }
            continue;
        }

        if (c == '&') {
            uint32_t nlen = 0;
            while (nlen < 32 && i + 1 + nlen < in_len) {
                char n = in[i + 1 + nlen];
                if ((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || (n >= '0' && n <= '9')) nlen++;
                else break;
            }
            if (i + 1 + nlen < in_len && in[i + 1 + nlen] == ';') {
                const char* repl = entity_lookup(in + i + 1, nlen);
                if (repl) {
                    uint32_t w = 0;
                    while (repl[w] && w < out_cap - o - 1) { out[o + w] = repl[w]; w++; }
                    o += w;
                    last_was_space = (w == 1 && (repl[0] == ' ' || repl[0] == '\n'));
                    i += nlen + 2;
                    continue;
                }
            }
            if (i + 2 < in_len && in[i + 1] == '#') {
                uint32_t cons;
                char ebuf[4];
                int nw = decode_num_entity(in + i, in_len - i, &cons, ebuf, sizeof(ebuf));
                if (nw > 0 && o + nw < out_cap) {
                    for (int w = 0; w < nw; w++) out[o + w] = ebuf[w];
                    o += nw;
                    last_was_space = (nw == 1 && (ebuf[0] == ' ' || ebuf[0] == '\n'));
                    i += cons - 1;
                    continue;
                }
            }
            if (o + 1 < out_cap) { out[o++] = c; last_was_space = 0; }
            continue;
        }

        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        if (c == ' ') {
            if (!last_was_space && o + 1 < out_cap) { out[o++] = ' '; last_was_space = 1; }
            continue;
        }
        if ((unsigned char)c >= ' ' && (unsigned char)c <= 126) {
            out[o++] = c;
            last_was_space = 0;
        }
    }
    out[o] = '\0';
    return o;
}

static const char* find_body(const char* p, uint32_t len, uint32_t* body_len)
{
    for (uint32_t i = 0; i + 3 < len; i++) {
        if ((p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') ||
            (p[i] == '\n' && p[i + 1] == '\n')) {
            uint32_t off = (p[i] == '\r') ? 4u : 2u;
            *body_len = len - i - off;
            return p + i + off;
        }
    }
    if (len >= 2 && p[0] == '\n' && p[1] == '\n') {
        *body_len = len - 2;
        return p + 2;
    }
    *body_len = len;
    return p;
}

static int looks_like_html(const char* p, uint32_t len)
{
    while (len && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) { p++; len--; }
    if (len < 5) return 0;
    if (p[0] == '<' && (p[1] == 'h' || p[1] == 'H') && (p[2] == 't' || p[2] == 'T') && (p[3] == 'm' || p[3] == 'M') && (p[4] == 'l' || p[4] == 'L')) return 1;
    if (p[0] == '<' && (p[1] == '!' || p[1] == '?')) return 1;
    for (uint32_t i = 0; i < len && i < 512; i++) if (p[i] == '<') return 1;
    return 0;
}

int lafillo_http_to_text(const char* http_resp, uint32_t resp_len, char* out, uint32_t out_cap)
{
    if (!http_resp || !out || out_cap == 0) return -1;
    out[0] = '\0';

    uint32_t body_len = 0;
    const char* body = find_body(http_resp, resp_len, &body_len);

    if (!looks_like_html(body, body_len)) {
        uint32_t n = body_len;
        if (n >= out_cap) n = out_cap - 1;
        for (uint32_t i = 0; i < n; i++) out[i] = body[i];
        out[n] = '\0';
        return 0;
    }

    html_to_text(body, body_len, out, out_cap);
    return 0;
}
