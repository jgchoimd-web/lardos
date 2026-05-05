/**
 * lard_inflate.c - LardOS 자체 DEFLATE 복원기 (RFC 1951)
 *
 * 서드파티 없음. 스택만 사용, kmalloc 없음.
 */
#include "lard_inflate.h"
#include <stddef.h>

typedef struct {
    const unsigned char* src;
    const unsigned char* src_end;
    unsigned int bits;
    int bitcnt;
    int overflow;
    unsigned char* dst;
    unsigned char* dst_start;
    unsigned char* dst_end;
} lard_inf_ctx_t;

typedef struct {
    unsigned short cnt[16];
    unsigned short sym[288];
    int max_sym;
} lard_huff_t;

static unsigned int read_le16(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static void refill_bits(lard_inf_ctx_t* c, int need)
{
    while (c->bitcnt < need && c->src < c->src_end) {
        c->bits |= (unsigned int)*c->src++ << c->bitcnt;
        c->bitcnt += 8;
    }
    if (c->bitcnt < need) c->overflow = 1;
}

static unsigned int get_bits(lard_inf_ctx_t* c, int n)
{
    refill_bits(c, n);
    if (c->overflow) return 0;
    unsigned int v = c->bits & ((1u << n) - 1u);
    c->bits >>= n;
    c->bitcnt -= n;
    return v;
}

static unsigned int get_bits_base(lard_inf_ctx_t* c, int n, unsigned int base)
{
    return base + (n ? get_bits(c, n) : 0);
}

static void build_fixed_trees(lard_huff_t* lt, lard_huff_t* dt)
{
    int i;
    for (i = 0; i < 16; i++) lt->cnt[i] = 0;
    lt->cnt[7] = 24;
    lt->cnt[8] = 152;
    lt->cnt[9] = 112;
    for (i = 0; i < 24; i++) lt->sym[i] = 256 + i;
    for (i = 0; i < 144; i++) lt->sym[24 + i] = i;
    for (i = 0; i < 8; i++) lt->sym[168 + i] = 280 + i;
    for (i = 0; i < 112; i++) lt->sym[176 + i] = 144 + i;
    lt->max_sym = 285;
    for (i = 0; i < 16; i++) dt->cnt[i] = 0;
    dt->cnt[5] = 32;
    for (i = 0; i < 32; i++) dt->sym[i] = i;
    dt->max_sym = 29;
}

static int build_tree(lard_huff_t* t, const unsigned char* len, unsigned int n)
{
    unsigned short off[16];
    unsigned int i, avail = 1, nc = 0;
    if (n > 288) return LARD_INFLATE_DATA_ERR;
    for (i = 0; i < 16; i++) t->cnt[i] = 0;
    t->max_sym = -1;
    for (i = 0; i < n; i++) {
        if (len[i] > 15) return LARD_INFLATE_DATA_ERR;
        if (len[i]) {
            t->max_sym = (int)i;
            t->cnt[len[i]]++;
        }
    }
    for (i = 0; i < 16; i++) {
        unsigned int u = t->cnt[i];
        if (u > avail) return LARD_INFLATE_DATA_ERR;
        avail = 2 * (avail - u);
        off[i] = (unsigned short)nc;
        nc += u;
    }
    if ((nc > 1 && avail > 0) || (nc == 1 && t->cnt[1] != 1))
        return LARD_INFLATE_DATA_ERR;
    for (i = 0; i < n; i++)
        if (len[i]) t->sym[off[len[i]]++] = (unsigned short)i;
    if (nc == 1) {
        t->cnt[1] = 2;
        t->sym[1] = (unsigned short)(t->max_sym + 1);
    }
    return LARD_INFLATE_OK;
}

static int decode_sym(lard_inf_ctx_t* c, const lard_huff_t* t)
{
    int base = 0, off = 0, len = 1;
    for (;; len++) {
        off = 2 * off + (int)get_bits(c, 1);
        if (len > 15) return -1;
        if ((unsigned)off < t->cnt[len]) break;
        base += t->cnt[len];
        off -= t->cnt[len];
    }
    if (base + off >= 288) return -1;
    return (int)t->sym[base + off];
}

static const unsigned char clcidx[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static const unsigned char len_bits[30] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0, 127
};
static const unsigned short len_base[30] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258, 0
};
static const unsigned char dist_bits[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const unsigned short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static int decode_dyn_trees(lard_inf_ctx_t* c, lard_huff_t* lt, lard_huff_t* dt,
                            unsigned char* len_buf)
{
    unsigned int hlit = get_bits_base(c, 5, 257);
    unsigned int hdist = get_bits_base(c, 5, 1);
    unsigned int hclen = get_bits_base(c, 4, 4);
    if (hlit > 286 || hdist > 30) return LARD_INFLATE_DATA_ERR;
    unsigned int i, num = 0;
    for (i = 0; i < 19; i++) len_buf[i] = 0;
    for (i = 0; i < hclen; i++)
        len_buf[clcidx[i]] = (unsigned char)get_bits(c, 3);
    if (build_tree(lt, len_buf, 19) != LARD_INFLATE_OK) return LARD_INFLATE_DATA_ERR;
    if (lt->max_sym < 0) return LARD_INFLATE_DATA_ERR;
    while (num < hlit + hdist) {
        int s = decode_sym(c, lt);
        if (s < 0) return LARD_INFLATE_DATA_ERR;
        if (s > lt->max_sym) return LARD_INFLATE_DATA_ERR;
        unsigned int rep = 1;
        unsigned char val = (unsigned char)s;
        if (s == 16) {
            if (num == 0) return LARD_INFLATE_DATA_ERR;
            val = len_buf[num - 1];
            rep = get_bits_base(c, 2, 3);
        } else if (s == 17) {
            val = 0;
            rep = get_bits_base(c, 3, 3);
        } else if (s == 18) {
            val = 0;
            rep = get_bits_base(c, 7, 11);
        }
        if (rep > hlit + hdist - num) return LARD_INFLATE_DATA_ERR;
        while (rep--) len_buf[num++] = val;
    }
    if (len_buf[256] == 0) return LARD_INFLATE_DATA_ERR;
    if (build_tree(lt, len_buf, hlit) != LARD_INFLATE_OK) return LARD_INFLATE_DATA_ERR;
    if (build_tree(dt, len_buf + hlit, hdist) != LARD_INFLATE_OK) return LARD_INFLATE_DATA_ERR;
    return LARD_INFLATE_OK;
}

static int inflate_block_data(lard_inf_ctx_t* c, const lard_huff_t* lt, const lard_huff_t* dt)
{
    for (;;) {
        int s = decode_sym(c, lt);
        if (c->overflow) return LARD_INFLATE_DATA_ERR;
        if (s < 256) {
            if (c->dst >= c->dst_end) return LARD_INFLATE_BUF_ERR;
            *c->dst++ = (unsigned char)s;
        } else if (s == 256) {
            return LARD_INFLATE_OK;
        } else {
            if (s > lt->max_sym || s - 257 > 28 || dt->max_sym < 0) return LARD_INFLATE_DATA_ERR;
            int lidx = s - 257;
            unsigned int ln = get_bits_base(c, len_bits[lidx], len_base[lidx]);
            int d = decode_sym(c, dt);
            if (d < 0 || d > dt->max_sym || d > 29) return LARD_INFLATE_DATA_ERR;
            unsigned int dist = get_bits_base(c, dist_bits[d], dist_base[d]);
            if (dist > (unsigned)(c->dst - c->dst_start)) return LARD_INFLATE_DATA_ERR;
            if (c->dst_end - c->dst < ln) return LARD_INFLATE_BUF_ERR;
            unsigned char* from = c->dst - dist;
            unsigned int i;
            for (i = 0; i < ln; i++) c->dst[i] = from[i];
            c->dst += ln;
        }
    }
}

static int inflate_stored(lard_inf_ctx_t* c)
{
    if (c->src_end - c->src < 4) return LARD_INFLATE_DATA_ERR;
    unsigned int ln = read_le16(c->src);
    unsigned int inv = read_le16(c->src + 2);
    if (ln != (inv ^ 0xFFFFu)) return LARD_INFLATE_DATA_ERR;
    c->src += 4;
    if (c->src_end - c->src < ln) return LARD_INFLATE_DATA_ERR;
    if (c->dst_end - c->dst < ln) return LARD_INFLATE_BUF_ERR;
    while (ln--) *c->dst++ = *c->src++;
    c->bits = 0;
    c->bitcnt = 0;
    return LARD_INFLATE_OK;
}

int lard_inflate_uncompress(void* dest, unsigned int* destLen,
                            const void* source, unsigned int sourceLen)
{
    lard_inf_ctx_t c;
    c.src = (const unsigned char*)source;
    c.src_end = c.src + sourceLen;
    c.bits = 0;
    c.bitcnt = 0;
    c.overflow = 0;
    c.dst = (unsigned char*)dest;
    c.dst_start = c.dst;
    c.dst_end = c.dst + *destLen;

    for (;;) {
        int bfinal = (int)get_bits(&c, 1);
        unsigned int btype = get_bits(&c, 2);
        int res;
        if (btype == 0) {
            res = inflate_stored(&c);
        } else if (btype == 1) {
            lard_huff_t lt, dt;
            build_fixed_trees(&lt, &dt);
            res = inflate_block_data(&c, &lt, &dt);
        } else if (btype == 2) {
            lard_huff_t lt, dt;
            unsigned char len_buf[288 + 32];
            res = decode_dyn_trees(&c, &lt, &dt, len_buf);
            if (res == LARD_INFLATE_OK) res = inflate_block_data(&c, &lt, &dt);
        } else {
            res = LARD_INFLATE_DATA_ERR;
        }
        if (res != LARD_INFLATE_OK) return res;
        if (bfinal) break;
    }
    if (c.overflow) return LARD_INFLATE_DATA_ERR;
    *destLen = (unsigned int)(c.dst - c.dst_start);
    return LARD_INFLATE_OK;
}
