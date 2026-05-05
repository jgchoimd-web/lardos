#pragma once

#include <stddef.h>
#include <stdint.h>

/* Fixed-size ring buffer. Caller allocates the buffer. */
typedef struct ringbuf {
    uint8_t* buf;
    uint32_t cap;
    uint32_t head;   /* write position */
    uint32_t tail;   /* read position */
} ringbuf_t;

/* Init with pre-allocated buffer. */
void ringbuf_init(ringbuf_t* r, uint8_t* buf, uint32_t cap);
/* Bytes available to read */
uint32_t ringbuf_len(const ringbuf_t* r);
/* Free space for write */
uint32_t ringbuf_free(const ringbuf_t* r);
/* Write up to n bytes. Returns bytes written. */
uint32_t ringbuf_write(ringbuf_t* r, const uint8_t* data, uint32_t n);
/* Read up to n bytes. Returns bytes read. */
uint32_t ringbuf_read(ringbuf_t* r, uint8_t* out, uint32_t n);
/* Discard n bytes from head. Returns bytes discarded. */
uint32_t ringbuf_skip(ringbuf_t* r, uint32_t n);
/* Clear buffer */
void ringbuf_clear(ringbuf_t* r);
