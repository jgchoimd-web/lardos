#include "net.h"
#include "sock.h"
#include "rtl8139.h"
#include "drfl.h"
#include "lard_tls.h"
#include "lardkit.h"

#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} eth_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} arp_pkt_t;

typedef struct __attribute__((packed)) {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t hdr_csum;
    uint32_t src;
    uint32_t dst;
} ip4_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t csum;
} udp_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t off_res;
    uint8_t flags;
    uint16_t win;
    uint16_t csum;
    uint16_t urg;
} tcp_hdr_t;

typedef struct net_stack_impl {
    rtl8139_t nic;
    uint8_t gw_mac[6];
    int gw_mac_valid;
    uint16_t ip_id;
    uint32_t tcp_iss;
    uint8_t rx[2048];
    net_cfg_t cfg;
    int cfg_valid;
} net_stack_impl_t;

static inline net_stack_impl_t* impl(net_stack_t* n) { return (net_stack_impl_t*)(void*)n; }

static uint16_t bswap16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static uint32_t bswap32(uint32_t x)
{
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}
static uint16_t htons(uint16_t x) { return bswap16(x); }
static uint16_t ntohs(uint16_t x) { return bswap16(x); }
static uint32_t htonl(uint32_t x) { return bswap32(x); }
static uint32_t ntohl(uint32_t x) { return bswap32(x); }

static uint32_t ip_u32(ip4_t ip)
{
    return ((uint32_t)ip.b[0] << 24) | ((uint32_t)ip.b[1] << 16) | ((uint32_t)ip.b[2] << 8) |
           (uint32_t)ip.b[3];
}

static void mac_broadcast(uint8_t m[6])
{
    for (int i = 0; i < 6; i++) m[i] = 0xFF;
}

static uint16_t csum16(const void* data, uint32_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t csum_pseudo_ip4(ip4_t src, ip4_t dst, uint8_t proto, const void* l4, uint16_t l4_len)
{
    uint32_t sum = 0;
    uint32_t s = ip_u32(src);
    uint32_t d = ip_u32(dst);
    sum += (s >> 16) & 0xFFFFu;
    sum += s & 0xFFFFu;
    sum += (d >> 16) & 0xFFFFu;
    sum += d & 0xFFFFu;
    sum += proto;
    sum += l4_len;

    const uint8_t* p = (const uint8_t*)l4;
    uint32_t len = l4_len;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static int nic_rx(net_stack_t* n, uint8_t** out, uint32_t* out_len)
{
    net_stack_impl_t* s = impl(n);
    uint32_t got = 0;
    int r = rtl8139_poll_rx(&s->nic, s->rx, sizeof(s->rx), &got);
    if (r != 0) return r;
    *out = s->rx;
    *out_len = got;
    return 0;
}

static int arp_resolve(net_stack_t* n, ip4_t our_ip, ip4_t target_ip, uint8_t out_mac[6])
{
    net_stack_impl_t* s = impl(n);
    // Send ARP request and poll for reply.
    uint8_t frame[64];
    eth_hdr_t* eth = (eth_hdr_t*)frame;
    mac_broadcast(eth->dst);
    for (int i = 0; i < 6; i++) eth->src[i] = s->nic.mac[i];
    eth->ethertype = htons(0x0806);

    arp_pkt_t* arp = (arp_pkt_t*)(frame + sizeof(eth_hdr_t));
    arp->htype = htons(1);
    arp->ptype = htons(0x0800);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(1);
    for (int i = 0; i < 6; i++) arp->sha[i] = s->nic.mac[i];
    for (int i = 0; i < 4; i++) arp->spa[i] = our_ip.b[i];
    for (int i = 0; i < 6; i++) arp->tha[i] = 0;
    for (int i = 0; i < 4; i++) arp->tpa[i] = target_ip.b[i];

    rtl8139_send(&s->nic, frame, (uint32_t)(sizeof(eth_hdr_t) + sizeof(arp_pkt_t)));

    for (int tries = 0; tries < 50000; tries++) {
        uint8_t* rx = NULL;
        uint32_t rx_len = 0;
        if (nic_rx(n, &rx, &rx_len) != 0) continue;
        if (rx_len < sizeof(eth_hdr_t) + sizeof(arp_pkt_t)) continue;
        const eth_hdr_t* re = (const eth_hdr_t*)rx;
        if (ntohs(re->ethertype) != 0x0806) continue;
        const arp_pkt_t* ra = (const arp_pkt_t*)(rx + sizeof(eth_hdr_t));
        if (ntohs(ra->oper) != 2) continue;
        int match = 1;
        for (int i = 0; i < 4; i++) {
            if (ra->spa[i] != target_ip.b[i]) match = 0;
            if (ra->tpa[i] != our_ip.b[i]) match = 0;
        }
        if (!match) continue;
        for (int i = 0; i < 6; i++) out_mac[i] = ra->sha[i];
        return 0;
    }
    return -1;
}

static int ip_send_udp(net_stack_t* n,
                       const uint8_t dst_mac[6],
                       ip4_t src_ip,
                       ip4_t dst_ip,
                       uint16_t src_port,
                       uint16_t dst_port,
                       const void* payload,
                       uint16_t payload_len)
{
    net_stack_impl_t* s = impl(n);
    uint8_t frame[1514];
    eth_hdr_t* eth = (eth_hdr_t*)frame;
    for (int i = 0; i < 6; i++) eth->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = s->nic.mac[i];
    eth->ethertype = htons(0x0800);

    ip4_hdr_t* ip = (ip4_hdr_t*)(frame + sizeof(eth_hdr_t));
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    uint16_t ip_len = (uint16_t)(sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + payload_len);
    ip->total_len = htons(ip_len);
    ip->id = htons(s->ip_id++);
    ip->flags_frag = htons(0x4000);
    ip->ttl = 64;
    ip->proto = 17;
    ip->hdr_csum = 0;
    ip->src = htonl(ip_u32(src_ip));
    ip->dst = htonl(ip_u32(dst_ip));
    ip->hdr_csum = csum16(ip, sizeof(ip4_hdr_t));

    udp_hdr_t* udp = (udp_hdr_t*)((uint8_t*)ip + sizeof(ip4_hdr_t));
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->len = htons((uint16_t)(sizeof(udp_hdr_t) + payload_len));
    udp->csum = 0;

    uint8_t* pl = (uint8_t*)udp + sizeof(udp_hdr_t);
    const uint8_t* src = (const uint8_t*)payload;
    for (uint16_t i = 0; i < payload_len; i++) pl[i] = src[i];

    uint16_t csum = csum_pseudo_ip4(src_ip, dst_ip, 17, udp, (uint16_t)(sizeof(udp_hdr_t) + payload_len));
    if (csum == 0) csum = 0xFFFF;
    udp->csum = csum;

    uint32_t frame_len = sizeof(eth_hdr_t) + ip_len;
    return rtl8139_send(&s->nic, frame, frame_len);
}

int net_init(net_stack_t* n)
{
    net_stack_impl_t* s = impl(n);
    s->gw_mac_valid = 0;
    s->ip_id = 0x1234;
    s->tcp_iss = 0x10000;
    s->cfg_valid = 0;
    if (drfl_probe_net(&s->nic, "rtl8139", (drfl_net_init_fn)rtl8139_init) != 0) return -1;
    return 0;
}

int net_get_cfg(net_stack_t* n, net_cfg_t* out)
{
    net_stack_impl_t* s = impl(n);
    if (!s->cfg_valid) return -1;
    *out = s->cfg;
    return 0;
}

static int ip_is_broadcast(ip4_t ip)
{
    return ip.b[0] == 255 && ip.b[1] == 255 && ip.b[2] == 255 && ip.b[3] == 255;
}

static uint32_t ip4_pack(ip4_t ip)
{
    return ((uint32_t)ip.b[0] << 24) | ((uint32_t)ip.b[1] << 16) | ((uint32_t)ip.b[2] << 8) | (uint32_t)ip.b[3];
}

static int ip_same_subnet(ip4_t a, ip4_t b, ip4_t mask)
{
    uint32_t m = ip4_pack(mask);
    return (ip4_pack(a) & m) == (ip4_pack(b) & m);
}

int net_udp_send(net_stack_t* n,
                 ip4_t dst,
                 uint16_t src_port,
                 uint16_t dst_port,
                 const void* payload,
                 uint32_t payload_len)
{
    net_stack_impl_t* s = impl(n);
    net_cfg_t cfg;
    uint8_t mac[6];
    if (!n || (!payload && payload_len) || payload_len > 1400u) return -1;
    if (net_get_cfg(n, &cfg) != 0) return -2;
    if (ip_is_broadcast(dst)) {
        mac_broadcast(mac);
    } else if (ip_same_subnet(cfg.ip, dst, cfg.mask)) {
        if (arp_resolve(n, cfg.ip, dst, mac) != 0) return -3;
    } else {
        if (!s->gw_mac_valid) {
            if (arp_resolve(n, cfg.ip, cfg.gw, s->gw_mac) != 0) return -3;
            s->gw_mac_valid = 1;
        }
        for (int i = 0; i < 6; i++) mac[i] = s->gw_mac[i];
    }
    int r = ip_send_udp(n, mac, cfg.ip, dst, src_port, dst_port, payload, (uint16_t)payload_len);
    if (r == 0) lardkit_netwatch_record("udp-send", "packet", (int32_t)payload_len);
    return r;
}

int net_udp_recv(net_stack_t* n,
                 uint16_t dst_port,
                 void* out_payload,
                 uint32_t out_cap,
                 ip4_t* out_src,
                 uint16_t* out_src_port)
{
    if (!n || !out_payload || out_cap == 0) return -1;
    uint8_t* rx = NULL;
    uint32_t rx_len = 0;
    if (nic_rx(n, &rx, &rx_len) != 0) return 0;
    if (rx_len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t)) return 0;
    const eth_hdr_t* eth = (const eth_hdr_t*)rx;
    if (ntohs(eth->ethertype) != 0x0800) return 0;
    const ip4_hdr_t* ip = (const ip4_hdr_t*)(rx + sizeof(eth_hdr_t));
    if (ip->proto != 17) return 0;
    uint8_t ihl = (uint8_t)((ip->ver_ihl & 0x0Fu) * 4u);
    if (ihl < sizeof(ip4_hdr_t)) return 0;
    if (rx_len < sizeof(eth_hdr_t) + ihl + sizeof(udp_hdr_t)) return 0;
    const udp_hdr_t* udp = (const udp_hdr_t*)((const uint8_t*)ip + ihl);
    if (ntohs(udp->dst_port) != dst_port) return 0;
    uint16_t udp_len = ntohs(udp->len);
    if (udp_len < sizeof(udp_hdr_t)) return 0;
    if (rx_len < sizeof(eth_hdr_t) + ihl + udp_len) return 0;
    uint32_t pay_len = (uint32_t)udp_len - sizeof(udp_hdr_t);
    if (pay_len > out_cap) return -1;
    const uint8_t* pay = (const uint8_t*)udp + sizeof(udp_hdr_t);
    for (uint32_t i = 0; i < pay_len; i++) ((uint8_t*)out_payload)[i] = pay[i];
    if (out_src) {
        uint32_t src = ntohl(ip->src);
        out_src->b[0] = (uint8_t)(src >> 24);
        out_src->b[1] = (uint8_t)(src >> 16);
        out_src->b[2] = (uint8_t)(src >> 8);
        out_src->b[3] = (uint8_t)src;
    }
    if (out_src_port) *out_src_port = ntohs(udp->src_port);
    lardkit_netwatch_record("udp-recv", "packet", (int32_t)pay_len);
    return (int)pay_len;
}

// ---- DHCP (very small: DISCOVER/REQUEST) ----
typedef struct __attribute__((packed)) {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint8_t options[312];
} dhcp_t;

static uint32_t rnd_xid(const uint8_t mac[6])
{
    uint32_t x = 0xC0DEC0DEu;
    for (int i = 0; i < 6; i++) x = (x * 16777619u) ^ mac[i];
    return x;
}

static uint16_t opt_put(uint8_t* o, uint16_t off, uint8_t code, const void* data, uint8_t len)
{
    o[off++] = code;
    o[off++] = len;
    const uint8_t* p = (const uint8_t*)data;
    for (uint8_t i = 0; i < len; i++) o[off++] = p[i];
    return off;
}

static int dhcp_parse(const uint8_t* pkt, uint32_t len, uint32_t xid, uint8_t msg_type, net_cfg_t* out)
{
    if (len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + sizeof(dhcp_t)) return -1;
    const eth_hdr_t* eth = (const eth_hdr_t*)pkt;
    if (ntohs(eth->ethertype) != 0x0800) return -2;
    const ip4_hdr_t* ip = (const ip4_hdr_t*)(pkt + sizeof(eth_hdr_t));
    if (ip->proto != 17) return -3;
    const udp_hdr_t* udp = (const udp_hdr_t*)((const uint8_t*)ip + sizeof(ip4_hdr_t));
    if (ntohs(udp->src_port) != 67 || ntohs(udp->dst_port) != 68) return -4;
    const dhcp_t* d = (const dhcp_t*)((const uint8_t*)udp + sizeof(udp_hdr_t));
    if (d->op != 2) return -5;
    if (ntohl(d->xid) != xid) return -6;

    // options: look for message type, subnet mask(1), router(3), dns(6)
    if (d->options[0] != 99 || d->options[1] != 130 || d->options[2] != 83 || d->options[3] != 99) return -7;

    uint8_t got_type = 0;
    uint8_t got_dns = 0;
    uint8_t mask[4] = {255, 255, 255, 0};
    uint8_t gw[4] = {10, 0, 2, 2};
    uint8_t dns[4] = {10, 0, 2, 3};

    const uint8_t* o = d->options + 4;
    uint32_t i = 0;
    while (i < sizeof(d->options) - 4) {
        uint8_t code = o[i++];
        if (code == 0) continue;
        if (code == 255) break;
        if (i >= sizeof(d->options) - 4) break;
        uint8_t olen = o[i++];
        if (i + olen > sizeof(d->options) - 4) break;

        if (code == 53 && olen == 1) {
            got_type = o[i];
        } else if (code == 1 && olen == 4) {
            for (int k = 0; k < 4; k++) mask[k] = o[i + k];
        } else if (code == 3 && olen >= 4) {
            for (int k = 0; k < 4; k++) gw[k] = o[i + k];
        } else if (code == 6 && olen >= 4) {
            for (int k = 0; k < 4; k++) dns[k] = o[i + k];
            got_dns = 1;
        }

        i += olen;
    }

    if (got_type != msg_type) return -8;

    /* 공유기: DNS 미제공 시 게이트웨이를 DNS로 사용 (대부분 공유기가 DNS 프록시 제공) */
    if (!got_dns) {
        for (int k = 0; k < 4; k++) dns[k] = gw[k];
    }

    uint32_t yi = ntohl(d->yiaddr);
    out->ip = (ip4_t){{(uint8_t)(yi >> 24), (uint8_t)(yi >> 16), (uint8_t)(yi >> 8), (uint8_t)(yi >> 0)}};
    out->mask = (ip4_t){{mask[0], mask[1], mask[2], mask[3]}};
    out->gw = (ip4_t){{gw[0], gw[1], gw[2], gw[3]}};
    out->dns = (ip4_t){{dns[0], dns[1], dns[2], dns[3]}};
    return 0;
}

int net_dhcp(net_stack_t* n, net_cfg_t* out)
{
    net_stack_impl_t* s = impl(n);
    // QEMU usernet: DHCP is available; use broadcast.
    ip4_t ip0 = {{0, 0, 0, 0}};
    ip4_t bcast = {{255, 255, 255, 255}};
    uint8_t bmac[6];
    mac_broadcast(bmac);

    dhcp_t d;
    for (uint32_t i = 0; i < sizeof(d); i++) ((uint8_t*)&d)[i] = 0;
    d.op = 1;
    d.htype = 1;
    d.hlen = 6;
    d.xid = htonl(rnd_xid(s->nic.mac));
    d.flags = htons(0x8000);
    for (int i = 0; i < 6; i++) d.chaddr[i] = s->nic.mac[i];
    d.options[0] = 99;
    d.options[1] = 130;
    d.options[2] = 83;
    d.options[3] = 99;
    uint16_t off = 4;
    uint8_t mt = 1; // discover
    off = opt_put(d.options, off, 53, &mt, 1);
    uint8_t prl[] = {1, 3, 6};
    off = opt_put(d.options, off, 55, prl, (uint8_t)sizeof(prl));
    d.options[off++] = 255;

    ip_send_udp(n, bmac, ip0, bcast, 68, 67, &d, (uint16_t)sizeof(d));

    net_cfg_t offer;
    for (int tries = 0; tries < 200000; tries++) {
        uint8_t* rx = NULL;
        uint32_t rx_len = 0;
        if (nic_rx(n, &rx, &rx_len) != 0) continue;
        if (dhcp_parse(rx, rx_len, ntohl(d.xid), 2, &offer) == 0) {
            // request the offered IP (option 50). server id often 10.0.2.2; we can omit and rely on usernet.
            dhcp_t r;
            for (uint32_t i = 0; i < sizeof(r); i++) ((uint8_t*)&r)[i] = 0;
            r.op = 1;
            r.htype = 1;
            r.hlen = 6;
            r.xid = d.xid;
            r.flags = htons(0x8000);
            for (int i = 0; i < 6; i++) r.chaddr[i] = s->nic.mac[i];
            r.options[0] = 99;
            r.options[1] = 130;
            r.options[2] = 83;
            r.options[3] = 99;
            uint16_t roff = 4;
            uint8_t mt2 = 3;
            roff = opt_put(r.options, roff, 53, &mt2, 1);
            uint32_t reqip = htonl(ip_u32(offer.ip));
            roff = opt_put(r.options, roff, 50, &reqip, 4);
            r.options[roff++] = 255;
            ip_send_udp(n, bmac, ip0, bcast, 68, 67, &r, (uint16_t)sizeof(r));

            for (int tries2 = 0; tries2 < 200000; tries2++) {
                if (nic_rx(n, &rx, &rx_len) != 0) continue;
                net_cfg_t ack;
                if (dhcp_parse(rx, rx_len, ntohl(r.xid), 5, &ack) == 0) {
                    *out = ack;
                    for (int k = 0; k < 6; k++) out->mac[k] = s->nic.mac[k];
                    s->cfg = *out;
                    s->cfg_valid = 1;
                    return 0;
                }
            }
            return -3;
        }
    }
    return -2;
}

// ---- DNS (A query) ----
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd;
    uint16_t an;
    uint16_t ns;
    uint16_t ar;
} dns_hdr_t;

static uint16_t dns_write_name(uint8_t* buf, uint16_t off, const char* name)
{
    uint16_t label_len = 0;
    uint16_t label_pos = off++;
    for (uint16_t i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (c == '.') {
            buf[label_pos] = (uint8_t)label_len;
            label_len = 0;
            label_pos = off++;
            continue;
        }
        buf[off++] = (uint8_t)c;
        label_len++;
    }
    buf[label_pos] = (uint8_t)label_len;
    buf[off++] = 0;
    return off;
}

static uint16_t dns_read_name(const uint8_t* msg, uint16_t msg_len, uint16_t off, uint16_t* out_next)
{
    // Skip name (handles compression pointers).
    uint16_t i = off;
    for (;;) {
        if (i >= msg_len) return 0xFFFF;
        uint8_t l = msg[i++];
        if (l == 0) break;
        if ((l & 0xC0) == 0xC0) {
            if (i >= msg_len) return 0xFFFF;
            i++; // pointer
            break;
        }
        i = (uint16_t)(i + l);
    }
    *out_next = i;
    return 0;
}

int net_dns_a(net_stack_t* n, ip4_t dns, const char* name, ip4_t* out_ip)
{
    net_stack_impl_t* s = impl(n);
    net_cfg_t cfg;
    if (net_get_cfg(n, &cfg) != 0) {
        cfg.ip = (ip4_t){{10, 0, 2, 15}};
        cfg.gw = (ip4_t){{10, 0, 2, 2}};
    }

    // Resolve gateway MAC (needed for routing in usernet; L2 goes to gw).
    if (!s->gw_mac_valid) {
        if (arp_resolve(n, cfg.ip, cfg.gw, s->gw_mac) != 0) return -1;
        s->gw_mac_valid = 1;
    }

    uint8_t msg[512];
    for (uint16_t i = 0; i < sizeof(msg); i++) msg[i] = 0;
    dns_hdr_t* h = (dns_hdr_t*)msg;
    h->id = htons(0x1234);
    h->flags = htons(0x0100);
    h->qd = htons(1);
    uint16_t off = sizeof(dns_hdr_t);
    off = dns_write_name(msg, off, name);
    msg[off++] = 0;
    msg[off++] = 1; // A
    msg[off++] = 0;
    msg[off++] = 1; // IN

    // send to DNS via gateway L2
    ip_send_udp(n, s->gw_mac, cfg.ip, dns, 49152, 53, msg, off);

    for (int tries = 0; tries < 200000; tries++) {
        uint8_t* rx = NULL;
        uint32_t rx_len = 0;
        if (nic_rx(n, &rx, &rx_len) != 0) continue;
        if (rx_len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + sizeof(dns_hdr_t)) continue;
        const eth_hdr_t* eth = (const eth_hdr_t*)rx;
        if (ntohs(eth->ethertype) != 0x0800) continue;
        const ip4_hdr_t* ip = (const ip4_hdr_t*)(rx + sizeof(eth_hdr_t));
        if (ip->proto != 17) continue;
        const udp_hdr_t* udp = (const udp_hdr_t*)((const uint8_t*)ip + sizeof(ip4_hdr_t));
        if (ntohs(udp->src_port) != 53) continue;
        const uint8_t* dnsmsg = (const uint8_t*)udp + sizeof(udp_hdr_t);
        uint16_t dnslen = ntohs(udp->len);
        if (dnslen < sizeof(udp_hdr_t) + sizeof(dns_hdr_t)) continue;
        uint16_t pay = (uint16_t)(dnslen - sizeof(udp_hdr_t));
        const dns_hdr_t* rh = (const dns_hdr_t*)dnsmsg;
        if (rh->id != h->id) continue;
        uint16_t an = ntohs(rh->an);
        if (an == 0) continue;

        uint16_t p = sizeof(dns_hdr_t);
        uint16_t next = 0;
        dns_read_name(dnsmsg, pay, p, &next);
        if (next == 0xFFFF) continue;
        p = (uint16_t)(next + 4); // qtype/qclass
        for (uint16_t ai = 0; ai < an; ai++) {
            dns_read_name(dnsmsg, pay, p, &next);
            if (next == 0xFFFF) break;
            p = next;
            if (p + 10 > pay) break;
            uint16_t type = (uint16_t)((dnsmsg[p] << 8) | dnsmsg[p + 1]);
            uint16_t rdlen = (uint16_t)((dnsmsg[p + 8] << 8) | dnsmsg[p + 9]);
            p = (uint16_t)(p + 10);
            if (p + rdlen > pay) break;
            if (type == 1 && rdlen == 4) {
                out_ip->b[0] = dnsmsg[p + 0];
                out_ip->b[1] = dnsmsg[p + 1];
                out_ip->b[2] = dnsmsg[p + 2];
                out_ip->b[3] = dnsmsg[p + 3];
                return 0;
            }
            p = (uint16_t)(p + rdlen);
        }
    }
    return -2;
}

// ---- TCP + HTTP (minimal, no retransmit) ----
static int ip_send_tcp(net_stack_t* n,
                       const uint8_t dst_mac[6],
                       ip4_t src_ip,
                       ip4_t dst_ip,
                       uint16_t src_port,
                       uint16_t dst_port,
                       uint32_t seq,
                       uint32_t ack,
                       uint8_t flags,
                       const void* payload,
                       uint16_t payload_len)
{
    net_stack_impl_t* s = impl(n);
    uint8_t frame[1514];
    eth_hdr_t* eth = (eth_hdr_t*)frame;
    for (int i = 0; i < 6; i++) eth->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = s->nic.mac[i];
    eth->ethertype = htons(0x0800);

    ip4_hdr_t* ip = (ip4_hdr_t*)(frame + sizeof(eth_hdr_t));
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    uint16_t ip_len = (uint16_t)(sizeof(ip4_hdr_t) + sizeof(tcp_hdr_t) + payload_len);
    ip->total_len = htons(ip_len);
    ip->id = htons(s->ip_id++);
    ip->flags_frag = htons(0x4000);
    ip->ttl = 64;
    ip->proto = 6;
    ip->hdr_csum = 0;
    ip->src = htonl(ip_u32(src_ip));
    ip->dst = htonl(ip_u32(dst_ip));
    ip->hdr_csum = csum16(ip, sizeof(ip4_hdr_t));

    tcp_hdr_t* tcp = (tcp_hdr_t*)((uint8_t*)ip + sizeof(ip4_hdr_t));
    tcp->src_port = htons(src_port);
    tcp->dst_port = htons(dst_port);
    tcp->seq = htonl(seq);
    tcp->ack = htonl(ack);
    tcp->off_res = (uint8_t)(5 << 4);
    tcp->flags = flags;
    tcp->win = htons(4096);
    tcp->csum = 0;
    tcp->urg = 0;
    uint8_t* pl = (uint8_t*)tcp + sizeof(tcp_hdr_t);
    const uint8_t* src = (const uint8_t*)payload;
    for (uint16_t i = 0; i < payload_len; i++) pl[i] = src[i];

    tcp->csum = csum_pseudo_ip4(src_ip, dst_ip, 6, tcp, (uint16_t)(sizeof(tcp_hdr_t) + payload_len));
    return rtl8139_send(&s->nic, frame, (uint32_t)(sizeof(eth_hdr_t) + ip_len));
}

static int tcp_wait(net_stack_t* n,
                    ip4_t src_ip,
                    ip4_t dst_ip,
                    uint16_t src_port,
                    uint16_t dst_port,
                    uint32_t* out_seq_start,
                    uint32_t* out_ack,
                    char* out_data,
                    uint32_t* io_data_len,
                    uint8_t* out_flags)
{
    for (int tries = 0; tries < 400000; tries++) {
        uint8_t* rx = NULL;
        uint32_t rx_len = 0;
        if (nic_rx(n, &rx, &rx_len) != 0) continue;
        if (rx_len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(tcp_hdr_t)) continue;
        const eth_hdr_t* eth = (const eth_hdr_t*)rx;
        if (ntohs(eth->ethertype) != 0x0800) continue;
        const ip4_hdr_t* ip = (const ip4_hdr_t*)(rx + sizeof(eth_hdr_t));
        if (ip->proto != 6) continue;
        if (ntohl(ip->src) != ip_u32(dst_ip)) continue;
        if (ntohl(ip->dst) != ip_u32(src_ip)) continue;
        const tcp_hdr_t* tcp = (const tcp_hdr_t*)((const uint8_t*)ip + sizeof(ip4_hdr_t));
        if (ntohs(tcp->src_port) != dst_port) continue;
        if (ntohs(tcp->dst_port) != src_port) continue;
        uint8_t off = (tcp->off_res >> 4) & 0xF;
        uint16_t ip_tot = ntohs(ip->total_len);
        uint16_t hdrs = (uint16_t)(sizeof(ip4_hdr_t) + (off * 4));
        if (ip_tot < hdrs) continue;
        uint16_t pay = (uint16_t)(ip_tot - hdrs);

        uint32_t seq = ntohl(tcp->seq);
        uint32_t ack = ntohl(tcp->ack);
        *out_flags = tcp->flags;

        *out_seq_start = seq;
        *out_ack = ack;

        if (pay && out_data && io_data_len) {
            uint32_t cap = *io_data_len;
            uint32_t take = pay < cap ? pay : cap;
            const uint8_t* pl = (const uint8_t*)tcp + (off * 4);
            for (uint32_t i = 0; i < take; i++) out_data[i] = (char)pl[i];
            *io_data_len = take;
        } else if (io_data_len) {
            *io_data_len = 0;
        }
        return 0;
    }
    return -1;
}

typedef struct {
    ip4_t src_ip;
    ip4_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t snd_nxt; // next seq to send
    uint32_t snd_una; // oldest unacked seq
    uint32_t rcv_nxt; // next seq expected
    int established;

    // Last transmitted segment (for simple retransmit)
    uint32_t last_tx_seq;
    uint16_t last_tx_len;
    uint8_t last_tx_flags;
    uint8_t last_tx_buf[1460];
} tcp_conn_t;

typedef struct {
    tcp_conn_t c;
    uint8_t dst_mac[6];
    uint16_t dst_port;
} net_sock_impl_t;

static net_sock_impl_t* sock_impl(void* s) { return (net_sock_impl_t*)s; }

static int tcp_connect(net_stack_t* n, const uint8_t dst_mac[6], ip4_t src_ip, ip4_t dst_ip, uint16_t src_port, uint16_t dst_port, tcp_conn_t* out)
{
    net_stack_impl_t* s = impl(n);
    uint32_t iss = s->tcp_iss += 0x1000;
    uint32_t seq = iss;

    uint8_t flags = 0;
    uint32_t peer_seq0 = 0;
    uint32_t peer_ack = 0;

    // SYN with retries
    for (int attempt = 0; attempt < 5; attempt++) {
        ip_send_tcp(n, dst_mac, src_ip, dst_ip, src_port, dst_port, seq, 0, 0x02, NULL, 0);
        if (tcp_wait(n, src_ip, dst_ip, src_port, dst_port, &peer_seq0, &peer_ack, NULL, NULL, &flags) != 0) {
            continue;
        }
        if ((flags & 0x12) != 0x12) {
            continue;
        }
        // Expect ACK of our SYN (seq+1)
        if (peer_ack != (seq + 1)) {
            continue;
        }

        // ACK completes handshake
        uint32_t ack = peer_seq0 + 1;
        ip_send_tcp(n, dst_mac, src_ip, dst_ip, src_port, dst_port, seq + 1, ack, 0x10, NULL, 0);

        out->src_ip = src_ip;
        out->dst_ip = dst_ip;
        out->src_port = src_port;
        out->dst_port = dst_port;
        out->snd_nxt = seq + 1;
        out->snd_una = seq + 1;
        out->rcv_nxt = ack;
        out->established = 1;
        out->last_tx_seq = 0;
        out->last_tx_len = 0;
        out->last_tx_flags = 0;
        return 0;
    }
    return -1;
}

static int tcp_send_psh_ack(net_stack_t* n, const uint8_t dst_mac[6], tcp_conn_t* c, const void* data, uint16_t len)
{
    if (!c->established) return -1;
    if (len > sizeof(c->last_tx_buf)) return -2;
    ip_send_tcp(n, dst_mac, c->src_ip, c->dst_ip, c->src_port, c->dst_port, c->snd_nxt, c->rcv_nxt, 0x18, data, len);

    // Save for retransmit
    c->last_tx_seq = c->snd_nxt;
    c->last_tx_len = len;
    c->last_tx_flags = 0x18;
    const uint8_t* s = (const uint8_t*)data;
    for (uint16_t i = 0; i < len; i++) c->last_tx_buf[i] = s ? s[i] : 0;

    c->snd_nxt += len;
    return 0;
}

static int tcp_send_all(net_stack_t* n, const uint8_t dst_mac[6], tcp_conn_t* c, const void* data, uint32_t len)
{
    // TCP payload max for Ethernet MTU 1500 with no options: 1460 bytes.
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        uint16_t chunk = len > 1460 ? 1460 : (uint16_t)len;
        int r = tcp_send_psh_ack(n, dst_mac, c, p, chunk);
        if (r != 0) return r;
        p += chunk;
        len -= chunk;
    }
    return 0;
}

static int tcp_ack(net_stack_t* n, const uint8_t dst_mac[6], tcp_conn_t* c)
{
    if (!c->established) return -1;
    ip_send_tcp(n, dst_mac, c->src_ip, c->dst_ip, c->src_port, c->dst_port, c->snd_nxt, c->rcv_nxt, 0x10, NULL, 0);
    return 0;
}

static int tcp_fin_ack(net_stack_t* n, const uint8_t dst_mac[6], tcp_conn_t* c)
{
    if (!c->established) return -1;
    ip_send_tcp(n, dst_mac, c->src_ip, c->dst_ip, c->src_port, c->dst_port, c->snd_nxt, c->rcv_nxt, 0x11, NULL, 0);
    c->last_tx_seq = c->snd_nxt;
    c->last_tx_len = 0;
    c->last_tx_flags = 0x11;
    c->snd_nxt += 1; // FIN consumes one sequence number
    return 0;
}

static int tcp_retransmit_last(net_stack_t* n, const uint8_t dst_mac[6], tcp_conn_t* c)
{
    if (!c->established) return -1;
    if (c->last_tx_len == 0) return -2;
    ip_send_tcp(n,
                dst_mac,
                c->src_ip,
                c->dst_ip,
                c->src_port,
                c->dst_port,
                c->last_tx_seq,
                c->rcv_nxt,
                c->last_tx_flags,
                c->last_tx_buf,
                c->last_tx_len);
    return 0;
}

static int tcp_recv_some(net_stack_t* n, tcp_conn_t* c, char* out, uint32_t* io_len, uint8_t* out_flags)
{
    uint32_t seq0 = 0;
    uint32_t ack = 0;
    uint32_t cap = *io_len;
    int r = tcp_wait(n, c->src_ip, c->dst_ip, c->src_port, c->dst_port, &seq0, &ack, out, &cap, out_flags);
    if (r != 0) return r;

    // Track peer ACK for our sent data (snd_una)
    // Accept ACKs within [snd_una, snd_nxt].
    if (ack >= c->snd_una && ack <= c->snd_nxt) {
        c->snd_una = ack;
    }

    // Only accept in-order payload
    if (cap == 0) {
        *io_len = 0;
        return 0;
    }
    if (seq0 != c->rcv_nxt) {
        // Out-of-order; ignore payload. Caller should ACK current rcv_nxt.
        *io_len = 0;
        return 0;
    }
    c->rcv_nxt = seq0 + cap;
    *io_len = cap;
    return 0;
}

// -----------------------------
// Public "socket-like" API
// -----------------------------

int net_sock_connect(net_stack_t* n, ip4_t dst, uint16_t port, net_sock_t* out)
{
    net_stack_impl_t* st = impl(n);
    net_cfg_t cfg;
    if (net_get_cfg(n, &cfg) != 0) {
        cfg.ip = (ip4_t){{10, 0, 2, 15}};
        cfg.gw = (ip4_t){{10, 0, 2, 2}};
    }

    if (!st->gw_mac_valid) {
        if (arp_resolve(n, cfg.ip, cfg.gw, st->gw_mac) != 0) return -1;
        st->gw_mac_valid = 1;
    }

    net_sock_impl_t* s = sock_impl(out);
    for (uint32_t i = 0; i < sizeof(*s); i++) ((uint8_t*)s)[i] = 0;
    for (int i = 0; i < 6; i++) s->dst_mac[i] = st->gw_mac[i];
    s->dst_port = port;

    // naive ephemeral port
    uint16_t sport = (uint16_t)(40000 + (st->tcp_iss & 0x0FFF));
    if (tcp_connect(n, s->dst_mac, cfg.ip, dst, sport, port, &s->c) != 0) return -2;
    return 0;
}

int net_sock_send(net_stack_t* n, net_sock_t* s_, const void* data, uint32_t len)
{
    net_sock_impl_t* s = sock_impl(s_);
    return tcp_send_all(n, s->dst_mac, &s->c, data, len);
}

int net_sock_recv(net_stack_t* n, net_sock_t* s_, char* out, uint32_t* io_len, uint8_t* out_flags)
{
    net_sock_impl_t* s = sock_impl(s_);
    int r = tcp_recv_some(n, &s->c, out, io_len, out_flags);
    if (r == 0) {
        // Always ACK current rcv_nxt to keep sender moving.
        tcp_ack(n, s->dst_mac, &s->c);
    }
    return r;
}

int net_sock_close(net_stack_t* n, net_sock_t* s_)
{
    net_sock_impl_t* s = sock_impl(s_);
    // Best-effort FIN and wait for ACK.
    tcp_fin_ack(n, s->dst_mac, &s->c);
    for (int i = 0; i < 200000; i++) {
        uint8_t f = 0;
        uint32_t cap = 0;
        (void)tcp_recv_some(n, &s->c, NULL, &cap, &f);
        if (s->c.snd_una >= s->c.snd_nxt) break;
    }
    return 0;
}

static int streq_prefix(const char* s, const char* pfx)
{
    for (uint32_t i = 0;; i++) {
        if (pfx[i] == '\0') return 1;
        if (s[i] == '\0') return 0;
        if (s[i] != pfx[i]) return 0;
    }
}

static uint32_t str_len(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static int http_method_is_post(const char* method)
{
    return method && method[0] == 'P' && method[1] == 'O' && method[2] == 'S' &&
           method[3] == 'T' && method[4] == '\0';
}

static int req_puts(char* out, uint32_t cap, uint32_t* pos, const char* s)
{
    if (!out || !pos || !s) return -1;
    while (*s) {
        if (*pos + 1u >= cap) return -2;
        out[(*pos)++] = *s++;
    }
    return 0;
}

static int req_putn(char* out, uint32_t cap, uint32_t* pos, const char* s, uint32_t n)
{
    if (!out || !pos || (!s && n)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        if (*pos + 1u >= cap) return -2;
        out[(*pos)++] = s[i];
    }
    return 0;
}

static int req_put_u32(char* out, uint32_t cap, uint32_t* pos, uint32_t v)
{
    char tmp[12];
    uint32_t n = 0;
    if (v == 0) {
        return req_puts(out, cap, pos, "0");
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n) {
        if (*pos + 1u >= cap) return -2;
        out[(*pos)++] = tmp[--n];
    }
    return 0;
}

static int build_http_request(char* req,
                              uint32_t cap,
                              const char* method,
                              const char* host,
                              const char* path,
                              const char* body,
                              uint32_t body_len,
                              uint32_t* out_len)
{
    if (!req || cap < 64 || !host || !path || !out_len) return -1;
    uint32_t p = 0;
    int is_post = http_method_is_post(method);
    const char* verb = is_post ? "POST" : "GET";
    if (!is_post) {
        body = NULL;
        body_len = 0;
    } else if (body && body_len == 0) {
        body_len = str_len(body);
    }

    if (req_puts(req, cap, &p, verb) != 0) return -2;
    if (req_puts(req, cap, &p, " ") != 0) return -2;
    if (path[0]) {
        if (req_puts(req, cap, &p, path) != 0) return -2;
    } else {
        if (req_puts(req, cap, &p, "/") != 0) return -2;
    }
    if (req_puts(req, cap, &p, " HTTP/1.0\r\nHost: ") != 0) return -2;
    if (req_puts(req, cap, &p, host) != 0) return -2;
    if (req_puts(req, cap, &p, "\r\nConnection: close\r\n") != 0) return -2;
    if (is_post) {
        if (req_puts(req, cap, &p, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ") != 0) return -2;
        if (req_put_u32(req, cap, &p, body_len) != 0) return -2;
        if (req_puts(req, cap, &p, "\r\n\r\n") != 0) return -2;
        if (body_len && req_putn(req, cap, &p, body, body_len) != 0) return -2;
    } else {
        if (req_puts(req, cap, &p, "\r\n") != 0) return -2;
    }
    req[p] = '\0';
    *out_len = p;
    return 0;
}

/* Split "host:port" in place for DNS; default_port if no ':'. */
static int dns_host_strip_port(char* host, uint16_t* port, uint16_t default_port)
{
    uint32_t i = 0;
    while (host[i] && host[i] != ':') {
        i++;
    }
    if (host[i] == ':') {
        uint32_t v = 0;
        uint32_t j = i + 1;
        while (host[j] && host[j] >= '0' && host[j] <= '9') {
            v = v * 10u + (uint32_t)(host[j] - '0');
            if (v > 65535u) {
                return -1;
            }
            j++;
        }
        if (host[j] != '\0' || j == i + 1) {
            return -1;
        }
        host[i] = '\0';
        *port = (uint16_t)v;
        return 0;
    }
    *port = default_port;
    return 0;
}

static void tls_sni_hostname(const char* host_hdr, char* out, uint32_t cap)
{
    uint32_t i = 0;
    while (host_hdr[i] && host_hdr[i] != ':') i++;
    if (i >= cap) i = cap - 1;
    uint32_t j = 0;
    while (j < i) { out[j] = host_hdr[j]; j++; }
    out[j] = '\0';
}

typedef struct {
    net_stack_t* n;
    net_sock_t* sock;
} lard_tls_bio_t;

static int lard_tls_net_send(void* ctx, const uint8_t* buf, uint32_t len)
{
    lard_tls_bio_t* b = (lard_tls_bio_t*)ctx;
    return net_sock_send(b->n, b->sock, buf, len);
}

static int lard_tls_net_recv(void* ctx, uint8_t* buf, uint32_t cap, uint32_t* out_len)
{
    lard_tls_bio_t* b = (lard_tls_bio_t*)ctx;
    if (!out_len) return -1;
    *out_len = 0;
    if (cap == 0) return 0;
    for (uint32_t tries = 0; tries < 200000u; tries++) {
        uint32_t n = cap;
        uint8_t flags = 0;
        int rr = net_sock_recv(b->n, b->sock, (char*)buf, &n, &flags);
        if (rr == 0 && n > 0) {
            *out_len = n;
            return 0;
        }
        if (flags & 0x04) return -2;
        if (flags & 0x01) return -3;
    }
    return 0;
}

static void set_tls_status(char* out, uint32_t out_cap, int code)
{
    if (!out || out_cap == 0) return;
    const char* a = "Native TLS is internal now; ";
    const char* b = lard_tls_status_text(code);
    const char* c = ". External TLS libraries are not linked.\n";
    uint32_t p = 0;
    for (uint32_t i = 0; a[i] && p + 1 < out_cap; i++) out[p++] = a[i];
    for (uint32_t i = 0; b[i] && p + 1 < out_cap; i++) out[p++] = b[i];
    for (uint32_t i = 0; c[i] && p + 1 < out_cap; i++) out[p++] = c[i];
    out[p] = '\0';
}

static int parse_status_code(const char* hdr, uint32_t hdr_len)
{
    // Expect: "HTTP/1.1 200 ..."
    // Find first space, then parse 3 digits.
    uint32_t i = 0;
    while (i < hdr_len && hdr[i] != ' ') i++;
    if (i >= hdr_len) return -1;
    while (i < hdr_len && hdr[i] == ' ') i++;
    if (i + 2 >= hdr_len) return -1;
    if (hdr[i] < '0' || hdr[i] > '9') return -1;
    if (hdr[i + 1] < '0' || hdr[i + 1] > '9') return -1;
    if (hdr[i + 2] < '0' || hdr[i + 2] > '9') return -1;
    return (hdr[i] - '0') * 100 + (hdr[i + 1] - '0') * 10 + (hdr[i + 2] - '0');
}

static int header_find_value(const char* hdr, uint32_t hdr_len, const char* key, uint32_t* out_val_off, uint32_t* out_val_len)
{
    // Simple case-sensitive "\r\nKey:" scan.
    uint32_t klen = str_len(key);
    for (uint32_t i = 0; i + klen + 2 < hdr_len; i++) {
        // line start: either i==0 or previous is '\n'
        if (i != 0 && hdr[i - 1] != '\n') continue;
        int ok = 1;
        for (uint32_t j = 0; j < klen; j++) {
            if (hdr[i + j] != key[j]) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        if (hdr[i + klen] != ':') continue;
        uint32_t p = i + klen + 1;
        while (p < hdr_len && (hdr[p] == ' ' || hdr[p] == '\t')) p++;
        uint32_t e = p;
        while (e < hdr_len && hdr[e] != '\r' && hdr[e] != '\n') e++;
        *out_val_off = p;
        *out_val_len = e - p;
        return 0;
    }
    return -1;
}

static uint32_t parse_u32_dec(const char* s, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        v = v * 10u + (uint32_t)(s[i] - '0');
    }
    return v;
}

static uint32_t parse_u32_hex(const char* s, uint32_t n, uint32_t* out_used)
{
    uint32_t v = 0;
    uint32_t i = 0;
    for (; i < n; i++) {
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');
        else break;
        v = (v << 4) | d;
    }
    *out_used = i;
    return v;
}

static int parse_location(const char* v, uint32_t vlen, char* out_host, uint32_t host_cap, char* out_path, uint32_t path_cap)
{
    // Supports:
    // - "/path"
    // - "http://host/path"
    // - "https://host/path" (caller keeps its current HTTP/HTTPS transport)
    if (vlen == 0) return -1;
    if (v[0] == '/') {
        // host unchanged -> empty
        out_host[0] = '\0';
        uint32_t n = vlen < (path_cap - 1) ? vlen : (path_cap - 1);
        for (uint32_t i = 0; i < n; i++) out_path[i] = v[i];
        out_path[n] = '\0';
        return 0;
    }

    uint32_t p = 0;
    if (vlen >= 7 && streq_prefix(v, "http://")) p = 7;
    else if (vlen >= 8 && streq_prefix(v, "https://")) p = 8;
    else return -2;

    // host until '/' or end
    uint32_t hs = p;
    while (p < vlen && v[p] != '/') p++;
    uint32_t hlen = p - hs;
    if (hlen == 0 || hlen >= host_cap) return -3;
    for (uint32_t i = 0; i < hlen; i++) out_host[i] = v[hs + i];
    out_host[hlen] = '\0';

    // path
    if (p >= vlen) {
        out_path[0] = '/';
        out_path[1] = '\0';
        return 0;
    }
    uint32_t plen = vlen - p;
    if (plen >= path_cap) plen = path_cap - 1;
    for (uint32_t i = 0; i < plen; i++) out_path[i] = v[p + i];
    out_path[plen] = '\0';
    return 0;
}

static int net_https_request_once(net_stack_t* n,
                                  ip4_t dst,
                                  uint16_t port,
                                  const char* host,
                                  const char* path,
                                  const char* method,
                                  const char* body,
                                  uint32_t body_len,
                                  char* out,
                                  uint32_t out_cap,
                                  int* out_status,
                                  char* out_loc,
                                  uint32_t loc_cap)
{
    net_sock_t sock;
    if (net_sock_connect(n, dst, port, &sock) != 0) return -2;

    char sni[128];
    tls_sni_hostname(host, sni, sizeof(sni));
    lard_tls_bio_t bio = { n, &sock };
    lard_tls_client_t tls;
    int tr = lard_tls_client_init(&tls, sni, &bio, lard_tls_net_send, lard_tls_net_recv);
    if (tr == 0) tr = lard_tls_client_handshake(&tls);
    if (tr != 0) {
        set_tls_status(out, out_cap, tr);
        net_sock_close(n, &sock);
        return tr;
    }

    char req[2048];
    uint32_t rlen = 0;
    if (build_http_request(req, sizeof(req), method, host, path, body, body_len, &rlen) != 0) {
        net_sock_close(n, &sock);
        return -6;
    }

    uint32_t sent = 0;
    while (sent < rlen) {
        int w = lard_tls_write(&tls, (const uint8_t*)req + sent, rlen - sent);
        if (w != 0) {
            set_tls_status(out, out_cap, w);
            net_sock_close(n, &sock);
            return w;
        }
        sent = rlen;
    }

    uint32_t out_len = 0;
    uint32_t header_end = 0;
    int have_headers = 0;
    uint32_t content_len = 0xFFFFFFFFu;
    int is_chunked = 0;
    int saw_fin = 0;
    uint32_t chunk_rem = 0;
    int chunk_done = 0;
    char chunk_line[16];
    uint32_t chunk_line_len = 0;

    while (out_len + 1 < out_cap && !chunk_done) {
        char buf[512];
        uint32_t cap = sizeof(buf);
        int rr = lard_tls_read(&tls, (uint8_t*)buf, sizeof(buf), &cap);
        if (rr != 0) {
            set_tls_status(out, out_cap, rr);
            net_sock_close(n, &sock);
            return rr;
        }
        if (cap == 0) { saw_fin = 1; break; }

        for (uint32_t bi = 0; bi < cap && out_len + 1 < out_cap && !chunk_done; bi++) {
            char ch = buf[bi];
            if (!have_headers) {
                out[out_len++] = ch;
                if (out_len >= 4 && out[out_len - 4] == '\r' && out[out_len - 3] == '\n' && out[out_len - 2] == '\r' && out[out_len - 1] == '\n') {
                    have_headers = 1;
                    header_end = out_len;
                    uint32_t fl = 0;
                    while (fl + 1 < header_end && !(out[fl] == '\r' && out[fl + 1] == '\n')) fl++;
                    *out_status = parse_status_code(out, fl);
                    uint32_t vo = 0, vl = 0;
                    if (header_find_value(out, header_end, "Content-Length", &vo, &vl) == 0)
                        content_len = parse_u32_dec(out + vo, vl);
                    if (header_find_value(out, header_end, "Transfer-Encoding", &vo, &vl) == 0) {
                        const char* k = "chunked";
                        for (uint32_t i = 0; i + 6 <= vl; i++) {
                            int ok = 1;
                            for (int j = 0; j < 7; j++) {
                                if (out[vo + i + (uint32_t)j] != k[j]) { ok = 0; break; }
                            }
                            if (ok) { is_chunked = 1; break; }
                        }
                    }
                    if (out_loc && loc_cap) {
                        if (header_find_value(out, header_end, "Location", &vo, &vl) == 0) {
                            uint32_t nc = vl < (loc_cap - 1) ? vl : (loc_cap - 1);
                            for (uint32_t i = 0; i < nc; i++) out_loc[i] = out[vo + i];
                            out_loc[nc] = '\0';
                        } else out_loc[0] = '\0';
                    }
                }
                continue;
            }
            if (!is_chunked) {
                out[out_len++] = ch;
                if (content_len != 0xFFFFFFFFu && (out_len - header_end) >= content_len) chunk_done = 1;
                continue;
            }
            if (chunk_rem == 0) {
                if (ch == '\r') continue;
                if (ch == '\n') {
                    uint32_t used = 0;
                    chunk_rem = parse_u32_hex(chunk_line, chunk_line_len, &used);
                    chunk_line_len = 0;
                    if (chunk_rem == 0) chunk_done = 1;
                } else {
                    if (chunk_line_len + 1 < sizeof(chunk_line)) chunk_line[chunk_line_len++] = ch;
                }
            } else {
                out[out_len++] = ch;
                chunk_rem--;
            }
        }
        if (saw_fin) break;
    }

    net_sock_close(n, &sock);
    if (out_len < out_cap) out[out_len] = '\0';
    return 0;
}

static int net_http_request_once(net_stack_t* n,
                                 ip4_t dst,
                                 uint16_t port,
                                 const char* host,
                                 const char* path,
                                 const char* method,
                                 const char* body,
                                 uint32_t body_len,
                                 char* out,
                                 uint32_t out_cap,
                                 int* out_status,
                                 char* out_loc,
                                 uint32_t loc_cap)
{
    net_sock_t sock;
    if (net_sock_connect(n, dst, port, &sock) != 0) return -2;

    // HTTP request
    char req[2048];
    uint32_t rlen = 0;
    if (build_http_request(req, sizeof(req), method, host, path, body, body_len, &rlen) != 0) {
        net_sock_close(n, &sock);
        return -6;
    }

    if (net_sock_send(n, &sock, req, rlen) != 0) {
        net_sock_close(n, &sock);
        return -5;
    }

    // Parse/accumulate
    uint32_t out_len = 0;
    uint32_t header_end = 0;
    int have_headers = 0;
    uint32_t content_len = 0xFFFFFFFFu;
    int is_chunked = 0;
    int idle = 0;
    int did_retx = 0;
    int saw_fin = 0;

    // Chunked decode state
    uint32_t chunk_rem = 0;
    int chunk_done = 0;
    char chunk_line[16];
    uint32_t chunk_line_len = 0;

    while (out_len + 1 < out_cap && !chunk_done) {
        char buf[512];
        uint32_t cap = sizeof(buf);
        uint8_t flags = 0;
        if (net_sock_recv(n, &sock, buf, &cap, &flags) != 0) {
            idle++;
            cap = 0;
        } else if (cap == 0) {
            idle++;
        } else {
            idle = 0;
        }

        if (flags & 0x04) {
            net_sock_close(n, &sock);
            return -4;
        }
        if (flags & 0x01) {
            saw_fin = 1;
        }

        // Write bytes into out; once headers are done, optionally decode chunked.
        for (uint32_t bi = 0; bi < cap && out_len + 1 < out_cap && !chunk_done; bi++) {
            char ch = buf[bi];

            if (!have_headers) {
                out[out_len++] = ch;
                if (out_len >= 4 &&
                    out[out_len - 4] == '\r' && out[out_len - 3] == '\n' && out[out_len - 2] == '\r' && out[out_len - 1] == '\n') {
                    have_headers = 1;
                    header_end = out_len;

                    // status
                    // first line ends at \r\n
                    uint32_t fl = 0;
                    while (fl + 1 < header_end && !(out[fl] == '\r' && out[fl + 1] == '\n')) fl++;
                    int st = parse_status_code(out, fl);
                    *out_status = st;

                    // content-length
                    uint32_t vo = 0, vl = 0;
                    if (header_find_value(out, header_end, "Content-Length", &vo, &vl) == 0) {
                        content_len = parse_u32_dec(out + vo, vl);
                    }
                    // transfer-encoding
                    if (header_find_value(out, header_end, "Transfer-Encoding", &vo, &vl) == 0) {
                        // look for "chunked"
                        const char* k = "chunked";
                        for (uint32_t i = 0; i + 6 <= vl; i++) {
                            int ok = 1;
                            for (int j = 0; j < 7; j++) {
                                if (out[vo + i + (uint32_t)j] != k[j]) {
                                    ok = 0;
                                    break;
                                }
                            }
                            if (ok) {
                                is_chunked = 1;
                                break;
                            }
                        }
                    }
                    // location
                    if (out_loc && loc_cap) {
                        if (header_find_value(out, header_end, "Location", &vo, &vl) == 0) {
                            uint32_t n = vl < (loc_cap - 1) ? vl : (loc_cap - 1);
                            for (uint32_t i = 0; i < n; i++) out_loc[i] = out[vo + i];
                            out_loc[n] = '\0';
                        } else {
                            out_loc[0] = '\0';
                        }
                    }
                }
                continue;
            }

            if (!is_chunked) {
                // Raw body
                out[out_len++] = ch;
                if (content_len != 0xFFFFFFFFu) {
                    uint32_t body_have = out_len - header_end;
                    if (body_have >= content_len) {
                        chunk_done = 1;
                        break;
                    }
                }
                continue;
            }

            // Chunked decoder: do NOT include chunk framing in output, keep headers as-is.
            // We append decoded body after headers.
            if (chunk_rem == 0) {
                // Read chunk size line (hex) until \n
                if (ch == '\r') continue;
                if (ch == '\n') {
                    uint32_t used = 0;
                    uint32_t sz = parse_u32_hex(chunk_line, chunk_line_len, &used);
                    chunk_line_len = 0;
                    chunk_rem = sz;
                    if (sz == 0) {
                        chunk_done = 1;
                        break;
                    }
                } else {
                    if (chunk_line_len + 1 < sizeof(chunk_line)) chunk_line[chunk_line_len++] = ch;
                }
            } else {
                // Consume data bytes
                out[out_len++] = ch;
                chunk_rem--;
                if (chunk_rem == 0) {
                    // Next bytes will be \r\n then next size line
                }
            }
        }

        // If we see nothing for a while, retransmit once.
        if (!did_retx && idle > 200) {
            did_retx = 1;
            net_sock_impl_t* si = sock_impl(&sock);
            tcp_retransmit_last(n, si->dst_mac, &si->c);
            tcp_ack(n, si->dst_mac, &si->c);
            idle = 0;
        }

        if (saw_fin && cap == 0) {
            // likely done
            break;
        }
    }

    net_sock_close(n, &sock);
    if (out_len < out_cap) out[out_len] = '\0';
    return 0;
}

int net_http_request(net_stack_t* n,
                     ip4_t dst,
                     uint16_t port,
                     const char* host,
                     const char* path,
                     const char* method,
                     const char* body,
                     uint32_t body_len,
                     char* out,
                     uint32_t out_cap)
{
    // Follow up to 1 redirect.
    net_cfg_t cfg;
    int is_post = http_method_is_post(method);
    lardkit_netwatch_record("http", is_post ? "POST" : "GET", (int32_t)body_len);
    lardkit_trace_event("net", is_post ? "http POST" : "http GET", (int32_t)port);
    if (net_get_cfg(n, &cfg) != 0) {
        cfg.dns = (ip4_t){{10, 0, 2, 3}};
    }

    char loc[192];
    int st = -1;
    int r = net_http_request_once(n, dst, port, host, path, method, body, body_len, out, out_cap, &st, loc, sizeof(loc));
    if (r != 0) return r;

    if ((st == 301 || st == 302 || st == 307 || st == 308) && loc[0] != '\0') {
        if (is_post && st != 307 && st != 308) return 0;
        char nhost[128];
        char npath[128];
        if (parse_location(loc, str_len(loc), nhost, sizeof(nhost), npath, sizeof(npath)) == 0) {
            const char* host_hdr_val = host;
            const char* use_path = npath;
            ip4_t use_dst = dst;
            uint16_t use_port = port;

            if (nhost[0] != '\0') {
                char hbuf[128];
                uint32_t nhlen = str_len(nhost);
                if (nhlen >= sizeof(hbuf)) {
                    return 0;
                }
                for (uint32_t i = 0; i <= nhlen; i++) hbuf[i] = nhost[i];
                uint16_t scheme_port = 80;
                if (streq_prefix(loc, "https://")) {
                    scheme_port = 443;
                }
                if (dns_host_strip_port(hbuf, &use_port, scheme_port) != 0) {
                    return 0;
                }
                if (net_dns_a(n, cfg.dns, hbuf, &use_dst) != 0) {
                    return 0;
                }
                host_hdr_val = nhost;
            }
            return net_http_request_once(n, use_dst, use_port, host_hdr_val, use_path, method, body, body_len, out, out_cap, &st, NULL, 0);
        }
    }
    return 0;
}

int net_http_get(net_stack_t* n, ip4_t dst, uint16_t port, const char* host, const char* path, char* out, uint32_t out_cap)
{
    return net_http_request(n, dst, port, host, path, "GET", NULL, 0, out, out_cap);
}

int net_https_request(net_stack_t* n,
                      ip4_t dst,
                      uint16_t port,
                      const char* host,
                      const char* path,
                      const char* method,
                      const char* body,
                      uint32_t body_len,
                      char* out,
                      uint32_t out_cap)
{
    net_cfg_t cfg;
    int is_post = http_method_is_post(method);
    lardkit_netwatch_record("https", is_post ? "POST" : "GET", (int32_t)body_len);
    lardkit_trace_event("net", is_post ? "https POST" : "https GET", (int32_t)port);
    if (net_get_cfg(n, &cfg) != 0) cfg.dns = (ip4_t){{10, 0, 2, 3}};
    if (out && out_cap) out[0] = '\0';

    char loc[192];
    int st = -1;
    int r = net_https_request_once(n, dst, port, host, path, method, body, body_len, out, out_cap, &st, loc, sizeof(loc));
    if (r != 0) return r;

    if ((st == 301 || st == 302 || st == 307 || st == 308) && loc[0] != '\0') {
        if (is_post && st != 307 && st != 308) return 0;
        char nhost[128];
        char npath[128];
        if (parse_location(loc, str_len(loc), nhost, sizeof(nhost), npath, sizeof(npath)) == 0) {
            const char* host_hdr_val = host;
            ip4_t use_dst = dst;
            uint16_t use_port = port;

            if (nhost[0] != '\0') {
                char hbuf[128];
                uint32_t nhlen = str_len(nhost);
                if (nhlen < sizeof(hbuf)) {
                    for (uint32_t i = 0; i <= nhlen; i++) hbuf[i] = nhost[i];
                    uint16_t scheme_port = 443;
                    if (streq_prefix(loc, "http://")) scheme_port = 80;
                    if (dns_host_strip_port(hbuf, &use_port, scheme_port) == 0 && net_dns_a(n, cfg.dns, hbuf, &use_dst) == 0) {
                        host_hdr_val = nhost;
                        if (streq_prefix(loc, "http://"))
                            return net_http_request_once(n, use_dst, use_port, host_hdr_val, npath, method, body, body_len, out, out_cap, &st, NULL, 0);
                        return net_https_request_once(n, use_dst, use_port, host_hdr_val, npath, method, body, body_len, out, out_cap, &st, NULL, 0);
                    }
                }
            }
            if (streq_prefix(loc, "http://"))
                return net_http_request_once(n, use_dst, use_port, host, npath, method, body, body_len, out, out_cap, &st, NULL, 0);
            return net_https_request_once(n, use_dst, use_port, host, npath, method, body, body_len, out, out_cap, &st, NULL, 0);
        }
    }
    return 0;
}

int net_https_get(net_stack_t* n, ip4_t dst, uint16_t port, const char* host, const char* path, char* out, uint32_t out_cap)
{
    return net_https_request(n, dst, port, host, path, "GET", NULL, 0, out, out_cap);
}
