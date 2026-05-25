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

int net_udp_send(net_stack_t* n,
                 ip4_t dst,
                 uint16_t src_port,
                 uint16_t dst_port,
                 const void* payload,
                 uint32_t payload_len);

/* Poll one UDP datagram for dst_port. Returns payload bytes, 0 if none, -1 on bad args/truncation. */
int net_udp_recv(net_stack_t* n,
                 uint16_t dst_port,
                 void* out_payload,
                 uint32_t out_cap,
                 ip4_t* out_src,
                 uint16_t* out_src_port);

/* host_hdr is the full Host header value, for example example.com or 10.0.2.2:8765. */
int net_http_request(net_stack_t* n,
                     ip4_t dst,
                     uint16_t port,
                     const char* host_hdr,
                     const char* path,
                     const char* method,
                     const char* body,
                     uint32_t body_len,
                     char* out,
                     uint32_t out_cap);

int net_http_get(net_stack_t* n,
                 ip4_t dst,
                 uint16_t port,
                 const char* host_hdr,
                 const char* path,
                 char* out,
                 uint32_t out_cap);

/* Native in-kernel TLS path over TCP. No external TLS library is linked. */
int net_https_request(net_stack_t* n,
                      ip4_t dst,
                      uint16_t port,
                      const char* host_hdr,
                      const char* path,
                      const char* method,
                      const char* body,
                      uint32_t body_len,
                      char* out,
                      uint32_t out_cap);

int net_https_get(net_stack_t* n,
                  ip4_t dst,
                  uint16_t port,
                  const char* host_hdr,
                  const char* path,
                  char* out,
                  uint32_t out_cap);

/* Parser/builder selftest for GET/POST/HEAD/PUT/PATCH/DELETE/OPTIONS. Does not open the network. */
int net_http_selftest(void);
