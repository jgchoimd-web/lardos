#pragma once

#include <stdint.h>

typedef struct {
    uint8_t b[4];
} ip4_t;

typedef struct {
    uint8_t mac[6];
    ip4_t ip;
    ip4_t mask;
    ip4_t gw;
    ip4_t dns;
} net_cfg_t;

typedef struct net_stack {
    uint8_t opaque[4096];
} net_stack_t;

int net_init(net_stack_t* n);
int net_dhcp(net_stack_t* n, net_cfg_t* out);
int net_get_cfg(net_stack_t* n, net_cfg_t* out);
int net_dns_a(net_stack_t* n, ip4_t dns, const char* name, ip4_t* out_ip);

/* host_hdr is the full Host header value, for example example.com or 10.0.2.2:8765. */
int net_http_get(net_stack_t* n,
                 ip4_t dst,
                 uint16_t port,
                 const char* host_hdr,
                 const char* path,
                 char* out,
                 uint32_t out_cap);

/* Native in-kernel TLS path over TCP. No external TLS library is linked. */
int net_https_get(net_stack_t* n,
                  ip4_t dst,
                  uint16_t port,
                  const char* host_hdr,
                  const char* path,
                  char* out,
                  uint32_t out_cap);
