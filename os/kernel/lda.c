/*
 * LDA - LardOS Archive format
 * Header: "LDAC" (4) + ver(1) + uncompressed_size(4) = 9 bytes
 * Payload: raw deflate stream (RFC 1951)
 */
#include "lda.h"
#include "lard_inflate.h"
#include <stddef.h>

int lda_decompress(const uint8_t* data, uint32_t len, uint8_t* out, uint32_t* out_len)
{
    if (!data || !out || !out_len || len < 9) return -1;
    if ((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24) != LDA_MAGIC)
        return -2;
    if (data[4] != 1) return -3;
    unsigned int orig_size = (unsigned int)data[5] | ((unsigned int)data[6] << 8) |
                            ((unsigned int)data[7] << 16) | ((unsigned int)data[8] << 24);
    if (orig_size > *out_len) return -4;
    const uint8_t* deflate_src = data + 9;
    unsigned int deflate_len = (unsigned int)(len - 9);
    unsigned int dest_len = orig_size;
    int r = lard_inflate_uncompress(out, &dest_len, deflate_src, deflate_len);
    if (r != LARD_INFLATE_OK) return -5;
    *out_len = dest_len;
    return 0;
}
