/*
 * LDA - LardOS Archive format
 * Magic "LDAC", deflate-compressed payload.
 * Header: "LDAC" (4) + ver(1) + uncompressed_size(4) + deflate_data
 */
#pragma once

#include <stdint.h>

#define LDA_MAGIC 0x4341444Cu  /* "LDAC" LE */

/* Decompress LDA to buffer. *out_len is buffer size in, bytes written out. Returns 0 on success. */
int lda_decompress(const uint8_t* data, uint32_t len, uint8_t* out, uint32_t* out_len);
