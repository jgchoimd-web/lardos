#include "oslink.h"

#include "taskprio.h"

#include <stddef.h>
#include <stdint.h>

#define OSLINK_MAGIC0 'O'
#define OSLINK_MAGIC1 'S'
#define OSLINK_MAGIC2 'L'
#define OSLINK_MAGIC3 'K'
#define OSLINK_VERSION 1u

enum {
    OSLINK_TYPE_HELLO = 1,
    OSLINK_TYPE_PING = 2,
    OSLINK_TYPE_PONG = 3,
    OSLINK_TYPE_TEXT = 4,
    OSLINK_TYPE_ACK = 5,
    OSLINK_TYPE_EXEC = 6,
    OSLINK_TYPE_LOCAL = 7,
};

typedef struct {
    net_stack_t* net;
    net_cfg_t cfg;
    char node[OSLINK_NODE_MAX + 1u];
    uint32_t ready;
    uint32_t seq;
    uint32_t sent;
    uint32_t local_sent;
    uint32_t received;
    uint32_t dropped;
    uint32_t last_error;
    oslink_peer_t peers[OSLINK_PEER_MAX];
    uint32_t peer_count;
    oslink_msg_t inbox[OSLINK_INBOX_DEPTH];
    uint32_t inbox_count;
    uint32_t inbox_head;
    uint32_t inbox_tail;
} oslink_state_t;

static oslink_state_t s_oslink;

static uint32_t slen(const char* s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n] && n < cap) n++;
    return n;
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

static int streq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static void first_word(const char* in, char* out, uint32_t cap, const char** rest)
{
    uint32_t i = 0;
    if (!in) in = "";
    while (*in == ' ' || *in == '\t') in++;
    while (*in && *in != ' ' && *in != '\t' && i + 1u < cap) out[i++] = *in++;
    out[i] = '\0';
    while (*in == ' ' || *in == '\t') in++;
    if (rest) *rest = in;
}

static int has_control_pipe(const char* s)
{
    while (s && *s) {
        if (*s == '&' || *s == '|' || *s == ';') return 1;
        s++;
    }
    return 0;
}

static int rest_empty_or_word(const char* rest, const char* a, const char* b, const char* c)
{
    char word[16];
    const char* tail;
    first_word(rest, word, sizeof(word), &tail);
    if (!word[0]) return 1;
    if (tail && tail[0]) return 0;
    return streq(word, a) || (b && streq(word, b)) || (c && streq(word, c));
}

static int rest_word_allows_tail(const char* rest, const char* a, const char* b, const char* c)
{
    char word[16];
    first_word(rest, word, sizeof(word), NULL);
    if (!word[0]) return 0;
    return streq(word, a) || (b && streq(word, b)) || (c && streq(word, c));
}

static int safe_exec_command(const char* command)
{
    char cmd[32];
    const char* rest;
    first_word(command, cmd, sizeof(cmd), &rest);
    if (!cmd[0] || has_control_pipe(command)) return 0;
    if (streq(cmd, "help") || streq(cmd, "control") || streq(cmd, "status") ||
        streq(cmd, "release") || streq(cmd, "releases") || streq(cmd, "ver") ||
        streq(cmd, "post") || streq(cmd, "selftest") || streq(cmd, "tasktop") ||
        streq(cmd, "dir") || streq(cmd, "type") || streq(cmd, "lars") ||
        streq(cmd, "lardd") || streq(cmd, "doc") || streq(cmd, "larsform")) {
        return 1;
    }
    if (streq(cmd, "oslink")) return rest_empty_or_word(rest, "status", "peers", "poll");
    if (streq(cmd, "task") || streq(cmd, "tasks")) return rest_empty_or_word(rest, "list", "status", "ls");
    if (streq(cmd, "bootprof")) return rest_empty_or_word(rest, "status", "info", "list");
    if (streq(cmd, "awake") || streq(cmd, "awakening")) return rest_empty_or_word(rest, "status", "info", "test");
    if (streq(cmd, "crashlog")) return rest_empty_or_word(rest, "show", "status", NULL);
    if (streq(cmd, "lpack")) return rest_word_allows_tail(rest, "info", "list", "test");
    return 0;
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

static void remember_peer(ip4_t ip, const char* node)
{
    for (uint32_t i = 0; i < s_oslink.peer_count; i++) {
        if (ip_equal(s_oslink.peers[i].ip, ip)) {
            scopy(s_oslink.peers[i].node, sizeof(s_oslink.peers[i].node), node);
            s_oslink.peers[i].seen++;
            return;
        }
    }
    if (s_oslink.peer_count >= OSLINK_PEER_MAX) return;
    oslink_peer_t* p = &s_oslink.peers[s_oslink.peer_count++];
    p->ip = ip;
    scopy(p->node, sizeof(p->node), node);
    p->seen = 1;
}

static void queue_msg(ip4_t src, const char* src_node, const char* channel,
                      uint8_t type, uint32_t seq, const char* text)
{
    if (s_oslink.inbox_count >= OSLINK_INBOX_DEPTH) {
        s_oslink.dropped++;
        s_oslink.inbox_head = (s_oslink.inbox_head + 1u) % OSLINK_INBOX_DEPTH;
        s_oslink.inbox_count--;
    }
    oslink_msg_t* m = &s_oslink.inbox[s_oslink.inbox_tail];
    m->src_ip = src;
    m->type = type;
    m->seq = seq;
    scopy(m->src_node, sizeof(m->src_node), src_node);
    scopy(m->channel, sizeof(m->channel), channel);
    scopy(m->text, sizeof(m->text), text);
    s_oslink.inbox_tail = (s_oslink.inbox_tail + 1u) % OSLINK_INBOX_DEPTH;
    s_oslink.inbox_count++;
}

static int build_packet(uint8_t type, uint32_t seq, const char* text, uint8_t* out, uint32_t cap)
{
    uint32_t node_len = slen(s_oslink.node, OSLINK_NODE_MAX);
    uint32_t target_len = 0;
    uint32_t text_len = slen(text, OSLINK_TEXT_MAX);
    uint32_t need = 14u + node_len + target_len + text_len;
    if (!out || need > cap) return -1;
    out[0] = OSLINK_MAGIC0;
    out[1] = OSLINK_MAGIC1;
    out[2] = OSLINK_MAGIC2;
    out[3] = OSLINK_MAGIC3;
    out[4] = OSLINK_VERSION;
    out[5] = type;
    out[6] = (uint8_t)node_len;
    out[7] = (uint8_t)target_len;
    wr32(out + 8, seq);
    wr16(out + 12, (uint16_t)text_len);
    uint32_t p = 14;
    for (uint32_t i = 0; i < node_len; i++) out[p++] = (uint8_t)s_oslink.node[i];
    for (uint32_t i = 0; i < text_len; i++) out[p++] = (uint8_t)text[i];
    return (int)p;
}

static int parse_packet(const uint8_t* data, uint32_t len, uint8_t* type,
                        uint32_t* seq, char* src_node, uint32_t src_cap,
                        char* text, uint32_t text_cap)
{
    if (!data || len < 14u || !type || !seq || !src_node || !text) return -1;
    if (data[0] != OSLINK_MAGIC0 || data[1] != OSLINK_MAGIC1 ||
        data[2] != OSLINK_MAGIC2 || data[3] != OSLINK_MAGIC3) return -2;
    if (data[4] != OSLINK_VERSION) return -3;
    uint32_t src_len = data[6];
    uint32_t target_len = data[7];
    uint32_t text_len = rd16(data + 12);
    if (14u + src_len + target_len + text_len > len) return -4;
    *type = data[5];
    *seq = rd32(data + 8);
    uint32_t p = 14;
    uint32_t n = src_len < src_cap - 1u ? src_len : src_cap - 1u;
    for (uint32_t i = 0; i < n; i++) src_node[i] = (char)data[p + i];
    src_node[n] = '\0';
    p += src_len + target_len;
    n = text_len < text_cap - 1u ? text_len : text_cap - 1u;
    for (uint32_t i = 0; i < n; i++) text[i] = (char)data[p + i];
    text[n] = '\0';
    return 0;
}

static int send_type(ip4_t dst, uint8_t type, const char* text)
{
    uint8_t pkt[256];
    uint32_t seq = ++s_oslink.seq;
    int len = build_packet(type, seq, text ? text : "", pkt, sizeof(pkt));
    if (!s_oslink.ready || len < 0) {
        s_oslink.last_error = 1;
        return -1;
    }
    int r = net_udp_send(s_oslink.net, dst, OSLINK_PORT, OSLINK_PORT, pkt, (uint32_t)len);
    if (r == 0) {
        s_oslink.sent++;
        s_oslink.last_error = 0;
    } else {
        s_oslink.last_error = 2;
    }
    return r;
}

void oslink_init(net_stack_t* net, const net_cfg_t* cfg, const char* node)
{
    for (uint32_t i = 0; i < sizeof(s_oslink); i++) ((uint8_t*)&s_oslink)[i] = 0;
    s_oslink.net = net;
    if (cfg) s_oslink.cfg = *cfg;
    scopy(s_oslink.node, sizeof(s_oslink.node), node && node[0] ? node : "lardos");
    s_oslink.ready = (net && cfg) ? 1u : 0u;
    s_oslink.seq = 0x1000u;
}

int oslink_ready(void)
{
    return s_oslink.ready != 0;
}

void oslink_poll(void)
{
    if (!s_oslink.ready) return;
    for (uint32_t i = 0; i < 2u; i++) {
        uint8_t data[256];
        ip4_t src;
        uint16_t sport = 0;
        int r = net_udp_recv(s_oslink.net, OSLINK_PORT, data, sizeof(data), &src, &sport);
        if (r <= 0) return;
        uint8_t type = 0;
        uint32_t seq = 0;
        char src_node[OSLINK_NODE_MAX + 1u];
        char text[OSLINK_TEXT_MAX + 1u];
        if (parse_packet(data, (uint32_t)r, &type, &seq, src_node, sizeof(src_node), text, sizeof(text)) != 0) {
            s_oslink.dropped++;
            continue;
        }
        (void)sport;
        remember_peer(src, src_node);
        s_oslink.received++;
        if (type == OSLINK_TYPE_HELLO || type == OSLINK_TYPE_PING ||
            type == OSLINK_TYPE_TEXT || type == OSLINK_TYPE_EXEC) {
            queue_msg(src, src_node, "", type, seq, text);
        }
        if (type == OSLINK_TYPE_HELLO) {
            (void)send_type(src, OSLINK_TYPE_ACK, "hello");
        } else if (type == OSLINK_TYPE_PING) {
            (void)send_type(src, OSLINK_TYPE_PONG, text[0] ? text : "pong");
        } else if (type == OSLINK_TYPE_TEXT) {
            (void)send_type(src, OSLINK_TYPE_ACK, "text");
        } else if (type == OSLINK_TYPE_EXEC) {
            if (!safe_exec_command(text)) {
                (void)send_type(src, OSLINK_TYPE_ACK, "exec denied");
            } else {
                uint32_t id = 0;
                int qr = taskprio_enqueue("remote", text, 6, &id);
                (void)send_type(src, OSLINK_TYPE_ACK, qr == 0 ? "exec queued" : "exec queue full");
            }
        }
    }
}

int oslink_send_hello(ip4_t dst)
{
    return send_type(dst, OSLINK_TYPE_HELLO, "hello");
}

int oslink_send_ping(ip4_t dst, const char* text)
{
    return send_type(dst, OSLINK_TYPE_PING, text && text[0] ? text : "ping");
}

int oslink_send_text(ip4_t dst, const char* text)
{
    return send_type(dst, OSLINK_TYPE_TEXT, text && text[0] ? text : "");
}

int oslink_send_exec(ip4_t dst, const char* command)
{
    return send_type(dst, OSLINK_TYPE_EXEC, command && command[0] ? command : "");
}

int oslink_emit_local(const char* channel, const char* text)
{
    ip4_t zero = {{0, 0, 0, 0}};
    const char* ch = (channel && channel[0]) ? channel : "main";
    const char* body = text ? text : "";
    if (slen(ch, OSLINK_CHANNEL_MAX + 1u) == 0 || slen(body, OSLINK_TEXT_MAX + 1u) == 0) {
        s_oslink.last_error = 3;
        return -1;
    }
    queue_msg(zero, "local", ch, OSLINK_TYPE_LOCAL, ++s_oslink.seq, body);
    s_oslink.local_sent++;
    s_oslink.last_error = 0;
    return 0;
}

int oslink_recv(oslink_msg_t* out)
{
    if (!out || s_oslink.inbox_count == 0) return 0;
    *out = s_oslink.inbox[s_oslink.inbox_head];
    s_oslink.inbox_head = (s_oslink.inbox_head + 1u) % OSLINK_INBOX_DEPTH;
    s_oslink.inbox_count--;
    return 1;
}

uint32_t oslink_peer_count(void)
{
    return s_oslink.peer_count;
}

uint32_t oslink_local_count(void)
{
    uint32_t count = 0;
    uint32_t idx = s_oslink.inbox_head;
    for (uint32_t i = 0; i < s_oslink.inbox_count; i++) {
        if (s_oslink.inbox[idx].type == OSLINK_TYPE_LOCAL) count++;
        idx = (idx + 1u) % OSLINK_INBOX_DEPTH;
    }
    return count;
}

int oslink_peer_at(uint32_t idx, oslink_peer_t* out)
{
    if (!out || idx >= s_oslink.peer_count) return -1;
    *out = s_oslink.peers[idx];
    return 0;
}

void oslink_info(oslink_info_t* out)
{
    if (!out) return;
    out->ready = s_oslink.ready;
    out->port = OSLINK_PORT;
    out->sent = s_oslink.sent;
    out->received = s_oslink.received;
    out->dropped = s_oslink.dropped;
    out->inbox_count = s_oslink.inbox_count;
    out->local_count = oslink_local_count();
    out->local_sent = s_oslink.local_sent;
    out->peer_count = s_oslink.peer_count;
    out->last_error = s_oslink.last_error;
    out->ip = s_oslink.cfg.ip;
    scopy(out->node, sizeof(out->node), s_oslink.node);
}

int oslink_selftest(void)
{
    oslink_state_t saved;
    uint8_t pkt[256];
    oslink_msg_t msg;
    char src[OSLINK_NODE_MAX + 1u];
    char text[OSLINK_TEXT_MAX + 1u];
    uint8_t type;
    uint32_t seq;
    int len = build_packet(OSLINK_TYPE_EXEC, 0x44556677u, "status", pkt, sizeof(pkt));
    if (len <= 0) return -1;
    if (parse_packet(pkt, (uint32_t)len, &type, &seq, src, sizeof(src), text, sizeof(text)) != 0) return -2;
    if (type != OSLINK_TYPE_EXEC || seq != 0x44556677u) return -3;
    if (!streq(src, s_oslink.node)) return -4;
    if (!streq(text, "status")) return -5;
    if (!safe_exec_command("status") || safe_exec_command("poke 0 1 8")) return -6;
    saved = s_oslink;
    if (oslink_emit_local("self", "local-ok") != 0) {
        s_oslink = saved;
        return -7;
    }
    if (oslink_local_count() != 1u || oslink_recv(&msg) != 1 || msg.type != OSLINK_TYPE_LOCAL) {
        s_oslink = saved;
        return -8;
    }
    if (!streq(msg.src_node, "local") || !streq(msg.channel, "self") || !streq(msg.text, "local-ok")) {
        s_oslink = saved;
        return -9;
    }
    s_oslink = saved;
    return 0;
}
