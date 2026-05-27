#include "lconnect.h"

#include "lardkit.h"
#include "megaclip.h"

#include <stddef.h>
#include <stdint.h>

#define LCON_MAGIC0 'L'
#define LCON_MAGIC1 'C'
#define LCON_MAGIC2 'O'
#define LCON_MAGIC3 'N'
#define LCON_VERSION 1u
#define LCON_HEADER 22u
#define LCON_PAYLOAD_MAX 704u

enum {
    LCON_TYPE_HELLO = 1,
    LCON_TYPE_OFFER = 2,
    LCON_TYPE_CLIP = 3,
    LCON_TYPE_REQUEST = 4,
    LCON_TYPE_GRANT = 5,
    LCON_TYPE_DENY = 6,
    LCON_TYPE_RELEASE = 7,
};

typedef struct {
    net_stack_t* net;
    net_cfg_t cfg;
    uint32_t present;
    uint32_t configured;
    uint32_t enabled;
    uint32_t auto_grant;
    uint32_t resources;
    uint32_t seq;
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
    uint32_t deprecated_input;
    uint32_t deprecated_quiet;
    uint32_t deprecated_events;
    char node[LCONNECT_NODE_MAX + 1u];
    lconnect_peer_t peers[LCONNECT_PEER_MAX];
    uint32_t peer_count;
    lconnect_event_t log[LCONNECT_LOG_DEPTH];
    uint32_t log_count;
    uint32_t log_head;
} lconnect_state_t;

typedef struct {
    uint8_t type;
    uint32_t seq;
    uint32_t resource;
    uint32_t resources;
    char node[LCONNECT_NODE_MAX + 1u];
    uint8_t payload[LCON_PAYLOAD_MAX];
    uint16_t payload_len;
} lconnect_packet_t;

static lconnect_state_t s_lc;

static uint32_t slen_cap(const char* s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n] && n < cap) n++;
    return n;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    for (;;) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
        i++;
    }
}

static void scopy(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void append(char* out, uint32_t cap, const char* text)
{
    uint32_t i = 0;
    uint32_t j = 0;
    if (!out || cap == 0 || !text) return;
    while (out[i] && i + 1u < cap) i++;
    while (text[j] && i + 1u < cap) out[i++] = text[j++];
    out[i] = '\0';
}

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int ip_equal(ip4_t a, ip4_t b)
{
    return a.b[0] == b.b[0] && a.b[1] == b.b[1] && a.b[2] == b.b[2] && a.b[3] == b.b[3];
}

static uint32_t allowed_resource_mask(void)
{
    return s_lc.deprecated_input ? LCONNECT_RES_DEPRECATED_ALL : LCONNECT_RES_ALL;
}

static ip4_t broadcast_ip(void)
{
    ip4_t ip = {{255, 255, 255, 255}};
    return ip;
}

static void log_event(ip4_t ip, const char* node, const char* action,
                      uint32_t resource, uint32_t seq, const char* detail)
{
    uint32_t idx;
    if (s_lc.log_count < LCONNECT_LOG_DEPTH) {
        idx = (s_lc.log_head + s_lc.log_count) % LCONNECT_LOG_DEPTH;
        s_lc.log_count++;
    } else {
        idx = s_lc.log_head;
        s_lc.log_head = (s_lc.log_head + 1u) % LCONNECT_LOG_DEPTH;
    }
    lconnect_event_t* e = &s_lc.log[idx];
    e->ip = ip;
    e->resource = resource;
    e->seq = seq;
    scopy(e->node, sizeof(e->node), node);
    scopy(e->action, sizeof(e->action), action);
    scopy(e->detail, sizeof(e->detail), detail);
    lardkit_trace_event("lconnect", action ? action : "event", (int32_t)resource);
}

static void remember_peer(ip4_t ip, const char* node, uint32_t resources)
{
    for (uint32_t i = 0; i < s_lc.peer_count; i++) {
        if (ip_equal(s_lc.peers[i].ip, ip)) {
            scopy(s_lc.peers[i].node, sizeof(s_lc.peers[i].node), node);
            s_lc.peers[i].resources = resources;
            s_lc.peers[i].seen++;
            return;
        }
    }
    if (s_lc.peer_count >= LCONNECT_PEER_MAX) return;
    lconnect_peer_t* p = &s_lc.peers[s_lc.peer_count++];
    p->ip = ip;
    scopy(p->node, sizeof(p->node), node);
    p->resources = resources;
    p->seen = 1;
    p->grants = 0;
    p->denied = 0;
}

static void peer_note_grant(ip4_t ip, int grant)
{
    for (uint32_t i = 0; i < s_lc.peer_count; i++) {
        if (ip_equal(s_lc.peers[i].ip, ip)) {
            if (grant) s_lc.peers[i].grants++;
            else s_lc.peers[i].denied++;
            return;
        }
    }
}

static int build_packet(uint8_t type, uint32_t resource, const uint8_t* payload,
                        uint16_t payload_len, uint8_t* out, uint32_t cap, uint32_t* out_len)
{
    uint32_t node_len = slen_cap(s_lc.node, LCONNECT_NODE_MAX);
    uint32_t need = LCON_HEADER + node_len + (uint32_t)payload_len;
    if (!out || !out_len || need > cap || payload_len > LCON_PAYLOAD_MAX) return -1;
    out[0] = LCON_MAGIC0;
    out[1] = LCON_MAGIC1;
    out[2] = LCON_MAGIC2;
    out[3] = LCON_MAGIC3;
    out[4] = LCON_VERSION;
    out[5] = type;
    out[6] = (uint8_t)node_len;
    out[7] = 0;
    wr32(out + 8, ++s_lc.seq);
    wr32(out + 12, resource);
    wr32(out + 16, s_lc.enabled ? s_lc.resources : 0u);
    wr16(out + 20, payload_len);
    uint32_t p = LCON_HEADER;
    for (uint32_t i = 0; i < node_len; i++) out[p++] = (uint8_t)s_lc.node[i];
    for (uint32_t i = 0; i < payload_len; i++) out[p++] = payload ? payload[i] : 0u;
    *out_len = p;
    return 0;
}

static int parse_packet(const uint8_t* data, uint32_t len, lconnect_packet_t* out)
{
    if (!data || !out || len < LCON_HEADER) return -1;
    if (data[0] != LCON_MAGIC0 || data[1] != LCON_MAGIC1 ||
        data[2] != LCON_MAGIC2 || data[3] != LCON_MAGIC3) return -2;
    if (data[4] != LCON_VERSION) return -3;
    uint32_t node_len = data[6];
    uint32_t payload_len = rd16(data + 20);
    if (node_len > LCONNECT_NODE_MAX || payload_len > LCON_PAYLOAD_MAX) return -4;
    if (LCON_HEADER + node_len + payload_len > len) return -5;
    out->type = data[5];
    out->seq = rd32(data + 8);
    out->resource = rd32(data + 12);
    out->resources = rd32(data + 16);
    out->payload_len = (uint16_t)payload_len;
    uint32_t p = LCON_HEADER;
    for (uint32_t i = 0; i < node_len; i++) out->node[i] = (char)data[p + i];
    out->node[node_len] = '\0';
    p += node_len;
    for (uint32_t i = 0; i < payload_len; i++) out->payload[i] = data[p + i];
    return 0;
}

static int send_packet(ip4_t dst, uint8_t type, uint32_t resource,
                       const uint8_t* payload, uint16_t payload_len)
{
    uint8_t pkt[768];
    uint32_t len = 0;
    if (!s_lc.present || !s_lc.configured || !s_lc.net) {
        s_lc.last_error = 1;
        return -1;
    }
    if (build_packet(type, resource, payload, payload_len, pkt, sizeof(pkt), &len) != 0) {
        s_lc.last_error = 2;
        return -2;
    }
    int r = net_udp_send(s_lc.net, dst, LCONNECT_PORT, LCONNECT_PORT, pkt, len);
    if (r == 0) {
        s_lc.sent++;
        s_lc.last_error = 0;
        lardkit_netwatch_record("lconnect", "send", (int32_t)type);
    } else {
        s_lc.last_error = 3;
    }
    return r;
}

static const char* type_action(uint8_t type)
{
    if (type == LCON_TYPE_HELLO) return "hello";
    if (type == LCON_TYPE_OFFER) return "offer";
    if (type == LCON_TYPE_CLIP) return "megaclip";
    if (type == LCON_TYPE_REQUEST) return "request";
    if (type == LCON_TYPE_GRANT) return "grant";
    if (type == LCON_TYPE_DENY) return "deny";
    if (type == LCON_TYPE_RELEASE) return "release";
    return "packet";
}

static void handle_clip(ip4_t src, const lconnect_packet_t* p)
{
    char kind[MEGACLIP_KIND_MAX + 1u];
    char label[MEGACLIP_LABEL_MAX + 1u];
    uint32_t kind_len;
    uint32_t label_len;
    uint32_t size;
    uint32_t pos;
    if (!s_lc.enabled || (s_lc.resources & LCONNECT_RES_MEGACLIP) == 0) {
        s_lc.denied++;
        log_event(src, p->node, "clip-deny", LCONNECT_RES_MEGACLIP, p->seq, "megaclip sharing off");
        return;
    }
    if (p->payload_len < 4u) {
        s_lc.dropped++;
        return;
    }
    kind_len = p->payload[0];
    label_len = p->payload[1];
    size = rd16(p->payload + 2);
    pos = 4u;
    if (kind_len > MEGACLIP_KIND_MAX || label_len > MEGACLIP_LABEL_MAX ||
        pos + kind_len + label_len + size > p->payload_len) {
        s_lc.dropped++;
        return;
    }
    for (uint32_t i = 0; i < kind_len; i++) kind[i] = (char)p->payload[pos + i];
    kind[kind_len] = '\0';
    pos += kind_len;
    for (uint32_t i = 0; i < label_len; i++) label[i] = (char)p->payload[pos + i];
    label[label_len] = '\0';
    pos += label_len;
    if (megaclip_push(kind[0] ? kind : "remote", label[0] ? label : "lconnect",
                      p->payload + pos, size) == 0) {
        s_lc.clip_in++;
        log_event(src, p->node, "clip-in", LCONNECT_RES_MEGACLIP, p->seq, label);
    } else {
        s_lc.dropped++;
    }
}

static void handle_request(ip4_t src, const lconnect_packet_t* p)
{
    char detail[LCONNECT_DETAIL_MAX + 1u];
    uint32_t n = p->payload_len < LCONNECT_DETAIL_MAX ? p->payload_len : LCONNECT_DETAIL_MAX;
    for (uint32_t i = 0; i < n; i++) detail[i] = (char)p->payload[i];
    detail[n] = '\0';
    uint32_t allowed = allowed_resource_mask();
    if (!s_lc.enabled || p->resource == 0 || (p->resource & ~allowed) != 0 ||
        (s_lc.resources & p->resource) != p->resource) {
        (void)send_packet(src, LCON_TYPE_DENY, p->resource, (const uint8_t*)"not shared", 10u);
        s_lc.denied++;
        peer_note_grant(src, 0);
        log_event(src, p->node, "deny", p->resource, p->seq, "not shared");
        return;
    }
    if (s_lc.auto_grant || s_lc.deprecated_quiet) {
        (void)send_packet(src, LCON_TYPE_GRANT, p->resource, (const uint8_t*)"auto grant", 10u);
        s_lc.grants++;
        peer_note_grant(src, 1);
        log_event(src, p->node, s_lc.deprecated_quiet ? "quiet-grant" : "grant",
                  p->resource, p->seq, detail[0] ? detail : "auto");
    } else {
        s_lc.pending++;
        log_event(src, p->node, "pending", p->resource, p->seq, detail[0] ? detail : "manual grant required");
    }
}

void lconnect_init(net_stack_t* net, const net_cfg_t* cfg, const char* node)
{
    for (uint32_t i = 0; i < sizeof(s_lc); i++) ((uint8_t*)&s_lc)[i] = 0;
    s_lc.net = net;
    s_lc.present = net ? 1u : 0u;
    s_lc.seq = 0xC000u;
    s_lc.resources = LCONNECT_RES_MEGACLIP;
    scopy(s_lc.node, sizeof(s_lc.node), node && node[0] ? node : "lardos");
    if (cfg) lconnect_set_cfg(cfg);
}

void lconnect_set_cfg(const net_cfg_t* cfg)
{
    if (!cfg) return;
    s_lc.cfg = *cfg;
    s_lc.configured = 1;
}

int lconnect_direct(ip4_t ip, ip4_t mask)
{
    ip4_t zero = {{0, 0, 0, 0}};
    if (!s_lc.net) {
        s_lc.last_error = 4;
        return -1;
    }
    if (mask.b[0] == 0 && mask.b[1] == 0 && mask.b[2] == 0 && mask.b[3] == 0) {
        mask.b[0] = 255;
        mask.b[1] = 255;
        mask.b[2] = 255;
        mask.b[3] = 0;
    }
    if (net_set_ipv4(s_lc.net, ip, mask, zero, zero) != 0) {
        s_lc.last_error = 5;
        return -2;
    }
    if (net_get_cfg(s_lc.net, &s_lc.cfg) != 0) {
        s_lc.last_error = 6;
        return -3;
    }
    s_lc.configured = 1;
    log_event(ip, s_lc.node, "direct-ip", 0, ++s_lc.seq, "manual cable profile");
    return 0;
}

void lconnect_poll(void)
{
    if (!s_lc.present || !s_lc.configured || !s_lc.net) return;
    for (uint32_t i = 0; i < 3u; i++) {
        uint8_t data[768];
        ip4_t src;
        uint16_t sport = 0;
        int r = net_udp_recv(s_lc.net, LCONNECT_PORT, data, sizeof(data), &src, &sport);
        if (r <= 0) return;
        (void)sport;
        lconnect_packet_t pkt;
        if (parse_packet(data, (uint32_t)r, &pkt) != 0) {
            s_lc.dropped++;
            continue;
        }
        s_lc.received++;
        remember_peer(src, pkt.node, pkt.resources);
        lardkit_netwatch_record("lconnect", type_action(pkt.type), (int32_t)pkt.resource);
        if (pkt.type == LCON_TYPE_HELLO) {
            log_event(src, pkt.node, "hello", pkt.resource, pkt.seq, "discover");
            if (s_lc.enabled) (void)send_packet(src, LCON_TYPE_OFFER, s_lc.resources, NULL, 0);
        } else if (pkt.type == LCON_TYPE_OFFER) {
            log_event(src, pkt.node, "offer", pkt.resources, pkt.seq, "peer resources");
        } else if (pkt.type == LCON_TYPE_CLIP) {
            handle_clip(src, &pkt);
        } else if (pkt.type == LCON_TYPE_REQUEST) {
            handle_request(src, &pkt);
        } else if (pkt.type == LCON_TYPE_GRANT) {
            s_lc.leases++;
            peer_note_grant(src, 1);
            log_event(src, pkt.node, "lease", pkt.resource, pkt.seq, "remote grant");
        } else if (pkt.type == LCON_TYPE_DENY) {
            s_lc.denied++;
            peer_note_grant(src, 0);
            log_event(src, pkt.node, "deny", pkt.resource, pkt.seq, "remote deny");
        } else if (pkt.type == LCON_TYPE_RELEASE) {
            if (s_lc.leases) s_lc.leases--;
            log_event(src, pkt.node, "release", pkt.resource, pkt.seq, "remote release");
        }
    }
}

int lconnect_enable(int on)
{
    s_lc.enabled = on ? 1u : 0u;
    lardkit_journal_event("lconnect", s_lc.enabled ? "enabled" : "disabled");
    return 0;
}

int lconnect_set_auto(int on)
{
    s_lc.auto_grant = on ? 1u : 0u;
    return 0;
}

int lconnect_set_share(uint32_t resource, int on)
{
    uint32_t allowed = allowed_resource_mask();
    if (resource == 0 || (resource & ~allowed) != 0) {
        s_lc.last_error = 7;
        return -1;
    }
    if (on) s_lc.resources |= resource;
    else s_lc.resources &= ~resource;
    return 0;
}

uint32_t lconnect_share_mask(void)
{
    return s_lc.resources;
}

uint32_t lconnect_resource_from_name(const char* name)
{
    if (streq_ci(name, "all")) return LCONNECT_RES_ALL;
    if (streq_ci(name, "megaclip") || streq_ci(name, "clip") || streq_ci(name, "clipboard")) return LCONNECT_RES_MEGACLIP;
    if (streq_ci(name, "cpu") || streq_ci(name, "compute")) return LCONNECT_RES_CPU;
    if (streq_ci(name, "gpu") || streq_ci(name, "gfx")) return LCONNECT_RES_GPU;
    if (streq_ci(name, "storage") || streq_ci(name, "disk") || streq_ci(name, "drive")) return LCONNECT_RES_STORAGE;
    if (streq_ci(name, "peripheral") || streq_ci(name, "peripherals") || streq_ci(name, "device")) return LCONNECT_RES_PERIPHERAL;
    return 0;
}

uint32_t lconnect_deprecated_resource_from_name(const char* name)
{
    uint32_t r = lconnect_resource_from_name(name);
    if (r) return r;
    if (streq_ci(name, "keyboard") || streq_ci(name, "keys") || streq_ci(name, "kbd")) return LCONNECT_RES_KEYBOARD;
    if (streq_ci(name, "mouse") || streq_ci(name, "pointer")) return LCONNECT_RES_MOUSE;
    if (streq_ci(name, "input") || streq_ci(name, "hid")) return LCONNECT_RES_INPUT;
    if (streq_ci(name, "everything") || streq_ci(name, "all-unsafe")) return LCONNECT_RES_DEPRECATED_ALL;
    return 0;
}

const char* lconnect_resource_name(uint32_t resource)
{
    if (resource == LCONNECT_RES_MEGACLIP) return "megaclip";
    if (resource == LCONNECT_RES_CPU) return "cpu";
    if (resource == LCONNECT_RES_GPU) return "gpu";
    if (resource == LCONNECT_RES_STORAGE) return "storage";
    if (resource == LCONNECT_RES_PERIPHERAL) return "peripheral";
    if (resource == LCONNECT_RES_KEYBOARD) return "keyboard";
    if (resource == LCONNECT_RES_MOUSE) return "mouse";
    if (resource == LCONNECT_RES_INPUT) return "input";
    if (resource == LCONNECT_RES_ALL) return "all";
    if (resource == LCONNECT_RES_DEPRECATED_ALL) return "everything";
    return "resource";
}

void lconnect_resource_list(uint32_t mask, char* out, uint32_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (mask & LCONNECT_RES_MEGACLIP) append(out, cap, out[0] ? ",megaclip" : "megaclip");
    if (mask & LCONNECT_RES_CPU) append(out, cap, out[0] ? ",cpu" : "cpu");
    if (mask & LCONNECT_RES_GPU) append(out, cap, out[0] ? ",gpu" : "gpu");
    if (mask & LCONNECT_RES_STORAGE) append(out, cap, out[0] ? ",storage" : "storage");
    if (mask & LCONNECT_RES_PERIPHERAL) append(out, cap, out[0] ? ",peripheral" : "peripheral");
    if (mask & LCONNECT_RES_KEYBOARD) append(out, cap, out[0] ? ",keyboard" : "keyboard");
    if (mask & LCONNECT_RES_MOUSE) append(out, cap, out[0] ? ",mouse" : "mouse");
    if (!out[0]) scopy(out, cap, "none");
}

static int confirm_word(const char* confirm)
{
    return streq_ci(confirm, "confirm") || streq_ci(confirm, "yes") || streq_ci(confirm, "i-own-this");
}

int lconnect_deprecated_set_input(int on, const char* confirm)
{
    if (!confirm_word(confirm)) {
        s_lc.last_error = 9;
        return -1;
    }
    s_lc.deprecated_input = on ? 1u : 0u;
    if (!s_lc.deprecated_input) s_lc.resources &= ~LCONNECT_RES_INPUT;
    s_lc.deprecated_events++;
    log_event(s_lc.cfg.ip, s_lc.node, "deprecated-input", LCONNECT_RES_INPUT,
              ++s_lc.seq, s_lc.deprecated_input ? "on" : "off");
    return 0;
}

int lconnect_deprecated_set_quiet(int on, const char* confirm)
{
    if (!confirm_word(confirm)) {
        s_lc.last_error = 10;
        return -1;
    }
    s_lc.deprecated_quiet = on ? 1u : 0u;
    s_lc.deprecated_events++;
    log_event(s_lc.cfg.ip, s_lc.node, "deprecated-quiet", 0,
              ++s_lc.seq, s_lc.deprecated_quiet ? "on" : "off");
    return 0;
}

int lconnect_discover(ip4_t dst)
{
    if (dst.b[0] == 0 && dst.b[1] == 0 && dst.b[2] == 0 && dst.b[3] == 0) dst = broadcast_ip();
    return send_packet(dst, LCON_TYPE_HELLO, s_lc.resources, (const uint8_t*)"discover", 8u);
}

int lconnect_send_clip(ip4_t dst)
{
    megaclip_item_t item;
    uint8_t payload[LCON_PAYLOAD_MAX];
    if (megaclip_pull_latest(&item) != 0) {
        s_lc.last_error = 8;
        return -1;
    }
    uint32_t kind_len = slen_cap(item.kind, MEGACLIP_KIND_MAX);
    uint32_t label_len = slen_cap(item.label, MEGACLIP_LABEL_MAX);
    uint32_t max_data = LCON_PAYLOAD_MAX - 4u - kind_len - label_len;
    uint32_t data_len = item.size < max_data ? item.size : max_data;
    payload[0] = (uint8_t)kind_len;
    payload[1] = (uint8_t)label_len;
    wr16(payload + 2, (uint16_t)data_len);
    uint32_t p = 4u;
    for (uint32_t i = 0; i < kind_len; i++) payload[p++] = (uint8_t)item.kind[i];
    for (uint32_t i = 0; i < label_len; i++) payload[p++] = (uint8_t)item.label[i];
    for (uint32_t i = 0; i < data_len; i++) payload[p++] = item.data[i];
    int r = send_packet(dst, LCON_TYPE_CLIP, LCONNECT_RES_MEGACLIP, payload, (uint16_t)p);
    if (r == 0) s_lc.clip_out++;
    return r;
}

static int send_text_resource(ip4_t dst, uint8_t type, uint32_t resource, const char* detail)
{
    uint8_t payload[LCON_PAYLOAD_MAX];
    uint32_t n = slen_cap(detail, LCON_PAYLOAD_MAX);
    for (uint32_t i = 0; i < n; i++) payload[i] = (uint8_t)detail[i];
    return send_packet(dst, type, resource, payload, (uint16_t)n);
}

int lconnect_request(ip4_t dst, uint32_t resource, const char* detail)
{
    if (resource == 0 || (resource & ~allowed_resource_mask()) != 0) return -1;
    return send_text_resource(dst, LCON_TYPE_REQUEST, resource, detail && detail[0] ? detail : "lease request");
}

int lconnect_grant(ip4_t dst, uint32_t resource, const char* detail)
{
    if (resource == 0 || (resource & ~allowed_resource_mask()) != 0) return -1;
    s_lc.grants++;
    return send_text_resource(dst, LCON_TYPE_GRANT, resource, detail && detail[0] ? detail : "manual grant");
}

int lconnect_deny(ip4_t dst, uint32_t resource, const char* detail)
{
    if (resource == 0 || (resource & ~allowed_resource_mask()) != 0) return -1;
    s_lc.denied++;
    return send_text_resource(dst, LCON_TYPE_DENY, resource, detail && detail[0] ? detail : "manual deny");
}

uint32_t lconnect_peer_count(void)
{
    return s_lc.peer_count;
}

int lconnect_peer_at(uint32_t idx, lconnect_peer_t* out)
{
    if (!out || idx >= s_lc.peer_count) return -1;
    *out = s_lc.peers[idx];
    return 0;
}

uint32_t lconnect_log_count(void)
{
    return s_lc.log_count;
}

int lconnect_log_at(uint32_t idx, lconnect_event_t* out)
{
    if (!out || idx >= s_lc.log_count) return -1;
    *out = s_lc.log[(s_lc.log_head + idx) % LCONNECT_LOG_DEPTH];
    return 0;
}

void lconnect_info(lconnect_info_t* out)
{
    if (!out) return;
    out->present = s_lc.present;
    out->configured = s_lc.configured;
    out->enabled = s_lc.enabled;
    out->auto_grant = s_lc.auto_grant;
    out->resources = s_lc.resources;
    out->peer_count = s_lc.peer_count;
    out->sent = s_lc.sent;
    out->received = s_lc.received;
    out->dropped = s_lc.dropped;
    out->grants = s_lc.grants;
    out->denied = s_lc.denied;
    out->clip_out = s_lc.clip_out;
    out->clip_in = s_lc.clip_in;
    out->leases = s_lc.leases;
    out->pending = s_lc.pending;
    out->last_error = s_lc.last_error;
    out->deprecated_input = s_lc.deprecated_input;
    out->deprecated_quiet = s_lc.deprecated_quiet;
    out->deprecated_events = s_lc.deprecated_events;
    out->ip = s_lc.cfg.ip;
    scopy(out->node, sizeof(out->node), s_lc.node);
}

int lconnect_selftest(void)
{
    lconnect_state_t saved = s_lc;
    uint8_t pkt[128];
    uint32_t len = 0;
    lconnect_packet_t parsed;
    lconnect_init(NULL, NULL, "self");
    if (lconnect_resource_from_name("all") != LCONNECT_RES_ALL) {
        s_lc = saved;
        return -1;
    }
    if (lconnect_resource_from_name("mouse") != 0 || lconnect_resource_from_name("keyboard") != 0) {
        s_lc = saved;
        return -2;
    }
    if (lconnect_deprecated_resource_from_name("input") != LCONNECT_RES_INPUT) {
        s_lc = saved;
        return -3;
    }
    if (lconnect_set_share(LCONNECT_RES_INPUT, 1) == 0) {
        s_lc = saved;
        return -4;
    }
    if (lconnect_deprecated_set_input(1, "confirm") != 0 ||
        lconnect_set_share(LCONNECT_RES_INPUT, 1) != 0 ||
        (lconnect_share_mask() & LCONNECT_RES_INPUT) != LCONNECT_RES_INPUT) {
        s_lc = saved;
        return -5;
    }
    if (lconnect_deprecated_set_quiet(1, "confirm") != 0 || !s_lc.deprecated_quiet) {
        s_lc = saved;
        return -6;
    }
    if (lconnect_set_share(LCONNECT_RES_ALL, 1) != 0 ||
        (lconnect_share_mask() & LCONNECT_RES_ALL) != LCONNECT_RES_ALL) {
        s_lc = saved;
        return -7;
    }
    if (build_packet(LCON_TYPE_REQUEST, LCONNECT_RES_CPU, (const uint8_t*)"cpu", 3u, pkt, sizeof(pkt), &len) != 0) {
        s_lc = saved;
        return -8;
    }
    if (parse_packet(pkt, len, &parsed) != 0 || parsed.type != LCON_TYPE_REQUEST ||
        parsed.resource != LCONNECT_RES_CPU || !streq_ci(parsed.node, "self")) {
        s_lc = saved;
        return -9;
    }
    s_lc = saved;
    return 0;
}
