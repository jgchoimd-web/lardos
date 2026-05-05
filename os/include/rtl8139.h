#pragma once

#include <stdint.h>

typedef struct {
    uint16_t io_base;
    uint8_t irq_line;
    uint8_t mac[6];
} rtl8139_t;

int rtl8139_init(rtl8139_t* n);
int rtl8139_poll_rx(rtl8139_t* n, uint8_t* out_buf, uint32_t out_cap, uint32_t* out_len);
int rtl8139_send(rtl8139_t* n, const void* data, uint32_t len);

