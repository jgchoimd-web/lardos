#include "kmodtalk.h"

#include "awake.h"
#include "bootprof.h"
#include "cpumode.h"
#include "fs.h"
#include "gui.h"
#include "lardkit.h"
#include "lardtime.h"
#include "oslink.h"
#include "sysrxe.h"
#include "taskprio.h"
#include "version.h"
#include "vmmon.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

typedef int (*kmodtalk_handler_t)(const char* msg, char* out, uint32_t cap);

typedef struct {
    const char* name;
    const char* alias;
    const char* help;
    kmodtalk_handler_t handler;
} kmodtalk_module_t;

static uint32_t s_kmodtalk_seq;

static uint32_t slen_cap(const char* s, uint32_t cap)
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

static void append(char* dst, uint32_t cap, const char* src)
{
    uint32_t n = slen_cap(dst, cap);
    uint32_t i = 0;
    if (!dst || cap == 0 || !src) return;
    while (src[i] && n + 1u < cap) dst[n++] = src[i++];
    dst[n] = '\0';
}

static void append_u32(char* dst, uint32_t cap, uint32_t v)
{
    char tmp[16];
    uint32_t p = 0;
    if (v == 0) {
        append(dst, cap, "0");
        return;
    }
    while (v && p < sizeof(tmp)) {
        tmp[p++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (p) {
        char one[2];
        one[0] = tmp[--p];
        one[1] = '\0';
        append(dst, cap, one);
    }
}

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && lower_char(a[i]) == lower_char(b[i])) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int starts_ci(const char* s, const char* prefix)
{
    uint32_t i = 0;
    if (!s || !prefix) return 0;
    while (prefix[i]) {
        if (lower_char(s[i]) != lower_char(prefix[i])) return 0;
        i++;
    }
    return 1;
}

static const char* skip_ws(const char* s)
{
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static void first_word(const char* in, char* out, uint32_t cap, const char** rest)
{
    uint32_t i = 0;
    in = skip_ws(in);
    while (*in && *in != ' ' && *in != '\t' && i + 1u < cap) out[i++] = *in++;
    out[i] = '\0';
    in = skip_ws(in);
    if (rest) *rest = in;
}

static int parse_u32_word(const char* s, uint32_t* out)
{
    uint32_t v = 0;
    uint32_t i = 0;
    s = skip_ws(s);
    if (!s[0]) return -1;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10u + (uint32_t)(s[i] - '0');
        i++;
    }
    if (i == 0) return -1;
    if (s[i] && s[i] != ' ' && s[i] != '\t') return -1;
    if (out) *out = v;
    return 0;
}

static void record_line(const char* module, const char* msg, const char* result)
{
    FsWritableFile* w = fs_open_writable("kmodtalk.lardd");
    char line[256];
    const char* r = result ? result : "";
    if (!w) return;
    line[0] = '\0';
    append(line, sizeof(line), "ITEM #");
    append_u32(line, sizeof(line), s_kmodtalk_seq);
    append(line, sizeof(line), " ");
    append(line, sizeof(line), module && module[0] ? module : "unknown");
    append(line, sizeof(line), " <- ");
    append(line, sizeof(line), msg && msg[0] ? msg : "status");
    append(line, sizeof(line), " => ");
    for (uint32_t i = 0; r[i] && slen_cap(line, sizeof(line)) + 2u < sizeof(line); i++) {
        char c = r[i];
        if (c == '\n' || c == '\r') break;
        {
            char one[2];
            one[0] = c;
            one[1] = '\0';
            append(line, sizeof(line), one);
        }
    }
    append(line, sizeof(line), "\n");
    (void)fs_append(w, (const uint8_t*)line, slen_cap(line, sizeof(line)));
}

static int handle_gui(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    const char* rest;
    gui_cursor_info_t cur;
    gui_screenram_info_t sram;
    first_word(msg, word, sizeof(word), &rest);
    if (!word[0] || streq_ci(word, "status")) {
        gui_cursor_info(&cur);
        gui_screenram_info(&sram);
        snprintf(out, cap,
                 "gui: cursor=%s U+%04X assigned=%u renders=%u fallback=%u screenram=%s cap=%u used=%u",
                 cur.enabled ? "on" : "off", (unsigned)cur.cp, (unsigned)cur.assigned,
                 (unsigned)cur.render_count, (unsigned)cur.fallback_count,
                 sram.enabled ? "on" : "off", (unsigned)sram.capacity, (unsigned)sram.used);
        return 0;
    }
    if (streq_ci(word, "cursor")) {
        char sub[24];
        first_word(rest, sub, sizeof(sub), NULL);
        if (streq_ci(sub, "off")) {
            gui_cursor_disable();
            scopy(out, cap, "gui: cursor disabled");
            return 0;
        }
        if (streq_ci(sub, "mouse") || streq_ci(sub, "default")) {
            (void)gui_cursor_set_unicode(0xE004u);
            scopy(out, cap, "gui: cursor set to mouse glyph U+E004");
            return 0;
        }
        gui_cursor_info(&cur);
        snprintf(out, cap, "gui.cursor: enabled=%u cp=U+%04X assigned=%u last_error=%u",
                 (unsigned)cur.enabled, (unsigned)cur.cp, (unsigned)cur.assigned,
                 (unsigned)cur.last_error);
        return 0;
    }
    if (streq_ci(word, "screenram") || streq_ci(word, "sram")) {
        char sub[24];
        first_word(rest, sub, sizeof(sub), NULL);
        if (streq_ci(sub, "on")) {
            (void)gui_screenram_enable(1);
        } else if (streq_ci(sub, "off")) {
            (void)gui_screenram_enable(0);
        }
        gui_screenram_info(&sram);
        snprintf(out, cap, "gui.screenram: %s x=%u y=%u w=%u h=%u cap=%u used=%u",
                 sram.enabled ? "on" : "off", (unsigned)sram.x, (unsigned)sram.y,
                 (unsigned)sram.w, (unsigned)sram.h, (unsigned)sram.capacity,
                 (unsigned)sram.used);
        return 0;
    }
    scopy(out, cap, "gui: try status, cursor, cursor mouse, cursor off, screenram on/off");
    return 1;
}

static int handle_fs(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    uint32_t available = 0, dirty = 0, lba = 0, sectors = 0, generation = 0, bank = 0, bank_sectors = 0;
    int last = 0;
    const char* driver = NULL;
    first_word(msg, word, sizeof(word), NULL);
    if (streq_ci(word, "sync") || streq_ci(word, "save")) {
        int r = fs_persist_save();
        snprintf(out, cap, "fs: sync result=%d", r);
        return r == 0 ? 0 : 1;
    }
    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    fs_persist_detail(&bank, &generation, &bank_sectors);
    snprintf(out, cap,
             "fs: writable=%u lpst=%s dirty=%u driver=%s last=%d lba=%u sectors=%u bank=%u gen=%u",
             (unsigned)fs_writable_count(), available ? "online" : "offline", (unsigned)dirty,
             driver && driver[0] ? driver : "none", last, (unsigned)lba,
             (unsigned)sectors, (unsigned)bank, (unsigned)generation);
    return 0;
}

static int handle_task(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    const char* rest;
    taskprio_info_t info;
    first_word(msg, word, sizeof(word), &rest);
    if (streq_ci(word, "default")) {
        uint32_t v;
        if (parse_u32_word(rest, &v) == 0 && v <= 10u) {
            taskprio_set_default((int32_t)v);
        }
    }
    taskprio_info(&info);
    snprintf(out, cap,
             "task: queued=%u runnable=%u paused=%u lev10=%u default=%d completed=%u",
             (unsigned)info.queued, (unsigned)info.runnable, (unsigned)info.paused,
             (unsigned)info.os_urgent, (int)info.default_priority, (unsigned)info.completed);
    return 0;
}

static int handle_oslink(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    const char* rest;
    oslink_info_t info;
    first_word(msg, word, sizeof(word), &rest);
    if (streq_ci(word, "emit")) {
        char channel[16];
        const char* body;
        first_word(rest, channel, sizeof(channel), &body);
        if (channel[0] && body && body[0]) {
            int r = oslink_emit_local(channel, body);
            snprintf(out, cap, "oslink: local emit %s", r == 0 ? "queued" : "failed");
            return r == 0 ? 0 : 1;
        }
    }
    oslink_info(&info);
    snprintf(out, cap,
             "oslink: %s node=%s port=%u inbox=%u local=%u sent=%u recv=%u peers=%u err=%u",
             info.ready ? "ready" : "offline", info.node, (unsigned)info.port,
             (unsigned)info.inbox_count, (unsigned)info.local_count,
             (unsigned)info.sent, (unsigned)info.received, (unsigned)info.peer_count,
             (unsigned)info.last_error);
    return 0;
}

static int handle_boot(const char* msg, char* out, uint32_t cap)
{
    (void)msg;
    bootprof_info_t bp;
    awake_info_t ai;
    cpu_mode_info_t cm;
    bootprof_info(&bp);
    awake_info(&ai);
    cpu_mode_info(&cm);
    snprintf(out, cap,
             "boot: profile=%s safe=%u net=%u post=%u awake=%u phase=%u/%u %s cpu=%s bridge=%s trips=%u",
             bp.name, (unsigned)bp.safe_mode, (unsigned)bp.network, (unsigned)bp.force_post,
             (unsigned)bp.awakening_mode, (unsigned)ai.phase, (unsigned)ai.total, ai.current,
             cpu_mode_current_name(), cm.bridge_ready ? "ready" : "offline",
             (unsigned)cm.roundtrip_count);
    return 0;
}

static int handle_time(const char* msg, char* out, uint32_t cap)
{
    (void)msg;
    lardtime_snapshot_t now;
    if (lardtime_now(&now) != 0) {
        scopy(out, cap, "time: unavailable");
        return 1;
    }
    snprintf(out, cap,
             "time: LT=%lld solar=%05u-%02u-%02u %02u:%02u:%02u dangun=%05u lunar=%05u-%02u-%02u",
             (long long)now.ticks,
             (unsigned)now.civil.year, (unsigned)now.civil.month, (unsigned)now.civil.day,
             (unsigned)now.civil.hour, (unsigned)now.civil.minute, (unsigned)now.civil.second,
             (unsigned)now.dangun_year,
             (unsigned)now.lunar.year, (unsigned)now.lunar.month, (unsigned)now.lunar.day);
    return 0;
}

static int handle_vm(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    uint32_t start = 0;
    uint32_t end = VMMON_COUNT;
    first_word(msg, word, sizeof(word), NULL);
    out[0] = '\0';
    if (word[0] >= '0' && word[0] <= '9' && parse_u32_word(word, &start) == 0 && start < VMMON_COUNT) end = start + 1u;
    append(out, cap, "vm:");
    for (uint32_t i = start; i < end; i++) {
        vmmon_entry_t e;
        if (vmmon_info(i, &e) != 0) continue;
        append(out, cap, " ");
        append(out, cap, e.name);
        append(out, cap, "(runs=");
        append_u32(out, cap, e.runs);
        append(out, cap, ",fail=");
        append_u32(out, cap, e.failures);
        append(out, cap, ",budget=");
        append_u32(out, cap, e.budget_hits);
        append(out, cap, ")");
    }
    return 0;
}

static int handle_sysrxe(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    const char* rest;
    first_word(msg, word, sizeof(word), &rest);
    if (streq_ci(word, "reload")) {
        (void)sysrxe_reload();
    }
    out[0] = '\0';
    append(out, cap, "sysrxe: apps=");
    append_u32(out, cap, sysrxe_count());
    for (uint32_t i = 0; i < sysrxe_count(); i++) {
        const sysrxe_app_t* app = sysrxe_get(i);
        if (!app) continue;
        append(out, cap, " [");
        append_u32(out, cap, i);
        append(out, cap, ":");
        append(out, cap, app->name);
        append(out, cap, "]");
    }
    (void)rest;
    return 0;
}

static int handle_lardkit(const char* msg, char* out, uint32_t cap)
{
    char word[24];
    lardkit_trace_info_t ti;
    lardkit_netwatch_info_t ni;
    lardkit_bugeye_info_t bi;
    first_word(msg, word, sizeof(word), NULL);
    if (streq_ci(word, "trace-on")) lardkit_trace_enable(1);
    else if (streq_ci(word, "trace-off")) lardkit_trace_enable(0);
    lardkit_trace_info(&ti);
    lardkit_netwatch_info(&ni);
    lardkit_bugeye_info(&bi);
    snprintf(out, cap,
             "lardkit: trace=%s count=%u netwatch=%s http=%u oslink=%u bugeye=%s bugs=%u panicroom=%s",
             ti.enabled ? "on" : "off", (unsigned)ti.count,
             ni.enabled ? "on" : "off", (unsigned)ni.http, (unsigned)ni.oslink,
             bi.enabled ? "on" : "off", (unsigned)bi.bug_count,
             lardkit_panicroom_active() ? "entered" : "standby");
    return 0;
}

static const kmodtalk_module_t s_modules[] = {
    { "gui", "screen", "status, cursor, cursor mouse, cursor off, screenram on/off", handle_gui },
    { "fs", "filesystem", "status, sync", handle_fs },
    { "task", "taskprio", "status, default 0..10", handle_task },
    { "oslink", "link", "status, emit channel text", handle_oslink },
    { "boot", "bootprof", "status", handle_boot },
    { "time", "lardtime", "status", handle_time },
    { "vm", "vmmon", "status, index", handle_vm },
    { "sysrxe", "system-exec", "status, reload", handle_sysrxe },
    { "lardkit", "kit", "status, trace-on, trace-off", handle_lardkit },
};

void kmodtalk_init(void)
{
    s_kmodtalk_seq = 0;
}

uint32_t kmodtalk_module_count(void)
{
    return (uint32_t)(sizeof(s_modules) / sizeof(s_modules[0]));
}

const char* kmodtalk_module_name(uint32_t index)
{
    if (index >= kmodtalk_module_count()) return "";
    return s_modules[index].name;
}

const char* kmodtalk_module_help(uint32_t index)
{
    if (index >= kmodtalk_module_count()) return "";
    return s_modules[index].help;
}

static const kmodtalk_module_t* find_module(const char* module)
{
    if (!module || !module[0]) return NULL;
    for (uint32_t i = 0; i < kmodtalk_module_count(); i++) {
        if (streq_ci(module, s_modules[i].name) || streq_ci(module, s_modules[i].alias)) {
            return &s_modules[i];
        }
    }
    return NULL;
}

int kmodtalk_send(const char* module, const char* message, char* out, uint32_t out_cap)
{
    const kmodtalk_module_t* m;
    int r;
    if (!out || out_cap == 0) return -1;
    out[0] = '\0';
    m = find_module(module);
    if (!m) {
        scopy(out, out_cap, "kmodtalk: unknown module. Try kmod list.");
        return -2;
    }
    if (!message || !skip_ws(message)[0] || starts_ci(skip_ws(message), "help")) {
        snprintf(out, out_cap, "%s: %s", m->name, m->help);
        r = 0;
    } else {
        r = m->handler(skip_ws(message), out, out_cap);
    }
    s_kmodtalk_seq++;
    record_line(m->name, skip_ws(message), out);
    lardkit_trace_event("kmodtalk", m->name, r);
    lardkit_journal_event("kmodtalk", m->name);
    return r;
}

int kmodtalk_selftest(void)
{
    char out[256];
    if (kmodtalk_module_count() < 6u) return -1;
    if (!find_module("gui")) return -2;
    if (!find_module("filesystem")) return -3;
    if (kmodtalk_send("boot", "status", out, sizeof(out)) != 0) return -4;
    if (!out[0]) return -5;
    if (kmodtalk_send("nope", "status", out, sizeof(out)) >= 0) return -6;
    return 0;
}
