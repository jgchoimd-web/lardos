#include <stddef.h>
#include <stdint.h>
#include "unicode.h"
#include "net.h"
#include "oslink.h"
#include "awake.h"
#include "bootprof.h"
#include "taskprio.h"
#include "crashlog.h"
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
#include "post.h"
#include "gdt64.h"
#include "cpumode.h"
#include "syscall.h"
#include "usermode.h"
#include "lafillo.h"
#include "lard_doc.h"
#include "rtc.h"
#include "smp.h"
#include "string.h"
#include "fs.h"
#include "version.h"

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;

static size_t vga_pos;
static net_stack_t s_net;
static net_cfg_t s_cfg;
static int s_net_ready;
static char s_http_resp[4096];
static char s_boot_report[768];
static int s_boot_awakening_mode;
static uint32_t s_awake_background_phase;

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
    if (!buf || cap == 0) return;
    uint32_t i = 0;
    while (i + 1 < cap && buf[i]) {
        i++;
    }
    while (*s && i + 1 < cap) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
}

typedef struct {
    char* buf;
    uint32_t cap;
} boot_post_text_t;

static void boot_post_append(boot_post_text_t* text, const char* s)
{
    if (!text || !text->buf || text->cap == 0 || !s) return;
    uint32_t i = 0;
    while (i + 1 < text->cap && text->buf[i]) i++;
    while (*s && i + 1 < text->cap) text->buf[i++] = *s++;
    text->buf[i] = '\0';
}

static void boot_post_append_u32(boot_post_text_t* text, uint32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", v);
    boot_post_append(text, tmp);
}

static void boot_post_append_i32(boot_post_text_t* text, int v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d", v);
    boot_post_append(text, tmp);
}

static void boot_post_emit(const char* status, const char* name, void* user)
{
    boot_post_text_t* text = (boot_post_text_t*)user;
    boot_post_append(text, status);
    boot_post_append(text, " ");
    boot_post_append(text, name);
    boot_post_append(text, "\n");
}

static int boot_post_poll_key(int post_only)
{
    ps2_key_t k;
    if (ps2_kbd_poll(&k) == 0) {
        if (k.kind == PS2K_ASCII && (k.ch == 'p' || k.ch == 'P')) return 1;
        if (k.kind == PS2K_ASCII && (k.ch == 'm' || k.ch == 'M')) return 2;
        if (!post_only) return 1;
        return -1;
    }
    return 0;
}

static int boot_post_wait_key(uint32_t seconds, uint32_t fallback_loops, int post_only)
{
    int64_t start = rtc_unix_seconds();
    if (start > 0) {
        for (;;) {
            int r = boot_post_poll_key(post_only);
            if (r != 0) return r > 0 ? r : 0;
            int64_t now = rtc_unix_seconds();
            if (now > 0 && (uint32_t)(now - start) >= seconds) return 0;
            __asm__ __volatile__("pause");
        }
    }
    for (uint32_t i = 0; i < fallback_loops; i++) {
        int r = boot_post_poll_key(post_only);
        if (r != 0) return r > 0 ? r : 0;
        __asm__ __volatile__("pause");
    }
    return 0;
}

static int boot_post_offer(void)
{
    static const char* msg =
        "LardOS " LARDOS_VERSION " power-on options\n"
        "\n"
        "P  Power-On Self-Test\n"
        "M  CPU Mode Bridge Test\n"
        "Enter or timeout  Normal boot\n";
    gui_set_response(msg);
    gui_render();
    return boot_post_wait_key(4u, 240000000u, 1);
}

static void run_language_demos_report(int publish)
{
    char bosl_out[256];
    char lil_out[256];
    char gasm_out[256];
    int64_t lval;

    bosl_out[0] = '\0';
    lil_out[0] = '\0';
    gasm_out[0] = '\0';
    s_boot_report[0] = '\0';
    if (bosl_demo_hello(bosl_out, sizeof(bosl_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "BOSL: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "BOSL: ");
        append_line(s_boot_report, sizeof(s_boot_report), bosl_out);
    }
    if (bosl_demo_inline(bosl_out, sizeof(bosl_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "BOSL_ASM: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "BOSL_ASM: ");
        append_line(s_boot_report, sizeof(s_boot_report), bosl_out);
    }
    if (lil_demo_hello(lil_out, sizeof(lil_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "LIL: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "LIL: ");
        append_line(s_boot_report, sizeof(s_boot_report), lil_out);
    }
    if (lil_demo_inline(lil_out, sizeof(lil_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "LIL_ASM: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "LIL_ASM: ");
        append_line(s_boot_report, sizeof(s_boot_report), lil_out);
    }
    if (gasm_demo_hello(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "GASM: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "GASM: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
    }
    if (gasm_demo_inline(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "GASM_ASM: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "GASM_ASM: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
    }
    if (gasm_demo_oop(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "GASM_OOP: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "GASM_OOP: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
    }
    if (lafillo_demo_html(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "LAFILLO_VM: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "LAFILLO_VM: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
    }
    if (os_demo_hello(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "OS_VM: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "OS_VM: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
    }
    if (lml_demo(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "LML: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "LML: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
        append_line(s_boot_report, sizeof(s_boot_report), "\n");
    }
    if (gc_demo(gasm_out, sizeof(gasm_out)) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "GC: (failed)\n");
    } else {
        append_line(s_boot_report, sizeof(s_boot_report), "GC: ");
        append_line(s_boot_report, sizeof(s_boot_report), gasm_out);
    }
    if (lil_eval_int("(+ 40 2)", &lval) != 0) {
        append_line(s_boot_report, sizeof(s_boot_report), "LIL_EXPR: (failed)\n");
    } else {
        char lbuf[32];
        snprintf(lbuf, sizeof(lbuf), "%lld", (long long)lval);
        append_line(s_boot_report, sizeof(s_boot_report), "LIL_EXPR: ");
        append_line(s_boot_report, sizeof(s_boot_report), lbuf);
        append_line(s_boot_report, sizeof(s_boot_report), "\n");
    }
    if (publish) {
        gui_set_response(s_boot_report);
        gui_render();
    }
}

static void boot_mode_run_screen(void)
{
    static char out[1024];
    cpu_mode_info_t info;
    int r;

    out[0] = '\0';
    append_line(out, sizeof(out), "LardOS " LARDOS_VERSION " CPU Mode Bridge Test\n\n");
    append_line(out, sizeof(out), "Path: long64 -> protected32 -> real16 -> protected32 -> long64\n");
    r = cpu_mode_roundtrip_probe();
    cpu_mode_info(&info);
    append_line(out, sizeof(out), r == 0 ? "PASS real16/long64 roundtrip\n" : "FAIL real16/long64 roundtrip\n");
    append_line(out, sizeof(out), "Current: ");
    append_line(out, sizeof(out), cpu_mode_current_name());
    append_line(out, sizeof(out), "\nBridge: ");
    append_line(out, sizeof(out), info.bridge_ready ? "ready" : "offline");
    append_line(out, sizeof(out), ", trips=");
    char num[32];
    snprintf(num, sizeof(num), "%u", info.roundtrip_count);
    append_line(out, sizeof(out), num);
    append_line(out, sizeof(out), ", err=");
    snprintf(num, sizeof(num), "%u", info.last_error);
    append_line(out, sizeof(out), num);
    append_line(out, sizeof(out), "\n");

    gui_set_response(out);
    gui_render();
    vga_puts(r == 0 ? "CPU mode bridge OK\n" : "CPU mode bridge failed\n", r == 0 ? 0x2F : 0x4F);
    (void)boot_post_wait_key(6u, 360000000u, 0);
}

static void boot_post_run_screen(void)
{
    static char out[4096];
    lard_post_result_t post;
    boot_post_text_t text = { out, sizeof(out) };
    out[0] = '\0';
    boot_post_append(&text, "LardOS ");
    boot_post_append(&text, LARDOS_VERSION);
    boot_post_append(&text, " Power-On Self-Test\n\n");
    lard_post_run(boot_post_emit, &text, &post);
    boot_post_append(&text, "\nPOST: ");
    boot_post_append_u32(&text, post.pass);
    boot_post_append(&text, " passed, ");
    boot_post_append_u32(&text, post.fail);
    boot_post_append(&text, " failed");
    boot_post_append(&text, post.storage_available ? ", storage online" : ", storage offline");
    boot_post_append(&text, post.storage_dirty ? ", dirty" : ", clean");
    boot_post_append(&text, ", last=");
    boot_post_append_i32(&text, post.storage_last_result);
    boot_post_append(&text, ", gen=");
    boot_post_append_u32(&text, post.storage_generation);
    boot_post_append(&text, "\n");
    gui_set_response(out);
    gui_render();
    vga_puts(post.fail ? "POST found failures\n" : "POST OK\n", post.fail ? 0x4F : 0x2F);
    (void)boot_post_wait_key(6u, 360000000u, 0);
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

static void boot_network_start(int foreground)
{
    if (s_net_ready) return;
    if (!bootprof_network_enabled()) {
        if (foreground) vga_puts("Network skipped by boot profile\n", 0x2F);
        return;
    }
    if (net_init(&s_net) != 0) {
        awake_fail(10u, "net init");
        if (foreground) panic("NET init failed");
        return;
    }
    if (net_dhcp(&s_net, &s_cfg) != 0) {
        awake_fail(11u, "dhcp");
        if (foreground) panic("DHCP failed");
        return;
    }
    s_net_ready = 1;
    oslink_init(&s_net, &s_cfg, "lardos");
    if (foreground) vga_puts("DHCP OK\n", 0x2F);

    ip4_t ip;
    if (net_dns_a(&s_net, s_cfg.dns, "example.com", &ip) != 0) {
        awake_fail(12u, "dns");
        if (foreground) panic("DNS failed");
        return;
    }
    if (foreground) vga_puts("DNS OK\n", 0x2F);

    if (net_http_get(&s_net, ip, 80, "example.com", "/", s_http_resp, sizeof(s_http_resp)) == 0) {
        if (foreground) vga_puts("HTTP OK\n", 0x2F);
    } else if (foreground) {
        vga_puts("HTTP unavailable\n", 0x4F);
    }
}

static void awakening_background_poll(void)
{
    awake_info_t info;
    if (!s_boot_awakening_mode) return;
    awake_info(&info);
    if (!info.enabled || info.done) return;
    if (s_awake_background_phase == 0u) {
        awake_mark(1u, "drivers");
        drfl_load_all();
        lss_init();
        s_awake_background_phase = 1u;
        return;
    }
    if (s_awake_background_phase == 1u) {
        awake_mark(2u, "languages");
        run_language_demos_report(0);
        s_awake_background_phase = 2u;
        return;
    }
    if (s_awake_background_phase == 2u) {
        awake_mark(3u, "network");
        boot_network_start(0);
        s_awake_background_phase = 3u;
        awake_finish();
    }
}

void kmain(void)
{
    vga_pos = 0;
    vga_puts("Long mode (64-bit) OK\n", 0x0F);

    gdt64_init();
    idt64_init();
    syscall_init();
    mmu_init_protection();
    cpu_mode_init();
    usermode_init();

    gui_demo();
    smp_init();

    /* Custom language demos: BOSL (bytecode) + LIL (s-expr interpreter). */
    mem_init();
    fs_init();
    crashlog_init();
    bootprof_load();
    lsh_init();
    s_boot_awakening_mode = bootprof_awakening_mode();
    awake_enable(s_boot_awakening_mode, 3u);
    if (bootprof_dev_mode()) taskprio_set_default(7);
    if (s_boot_awakening_mode) {
        gui_set_response("LardOS ready.\n");
        gui_render();
        vga_puts("Awakening mode: fast surface ready\n", 0x2F);
    } else {
        drfl_load_all();
        lss_init();
        run_language_demos_report(1);
    }

    int ps2_ready = ps2_init();
    if (ps2_ready == 0) {
        if (s_boot_awakening_mode) {
            vga_puts("Awakening mode: POST prompt deferred\n", 0x2F);
        } else {
            int boot_choice = boot_post_offer();
            if (boot_choice == 1 || bootprof_force_post()) {
                boot_post_run_screen();
            } else if (boot_choice == 2) {
                boot_mode_run_screen();
            } else {
                gui_set_response(s_boot_report);
                gui_render();
            }
        }
    } else if (s_boot_awakening_mode) {
        vga_puts("Awakening mode: PS/2 unavailable, continuing\n", 0x2F);
    } else {
        vga_puts("PS/2 unavailable for POST option\n", 0x4F);
    }
    ps2_mouse_init();

    char* resp = s_http_resp;
    if (s_boot_awakening_mode) {
        vga_puts("Awakening mode: loaders continue in background\n", 0x2F);
    } else {
        boot_network_start(1);
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
            if (k.kind == PS2K_F10) {
                gui_activate_ring0_shortcut();
            } else if (k.kind == PS2K_ASCII) {
                syscall_key_push(k);
                gui_handle_key(k.ch);
            } else {
                syscall_key_push(k);
                gui_handle_key_nav((int)k.kind);
            }
            gui_render();
        }

        // URL submit -> fetch
        gui_http_request_t http_req;
        if (gui_take_submit(&http_req)) {
            gui_set_loading(1);
            gui_set_response("");
            gui_render();
            int is_file = 0;
            if (http_req.url[0] == 'f' && http_req.url[1] == 'i' && http_req.url[2] == 'l' && http_req.url[3] == 'e') {
                const char* path = (http_req.url[4] == ':' && http_req.url[5] == '/' && http_req.url[6] == '/') ? http_req.url + 7 : (http_req.url[4] == ':') ? http_req.url + 5 : NULL;
                if (path && path[0]) {
                    is_file = 1;
                    const FsFile* f = fs_open(path);
                    FsWritableFile* w = fs_open_writable(path);
                    const uint8_t* d = f ? f->data : (w ? w->data : NULL);
                    uint32_t sz = f ? f->size : (w ? w->size : 0);
                    if (d && sz < sizeof(s_http_resp)) {
                        uint32_t i;
                        for (i = 0; i < sz; i++) resp[i] = (char)d[i];
                        resp[i] = '\0';
                        static char doc_out[4096];
                        if (lard_doc_to_text(resp, (uint32_t)sz, doc_out, sizeof(doc_out)) == 0) {
                            gui_lafillo_set_content(doc_out, resp);
                        } else if (lafillo_http_to_text(resp, (uint32_t)sz, doc_out, sizeof(doc_out)) == 0) {
                            gui_lafillo_set_content(doc_out, resp);
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
                if (!s_net_ready) {
                    gui_set_response("Network disabled by boot profile");
                } else {
                    char host[128];
                    char host_hdr[160];
                    char path[512];
                    uint16_t url_port = 80;
                    if (parse_url(http_req.url, host, sizeof(host), host_hdr, sizeof(host_hdr), &url_port, path, sizeof(path)) != 0) {
                        gui_set_response("Bad URL");
                    } else {
                        ip4_t dip;
                        int is_https = (http_req.url[0] == 'h' && http_req.url[1] == 't' && http_req.url[2] == 't' && http_req.url[3] == 'p' && http_req.url[4] == 's' && http_req.url[5] == ':');
                        int have_ip = 0;
                        if (parse_ipv4_host(host, &dip) == 0) {
                            have_ip = 1;
                        } else if (net_dns_a(&s_net, s_cfg.dns, host, &dip) == 0) {
                            have_ip = 1;
                        } else {
                            gui_set_response("DNS failed");
                        }
                        if (have_ip) {
                            resp[0] = '\0';
                            int r = is_https ? net_https_request(&s_net, dip, url_port, host_hdr, path, http_req.method, http_req.body, http_req.body_len, resp, sizeof(s_http_resp))
                                             : net_http_request(&s_net, dip, url_port, host_hdr, path, http_req.method, http_req.body, http_req.body_len, resp, sizeof(s_http_resp));
                            if (r != 0) {
                                gui_set_response((is_https && resp[0]) ? resp : (is_https ? "HTTPS failed" : "HTTP failed"));
                            } else {
                                static char doc_out[4096];
                                if (lard_doc_to_text(resp, (uint32_t)strlen(resp), doc_out, sizeof(doc_out)) == 0) {
                                    gui_lafillo_set_content(doc_out, resp);
                                } else if (lafillo_http_to_text(resp, (uint32_t)strlen(resp), doc_out, sizeof(doc_out)) == 0) {
                                    gui_lafillo_set_content(doc_out, resp);
                                } else {
                                    gui_set_response(resp);
                                }
                            }
                        }
                    }
                }
            }
            gui_set_loading(0);
            gui_render();
        }

        gui_tick();
        awakening_background_poll();
        oslink_poll();
        if (gui_screensaver_active()) gui_render();
        __asm__ __volatile__("pause");
    }
}
