#include "lipc.h"

#include <stddef.h>

typedef struct {
    uint16_t len;
    uint8_t data[LIPC_MAX_PAYLOAD];
} lipc_msg_t;

typedef struct {
    lipc_msg_t q[LIPC_QUEUE_DEPTH];
    uint32_t count;
    uint32_t head;
    uint32_t tail;
} lipc_port_t;

static lipc_port_t s_ports[LIPC_NUM_PORTS];

void lipc_init(void)
{
    for (uint32_t i = 0; i < LIPC_NUM_PORTS; i++) {
        s_ports[i].count = 0;
        s_ports[i].head = 0;
        s_ports[i].tail = 0;
    }
}

static lipc_port_t* lipc_port(uint32_t id)
{
    if (id >= LIPC_NUM_PORTS) {
        return 0;
    }
    return &s_ports[id];
}

int lipc_send(uint32_t port_id, const void* data, uint32_t len)
{
    if (!data || len == 0 || len > LIPC_MAX_PAYLOAD) {
        return -1;
    }
    lipc_port_t* p = lipc_port(port_id);
    if (!p || p->count >= LIPC_QUEUE_DEPTH) {
        return -1;
    }
    lipc_msg_t* m = &p->q[p->tail];
    m->len = (uint16_t)len;
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) {
        m->data[i] = src[i];
    }
    p->tail = (p->tail + 1) % LIPC_QUEUE_DEPTH;
    p->count++;
    return (int)len;
}

int lipc_recv(uint32_t port_id, void* buf, uint32_t cap)
{
    if (!buf || cap == 0) {
        return -1;
    }
    lipc_port_t* p = lipc_port(port_id);
    if (!p || p->count == 0) {
        return 0;
    }
    lipc_msg_t* m = &p->q[p->head];
    if ((uint32_t)m->len > cap) {
        return -1;
    }
    uint8_t* dst = (uint8_t*)buf;
    for (uint32_t i = 0; i < (uint32_t)m->len; i++) {
        dst[i] = m->data[i];
    }
    int n = (int)m->len;
    p->head = (p->head + 1) % LIPC_QUEUE_DEPTH;
    p->count--;
    return n;
}

uint32_t lipc_pending(uint32_t port_id)
{
    lipc_port_t* p = lipc_port(port_id);
    if (!p) {
        return 0;
    }
    return p->count;
}
