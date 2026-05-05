#pragma once

#include <stdint.h>
#include "net.h"

// Minimal TCP "socket" handle (opaque storage).
typedef struct {
    uint8_t opaque[2048];
} net_sock_t;

// Connect to dst:port using current DHCP config.
int net_sock_connect(net_stack_t* n, ip4_t dst, uint16_t port, net_sock_t* out);
int net_sock_send(net_stack_t* n, net_sock_t* s, const void* data, uint32_t len);
int net_sock_recv(net_stack_t* n, net_sock_t* s, char* out, uint32_t* io_len, uint8_t* out_flags);
int net_sock_close(net_stack_t* n, net_sock_t* s);

