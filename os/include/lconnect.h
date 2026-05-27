#pragma once

#include "net.h"
#include <stdint.h>

#define LCONNECT_PORT 39011u
#define LCONNECT_NODE_MAX 31u
#define LCONNECT_PEER_MAX 8u
#define LCONNECT_LOG_DEPTH 12u
#define LCONNECT_DETAIL_MAX 79u

#define LCONNECT_RES_MEGACLIP   0x00000001u
#define LCONNECT_RES_CPU        0x00000002u
#define LCONNECT_RES_GPU        0x00000004u
#define LCONNECT_RES_STORAGE    0x00000008u
#define LCONNECT_RES_PERIPHERAL 0x00000010u
#define LCONNECT_RES_ALL        (LCONNECT_RES_MEGACLIP | LCONNECT_RES_CPU | LCONNECT_RES_GPU | LCONNECT_RES_STORAGE | LCONNECT_RES_PERIPHERAL)

typedef struct {
    ip4_t ip;
    char node[LCONNECT_NODE_MAX + 1u];
    uint32_t resources;
    uint32_t seen;
    uint32_t grants;
    uint32_t denied;
} lconnect_peer_t;

typedef struct {
    ip4_t ip;
    char node[LCONNECT_NODE_MAX + 1u];
    char action[15u + 1u];
    char detail[LCONNECT_DETAIL_MAX + 1u];
    uint32_t resource;
    uint32_t seq;
} lconnect_event_t;

typedef struct {
    uint32_t present;
    uint32_t configured;
    uint32_t enabled;
    uint32_t auto_grant;
    uint32_t resources;
    uint32_t peer_count;
    uint32_t sent;
    uint32_t received;
    uint32_t dropped;
    uint32_t grants;
    uint32_t denied;
    uint32_t clip_out;
    uint32_t clip_in;
    uint32_t leases;
    uint32_t pending;
    uint32_t last_error;
    ip4_t ip;
    char node[LCONNECT_NODE_MAX + 1u];
} lconnect_info_t;

void lconnect_init(net_stack_t* net, const net_cfg_t* cfg, const char* node);
void lconnect_set_cfg(const net_cfg_t* cfg);
int lconnect_direct(ip4_t ip, ip4_t mask);
void lconnect_poll(void);
int lconnect_enable(int on);
int lconnect_set_auto(int on);
int lconnect_set_share(uint32_t resource, int on);
uint32_t lconnect_share_mask(void);
uint32_t lconnect_resource_from_name(const char* name);
const char* lconnect_resource_name(uint32_t resource);
void lconnect_resource_list(uint32_t mask, char* out, uint32_t cap);
int lconnect_discover(ip4_t dst);
int lconnect_send_clip(ip4_t dst);
int lconnect_request(ip4_t dst, uint32_t resource, const char* detail);
int lconnect_grant(ip4_t dst, uint32_t resource, const char* detail);
int lconnect_deny(ip4_t dst, uint32_t resource, const char* detail);
uint32_t lconnect_peer_count(void);
int lconnect_peer_at(uint32_t idx, lconnect_peer_t* out);
uint32_t lconnect_log_count(void);
int lconnect_log_at(uint32_t idx, lconnect_event_t* out);
void lconnect_info(lconnect_info_t* out);
int lconnect_selftest(void);
