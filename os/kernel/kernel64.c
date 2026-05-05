#include <stddef.h>
#include <stdint.h>
#include "unicode.h"
#include "net.h"
#include "ps2.h"
#include "gui.h"
#include "idt64.h"
#include "panic.h"
#include "mmu.h"
#include "mem.h"
#include "bosl_demo.h"
#include "lil_demo.h"
#include "lil.h"
#include "gasm_demo.h"
#include "lafillo_demo.h"
#include "os_demo.h"
#include "lml_demo.h"
#include "gc_demo.h"
#include "fs.h"
#include "drfl.h"
#include "lss.h"
#include "lsh.h"
#include "gdt64.h"
#include "syscall.h"
#include "usermode.h"
#include "lafillo.h"
#include "smp.h"
#include "string.h"
#include "fs.h"

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;

static size_t vga_pos;

static void vga_put_byte(uint8_t b, void* userdata)
{
    uint8_t color = *(uint8_t*)userdata;
    if (b == '\n') {
        vga_pos = (vga_pos / 80 + 1) * 80;
        return;
    }
    if (b == '\r') {
        vga_pos = (vga_pos / 80) * 80;
        return;
    }
    if (vga_pos >= 80u * 25u) {
        return;
    }
    VGA[vga_pos++] = (uint16_t)color << 8 | (uint16_t)b;
}

static void vga_puts(const char* s, uint8_t color)
{
    unicode_utf8_to_cp437(vga_put_byte, &color, s);
}

static void append_line(char* buf, uint32_t cap, const char* s)
{
    uint32_t i = 0;
    while (i < cap && buf[i]) {
        i++;
    }
    while (*s && i + 1 < cap) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
}

static int parse_ipv4_host(const char* host, ip4_t* out_ip)
{
    if (!host || !out_ip) return -1;
    uint32_t seg = 0;
    uint32_t val = 0;
    int have = 0;
    for (uint32_t i = 0;; i++) {
        char c = host[i];
        if (c >= '0' && c <= '9') {
            val = val * 10u + (uint32_t)(c - '0');
            if (val > 255u) return -1;
            have = 1;
        } else if (c == '.' || c == '\0') {
            if (!have) return -1;
            if (seg >= 4) return -1;
            out_ip->b[seg++] = (uint8_t)val;
            val = 0;
            have = 0;
            if (c == '\0') break;
        } else {
            return -1;
        }
    }
    return seg == 4 ? 0 : -1;
}

/* Parse http(s)://host[:port][/path?query...]. host_out has no port; host_hdr is Host header value. */
static int parse_url(const char* url,
                     char* host_out,
                     uint32_t host_cap,
                     char* host_hdr,
                     uint32_t hdr_cap,
                     uint16_t* port_out,
                     char* path,
                     uint32_t path_cap)
{
    if (!url || !host_out || !host_hdr || !path || !port_out || host_cap < 2 || hdr_cap < 2 || path_cap < 2) return -1;
    uint32_t i = 0;
    uint16_t default_port = 80;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        if (url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/') {
            i = 8;
            default_port = 443;
        } else if (url[4] == ':' && url[5] == '/' && url[6] == '/') {
            i = 7;
            default_port = 80;
        } else {
            return -2;
        }
    }
    uint32_t hs = i;
    while (url[i] && url[i] != '/' && url[i] != '?' && url[i] != '#') {
        i++;
    }
    uint32_t he = i;
    if (he <= hs) return -3;

    uint32_t colon = 0;
    int has_colon = 0;
    for (uint32_t j = hs; j < he; j++) {
        if (url[j] == ':') {
            colon = j;
            has_colon = 1;
            break;
        }
    }
    uint32_t host_len = has_colon ? (colon - hs) : (he - hs);
    if (host_len == 0 || host_len >= host_cap) return -4;
    for (uint32_t j = 0; j < host_len; j++) host_out[j] = url[hs + j];
    host_out[host_len] = '\0';

    uint16_t port = default_port;
    if (has_colon) {
        uint32_t pi = colon + 1;
        uint32_t pv = 0;
        int any = 0;
        while (pi < he && url[pi] >= '0' && url[pi] <= '9') {
            pv = pv * 10u + (uint32_t)(url[pi] - '0');
            if (pv > 65535u) return -5;
            any = 1;
            pi++;
        }
        if (!any || pi != he || pv == 0) return -5;
        port = (uint16_t)pv;
    }
    *port_out = port;

    uint32_t copy_to = he - hs;
    if (copy_to >= hdr_cap) copy_to = hdr_cap - 1;
    for (uint32_t j = 0; j < copy_to; j++) host_hdr[j] = url[hs + j];
    host_hdr[copy_to] = '\0';

    if (url[i] == '\0') {
        path[0] = '/';
        path[1] = '\0';
        return 0;
    }
    uint32_t plen = 0;
    while (url[i] && plen + 1 < path_cap) {
        path[plen++] = url[i++];
    }
    path[plen] = '\0';
    return 0;
}

void kmain(void)
{
    vga_pos = 0;
    vga_puts("Long mode (64-bit) OK\n", 0x0F);

    gdt64_init();
    smp_init();  /* 코어 3개 이상이면 코어 1에서 보조 커널 구동 */
    idt64_init();
    syscall_init();
    mmu_init_protection();
    usermode_init();

    gui_demo();

    /* Custom language demos: BOSL (bytecode) + LIL (s-expr interpreter). */
    mem_init();
    fs_init();
    lsh_init();
    drfl_load_all();
    lss_init();
    char bosl_out[256];
    char lil_out[256];
    char gasm_out[256];
    char combined[640];
    bosl_out[0] = '\0';
    lil_out[0] = '\0';
    gasm_out[0] = '\0';
    combined[0] = '\0';
    if (bosl_demo_hello(bosl_out, sizeof(bosl_out)) != 0) {
        append_line(combined, sizeof(combined), "BOSL: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "BOSL: ");
        append_line(combined, sizeof(combined), bosl_out);
    }
    if (bosl_demo_inline(bosl_out, sizeof(bosl_out)) != 0) {
        append_line(combined, sizeof(combined), "BOSL_ASM: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "BOSL_ASM: ");
        append_line(combined, sizeof(combined), bosl_out);
    }
    if (lil_demo_hello(lil_out, sizeof(lil_out)) != 0) {
        append_line(combined, sizeof(combined), "LIL: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "LIL: ");
        append_line(combined, sizeof(combined), lil_out);
    }
    if (lil_demo_inline(lil_out, sizeof(lil_out)) != 0) {
        append_line(combined, sizeof(combined), "LIL_ASM: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "LIL_ASM: ");
        append_line(combined, sizeof(combined), lil_out);
    }
    if (gasm_demo_hello(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "GASM: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "GASM: ");
        append_line(combined, sizeof(combined), gasm_out);
    }
    if (gasm_demo_inline(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "GASM_ASM: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "GASM_ASM: ");
        append_line(combined, sizeof(combined), gasm_out);
    }
    if (gasm_demo_oop(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "GASM_OOP: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "GASM_OOP: ");
        append_line(combined, sizeof(combined), gasm_out);
    }
    if (lafillo_demo_html(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "LAFILLO_VM: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "LAFILLO_VM: ");
        append_line(combined, sizeof(combined), gasm_out);
    }
    if (os_demo_hello(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "OS_VM: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "OS_VM: ");
        append_line(combined, sizeof(combined), gasm_out);
    }
    if (lml_demo(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "LML: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "LML: ");
        append_line(combined, sizeof(combined), gasm_out);
        append_line(combined, sizeof(combined), "\n");
    }
    if (gc_demo(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(combined, sizeof(combined), "GC: (failed)\n");
    } else {
        append_line(combined, sizeof(combined), "GC: ");
        append_line(combined, sizeof(combined), gasm_out);
    }
    int64_t lval;
    if (lil_eval_int("(+ 40 2)", &lval) != 0) {
        append_line(combined, sizeof(combined), "LIL_EXPR: (failed)\n");
    } else {
        char lbuf[32];
        snprintf(lbuf, sizeof(lbuf), "%lld", (long long)lval);
        append_line(combined, sizeof(combined), "LIL_EXPR: ");
        append_line(combined, sizeof(combined), lbuf);
        append_line(combined, sizeof(combined), "\n");
    }
    gui_set_response(combined);
    gui_render();

    ps2_init();
    ps2_mouse_init();

    net_stack_t net;
    if (net_init(&net) != 0) {
        panic("NET init failed");
    }

    net_cfg_t cfg;
    if (net_dhcp(&net, &cfg) != 0) {
        panic("DHCP failed");
    }
    vga_puts("DHCP OK\n", 0x2F);

    ip4_t ip;
    if (net_dns_a(&net, cfg.dns, "example.com", &ip) != 0) {
        panic("DNS failed");
    }
    vga_puts("DNS OK\n", 0x2F);

    static char resp[4096];
    if (net_http_get(&net, ip, 80, "example.com", "/", resp, sizeof(resp)) == 0) {
        vga_puts("HTTP OK\n", 0x2F);
    } else {
        vga_puts("HTTP unavailable\n", 0x4F);
    }
    vga_puts("Native TLS loaded (external TLS removed)\n", 0x2F);

    for (;;) {
        int dx = 0, dy = 0, btn = 0;
        if (ps2_mouse_poll(&dx, &dy, &btn) == 0) {
            gui_handle_mouse(dx, dy, btn);
            gui_render();
        }
        ps2_key_t k;
        if (ps2_kbd_poll(&k) == 0) {
            syscall_key_push(k);
            if (k.kind == PS2K_ASCII) {
                gui_handle_key(k.ch);
            } else {
                gui_handle_key_nav((int)k.kind);
            }
            gui_render();
        }

        // URL submit -> fetch
        char url[256];
        if (gui_take_submit(url, sizeof(url))) {
            gui_set_loading(1);
            gui_set_response("");
            gui_render();
            int is_file = 0;
            if (url[0] == 'f' && url[1] == 'i' && url[2] == 'l' && url[3] == 'e') {
                const char* path = (url[4] == ':' && url[5] == '/' && url[6] == '/') ? url + 7 : (url[4] == ':') ? url + 5 : NULL;
                if (path && path[0]) {
                    is_file = 1;
                    const FsFile* f = fs_open(path);
                    FsWritableFile* w = fs_open_writable(path);
                    const uint8_t* d = f ? f->data : (w ? w->data : NULL);
                    uint32_t sz = f ? f->size : (w ? w->size : 0);
                    if (d && sz < sizeof(resp)) {
                        uint32_t i;
                        for (i = 0; i < sz; i++) resp[i] = (char)d[i];
                        resp[i] = '\0';
                        static char html_out[4096];
                        if (lafillo_http_to_text(resp, (uint32_t)sz, html_out, sizeof(html_out)) == 0) {
                            gui_lafillo_set_content(html_out, resp);
                        } else {
                            gui_set_response(resp);
                        }
                    } else {
                        gui_set_response("File not found");
                    }
                } else {
                    gui_set_response("Bad file URL");
                }
            }
            if (!is_file) {
            char host[128];
            char host_hdr[160];
            char path[512];
            uint16_t url_port = 80;
            if (parse_url(url, host, sizeof(host), host_hdr, sizeof(host_hdr), &url_port, path, sizeof(path)) != 0) {
                gui_set_response("Bad URL");
            } else {
                ip4_t dip;
                int is_https = (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' && url[4] == 's' && url[5] == ':');
                int have_ip = 0;
                if (parse_ipv4_host(host, &dip) == 0) {
                    have_ip = 1;
                } else if (net_dns_a(&net, cfg.dns, host, &dip) == 0) {
                    have_ip = 1;
                } else {
                    gui_set_response("DNS failed");
                }
                if (have_ip) {
                    resp[0] = '\0';
                    int r = is_https ? net_https_get(&net, dip, url_port, host_hdr, path, resp, sizeof(resp))
                                     : net_http_get(&net, dip, url_port, host_hdr, path, resp, sizeof(resp));
                    if (r != 0) {
                        gui_set_response((is_https && resp[0]) ? resp : (is_https ? "HTTPS failed" : "HTTP failed"));
                    } else {
                        static char html_out[4096];
                        if (lafillo_http_to_text(resp, (uint32_t)strlen(resp), html_out, sizeof(html_out)) == 0) {
                            gui_lafillo_set_content(html_out, resp);
                        } else {
                            gui_set_response(resp);
                        }
                    }
                }
            }
            }
            gui_set_loading(0);
            gui_render();
        }

        gui_tick();
        if (gui_screensaver_active()) gui_render();
        __asm__ __volatile__("pause");
    }
}
