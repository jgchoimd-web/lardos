#pragma once

#include "net.h"
#include <stdint.h>

#define OSLINK_PORT 39010u
#define OSLINK_NODE_MAX 31u
#define OSLINK_TEXT_MAX 192u
#define OSLINK_CHANNEL_MAX 15u
#define OSLINK_INBOX_DEPTH 8u
#define OSLINK_PEER_MAX 8u

typedef struct {
    ip4_t ip;
    char node[OSLINK_NODE_MAX + 1u];
    uint32_t seen;
} oslink_peer_t;

typedef struct {
    ip4_t src_ip;
    char src_node[OSLINK_NODE_MAX + 1u];
    char channel[OSLINK_CHANNEL_MAX + 1u];
    char text[OSLINK_TEXT_MAX + 1u];
    uint32_t seq;
    uint8_t type;
} oslink_msg_t;

typedef struct {
    uint32_t ready;
    uint32_t port;
    uint32_t sent;
    uint32_t received;
    uint32_t dropped;
    uint32_t inbox_count;
    uint32_t local_count;
    uint32_t local_sent;
    uint32_t peer_count;
    uint32_t last_error;
    char node[OSLINK_NODE_MAX + 1u];
    ip4_t ip;
} oslink_info_t;

void oslink_init(net_stack_t* net, const net_cfg_t* cfg, const char* node);
int oslink_ready(void);
void oslink_poll(void);
int oslink_send_hello(ip4_t dst);
int oslink_send_ping(ip4_t dst, const char* text);
int oslink_send_text(ip4_t dst, const char* text);
int oslink_send_exec(ip4_t dst, const char* command);
int oslink_emit_local(const char* channel, const char* text);
int oslink_recv(oslink_msg_t* out);
uint32_t oslink_local_count(void);
uint32_t oslink_peer_count(void);
int oslink_peer_at(uint32_t idx, oslink_peer_t* out);
void oslink_info(oslink_info_t* out);
int oslink_selftest(void);
