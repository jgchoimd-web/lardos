/*
 * librbuf - Ring buffer. Freestanding, no alloc.
 */
#include "ringbuf.h"

void ringbuf_init(ringbuf_t* r, uint8_t* buf, uint32_t cap)
{
    r->buf = buf;
    r->cap = cap;
    r->head = 0;
    r->tail = 0;
}

uint32_t ringbuf_len(const ringbuf_t* r)
{
    uint32_t h = r->head;
    uint32_t t = r->tail;
    if (h >= t) return h - t;
    return r->cap - t + h;
}

uint32_t ringbuf_free(const ringbuf_t* r)
{
    return r->cap - ringbuf_len(r) - 1;  /* keep 1 slot to distinguish full vs empty */
}

uint32_t ringbuf_write(ringbuf_t* r, const uint8_t* data, uint32_t n)
{
    uint32_t f = ringbuf_free(r);
    if (n > f) n = f;
    for (uint32_t i = 0; i < n; i++) {
        r->buf[r->head] = data[i];
        r->head = (r->head + 1) % r->cap;
    }
    return n;
}

uint32_t ringbuf_read(ringbuf_t* r, uint8_t* out, uint32_t n)
{
    uint32_t len = ringbuf_len(r);
    if (n > len) n = len;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = r->buf[r->tail];
        r->tail = (r->tail + 1) % r->cap;
    }
    return n;
}

uint32_t ringbuf_skip(ringbuf_t* r, uint32_t n)
{
    uint32_t len = ringbuf_len(r);
    if (n > len) n = len;
    r->tail = (r->tail + n) % r->cap;
    return n;
}

void ringbuf_clear(ringbuf_t* r)
{
    r->head = 0;
    r->tail = 0;
}
