/**
 * lard_inflate.h - LardOS 자체 DEFLATE 복원기
 *
 * RFC 1951 raw deflate 스트림 복원.
 * 서드파티 없음, OS 전용 구현.
 */
#pragma once

#define LARD_INFLATE_OK       0
#define LARD_INFLATE_DATA_ERR -3
#define LARD_INFLATE_BUF_ERR  -5

/* destLen: 입출력. 입력=버퍼 크기, 출력=실제 복원 바이트 수. */
int lard_inflate_uncompress(void* dest, unsigned int* destLen,
                            const void* source, unsigned int sourceLen);
