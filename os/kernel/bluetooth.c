#include "bluetooth.h"

#include "fs.h"
#include "lardkit.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t enabled;
    uint32_t controller_present;
    uint32_t discoverable;
    uint32_t scanning;
    uint32_t hid_enabled;
    uint32_t scan_count;
    uint32_t sent;
    uint32_t received;
    uint32_t last_error;
    uint32_t seq;
    char controller[LBT_NAME_MAX + 1u];
    lbt_device_t devices[LBT_DEVICE_MAX];
    uint32_t device_count;
    lbt_event_t log[LBT_LOG_MAX];
    uint32_t log_count;
    uint32_t log_head;
} lbt_state_t;

static lbt_state_t s_lbt;

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    for (;;) {
        char ca = lower_char(a[i]);
        char cb = lower_char(b[i]);
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

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static int valid_addr(const char* addr)
{
    if (!addr) return 0;
    for (uint32_t i = 0; i < LBT_ADDR_MAX; i++) {
        if ((i % 3u) == 2u) {
            if (addr[i] != ':') return 0;
        } else if (hex_digit(addr[i]) < 0) {
            return 0;
        }
    }
    return addr[LBT_ADDR_MAX] == '\0';
}

static void normalize_addr(char* out, uint32_t cap, const char* addr)
{
    uint32_t i = 0;
    if (!out || cap == 0) return;
    if (!valid_addr(addr)) {
        out[0] = '\0';
        return;
    }
    while (addr[i] && i + 1u < cap) {
        out[i] = lower_char(addr[i]);
        i++;
    }
    out[i] = '\0';
}

static int find_device(const char* addr)
{
    char norm[LBT_ADDR_MAX + 1u];
    normalize_addr(norm, sizeof(norm), addr);
    if (!norm[0]) return -1;
    for (uint32_t i = 0; i < s_lbt.device_count; i++) {
        if (strcmp(s_lbt.devices[i].addr, norm) == 0) return (int)i;
    }
    return -1;
}

static void report_append(FsWritableFile* f, const char* s)
{
    uint32_t n = 0;
    if (!f || !s) return;
    while (s[n]) n++;
    (void)fs_append(f, (const uint8_t*)s, n);
}

static void report_u32(FsWritableFile* f, uint32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", v);
    report_append(f, tmp);
}

static void report_i32(FsWritableFile* f, int32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d", v);
    report_append(f, tmp);
}

static void log_event(const char* action, const char* addr, const char* detail)
{
    uint32_t idx;
    if (s_lbt.log_count < LBT_LOG_MAX) {
        idx = (s_lbt.log_head + s_lbt.log_count) % LBT_LOG_MAX;
        s_lbt.log_count++;
    } else {
        idx = s_lbt.log_head;
        s_lbt.log_head = (s_lbt.log_head + 1u) % LBT_LOG_MAX;
    }
    lbt_event_t* e = &s_lbt.log[idx];
    e->seq = ++s_lbt.seq;
    scopy(e->action, sizeof(e->action), action);
    scopy(e->addr, sizeof(e->addr), addr);
    scopy(e->detail, sizeof(e->detail), detail);
    lardkit_trace_event("bluetooth", action ? action : "event", (int32_t)e->seq);
}

static uint32_t count_paired(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_lbt.device_count; i++) if (s_lbt.devices[i].paired) n++;
    return n;
}

static uint32_t count_trusted(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_lbt.device_count; i++) if (s_lbt.devices[i].trusted) n++;
    return n;
}

void lbt_flags_list(uint32_t flags, char* out, uint32_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (flags & LBT_FLAG_HID) append(out, cap, "hid ");
    if (flags & LBT_FLAG_AUDIO) append(out, cap, "audio ");
    if (flags & LBT_FLAG_SERIAL) append(out, cap, "serial ");
    if (flags & LBT_FLAG_FILE) append(out, cap, "file ");
    if (!out[0] || (flags & LBT_FLAG_UNKNOWN)) append(out, cap, "unknown ");
}

uint32_t lbt_flags_from_name(const char* name)
{
    if (!name || !name[0]) return LBT_FLAG_UNKNOWN;
    if (streq_ci(name, "hid") || streq_ci(name, "keyboard") ||
        streq_ci(name, "mouse") || streq_ci(name, "input")) return LBT_FLAG_HID;
    if (streq_ci(name, "audio") || streq_ci(name, "speaker") ||
        streq_ci(name, "headset")) return LBT_FLAG_AUDIO;
    if (streq_ci(name, "serial") || streq_ci(name, "uart") ||
        streq_ci(name, "console")) return LBT_FLAG_SERIAL;
    if (streq_ci(name, "file") || streq_ci(name, "object") ||
        streq_ci(name, "transfer")) return LBT_FLAG_FILE;
    if (streq_ci(name, "all")) return LBT_FLAG_HID | LBT_FLAG_AUDIO | LBT_FLAG_SERIAL | LBT_FLAG_FILE;
    return LBT_FLAG_UNKNOWN;
}

void lbt_init(void)
{
    for (uint32_t i = 0; i < sizeof(s_lbt); i++) ((uint8_t*)&s_lbt)[i] = 0;
    scopy(s_lbt.controller, sizeof(s_lbt.controller), "none");
    s_lbt.seq = 0xB700u;
}

void lbt_controller_attach(const char* name)
{
    s_lbt.controller_present = 1u;
    scopy(s_lbt.controller, sizeof(s_lbt.controller), name && name[0] ? name : "hci");
    s_lbt.last_error = 0;
    log_event("controller", "", s_lbt.controller);
    (void)lbt_write_report();
}

void lbt_controller_detach(void)
{
    s_lbt.controller_present = 0u;
    scopy(s_lbt.controller, sizeof(s_lbt.controller), "none");
    log_event("detach", "", "controller removed");
    (void)lbt_write_report();
}

int lbt_enable(int on)
{
    s_lbt.enabled = on ? 1u : 0u;
    s_lbt.last_error = 0;
    log_event(on ? "on" : "off", "", on ? "radio requested on" : "radio off");
    (void)lbt_write_report();
    return 0;
}

int lbt_set_discoverable(int on)
{
    if (!s_lbt.enabled && on) {
        s_lbt.last_error = 1u;
        return -1;
    }
    s_lbt.discoverable = on ? 1u : 0u;
    log_event(on ? "discoverable" : "hidden", "", on ? "visible to peers" : "not advertising");
    (void)lbt_write_report();
    return 0;
}

int lbt_set_hid(int on)
{
    s_lbt.hid_enabled = on ? 1u : 0u;
    log_event(on ? "hid-on" : "hid-off", "", on ? "paired HID may control input" : "Bluetooth HID input blocked");
    (void)lbt_write_report();
    return 0;
}

int lbt_scan(void)
{
    s_lbt.scan_count++;
    s_lbt.scanning = 1u;
    if (!s_lbt.enabled) {
        s_lbt.last_error = 1u;
        log_event("scan", "", "failed: bluetooth is off");
        (void)lbt_write_report();
        return -1;
    }
    if (!s_lbt.controller_present) {
        s_lbt.last_error = 2u;
        log_event("scan", "", "queued: no controller driver");
        (void)lbt_write_report();
        return -2;
    }
    s_lbt.last_error = 0;
    log_event("scan", "", "controller inquiry requested");
    (void)lbt_write_report();
    return 0;
}

int lbt_add_manual(const char* addr, const char* name, uint32_t flags)
{
    char norm[LBT_ADDR_MAX + 1u];
    int idx;
    normalize_addr(norm, sizeof(norm), addr);
    if (!norm[0]) {
        s_lbt.last_error = 3u;
        return -1;
    }
    idx = find_device(norm);
    if (idx < 0) {
        if (s_lbt.device_count >= LBT_DEVICE_MAX) {
            s_lbt.last_error = 4u;
            return -2;
        }
        idx = (int)s_lbt.device_count++;
    }
    lbt_device_t* d = &s_lbt.devices[idx];
    scopy(d->addr, sizeof(d->addr), norm);
    scopy(d->name, sizeof(d->name), name && name[0] ? name : "bt-device");
    d->flags = flags ? flags : LBT_FLAG_UNKNOWN;
    d->rssi = 0;
    d->seen++;
    s_lbt.last_error = 0;
    log_event("add", d->addr, d->name);
    (void)lbt_write_report();
    return 0;
}

int lbt_pair(const char* addr, const char* pin)
{
    int idx = find_device(addr);
    if (idx < 0) {
        s_lbt.last_error = 5u;
        return -1;
    }
    s_lbt.devices[idx].paired = 1u;
    s_lbt.last_error = 0;
    log_event("pair", s_lbt.devices[idx].addr, pin && pin[0] ? "pin/passkey used, not stored" : "no pin stored");
    (void)lbt_write_report();
    return 0;
}

int lbt_unpair(const char* addr)
{
    int idx = find_device(addr);
    if (idx < 0) {
        s_lbt.last_error = 5u;
        return -1;
    }
    s_lbt.devices[idx].paired = 0u;
    s_lbt.devices[idx].trusted = 0u;
    log_event("unpair", s_lbt.devices[idx].addr, "pair/trust removed");
    (void)lbt_write_report();
    return 0;
}

int lbt_trust(const char* addr, int on)
{
    int idx = find_device(addr);
    if (idx < 0) {
        s_lbt.last_error = 5u;
        return -1;
    }
    if (on && !s_lbt.devices[idx].paired) {
        s_lbt.last_error = 6u;
        return -2;
    }
    s_lbt.devices[idx].trusted = on ? 1u : 0u;
    s_lbt.last_error = 0;
    log_event(on ? "trust" : "untrust", s_lbt.devices[idx].addr, on ? "trusted by user" : "trust removed");
    (void)lbt_write_report();
    return 0;
}

int lbt_send(const char* addr, const char* text, int force)
{
    int idx = find_device(addr);
    if (!s_lbt.enabled) {
        s_lbt.last_error = 1u;
        return -1;
    }
    if (idx < 0) {
        s_lbt.last_error = 5u;
        return -2;
    }
    if (!force && (!s_lbt.devices[idx].paired || !s_lbt.devices[idx].trusted)) {
        s_lbt.last_error = 7u;
        return -3;
    }
    s_lbt.sent++;
    if (!s_lbt.controller_present) {
        s_lbt.last_error = 2u;
        log_event(force ? "force-send" : "send", s_lbt.devices[idx].addr,
                  "queued only: no controller driver");
        (void)lbt_write_report();
        return -4;
    }
    s_lbt.last_error = 0;
    log_event(force ? "force-send" : "send", s_lbt.devices[idx].addr, text && text[0] ? text : "empty payload");
    (void)lbt_write_report();
    return 0;
}

int lbt_receive_note(const char* addr, const char* text)
{
    int idx = find_device(addr);
    if (idx < 0) {
        (void)lbt_add_manual(addr, "incoming", LBT_FLAG_UNKNOWN);
        idx = find_device(addr);
    }
    if (idx < 0) return -1;
    s_lbt.received++;
    log_event("recv", s_lbt.devices[idx].addr, text && text[0] ? text : "packet");
    (void)lbt_write_report();
    return 0;
}

uint32_t lbt_device_count(void)
{
    return s_lbt.device_count;
}

int lbt_device_at(uint32_t idx, lbt_device_t* out)
{
    if (!out || idx >= s_lbt.device_count) return -1;
    *out = s_lbt.devices[idx];
    return 0;
}

uint32_t lbt_log_count(void)
{
    return s_lbt.log_count;
}

int lbt_log_at(uint32_t idx, lbt_event_t* out)
{
    if (!out || idx >= s_lbt.log_count) return -1;
    *out = s_lbt.log[(s_lbt.log_head + idx) % LBT_LOG_MAX];
    return 0;
}

void lbt_info(lbt_info_t* out)
{
    if (!out) return;
    out->enabled = s_lbt.enabled;
    out->controller_present = s_lbt.controller_present;
    out->discoverable = s_lbt.discoverable;
    out->scanning = s_lbt.scanning;
    out->hid_enabled = s_lbt.hid_enabled;
    out->device_count = s_lbt.device_count;
    out->paired_count = count_paired();
    out->trusted_count = count_trusted();
    out->scan_count = s_lbt.scan_count;
    out->sent = s_lbt.sent;
    out->received = s_lbt.received;
    out->log_count = s_lbt.log_count;
    out->last_error = s_lbt.last_error;
    scopy(out->controller, sizeof(out->controller), s_lbt.controller);
}

int lbt_write_report(void)
{
    FsWritableFile* w = fs_open_writable("bt.lardd");
    lbt_info_t info;
    if (!w) return -1;
    w->size = 0;
    lbt_info(&info);
    report_append(w, "LARDD 1\nTITLE LardOS Bluetooth\n");
    report_append(w, "TEXT Bluetooth is local, visible, and off by default. No external stack is linked.\n");
    report_append(w, "SECTION Status\nITEM enabled ");
    report_append(w, info.enabled ? "yes" : "no");
    report_append(w, "\nITEM controller ");
    report_append(w, info.controller_present ? info.controller : "none");
    report_append(w, "\nITEM discoverable ");
    report_append(w, info.discoverable ? "yes" : "no");
    report_append(w, "\nITEM hid-input ");
    report_append(w, info.hid_enabled ? "yes" : "no");
    report_append(w, "\nITEM devices ");
    report_u32(w, info.device_count);
    report_append(w, "\nITEM paired ");
    report_u32(w, info.paired_count);
    report_append(w, "\nITEM trusted ");
    report_u32(w, info.trusted_count);
    report_append(w, "\nITEM scans ");
    report_u32(w, info.scan_count);
    report_append(w, "\nITEM sent ");
    report_u32(w, info.sent);
    report_append(w, "\nITEM received ");
    report_u32(w, info.received);
    report_append(w, "\nITEM last-error ");
    report_u32(w, info.last_error);
    report_append(w, "\nSECTION Devices\n");
    for (uint32_t i = 0; i < s_lbt.device_count; i++) {
        char flags[64];
        lbt_flags_list(s_lbt.devices[i].flags, flags, sizeof(flags));
        report_append(w, "ITEM ");
        report_append(w, s_lbt.devices[i].addr);
        report_append(w, " ");
        report_append(w, s_lbt.devices[i].name);
        report_append(w, " flags=");
        report_append(w, flags);
        report_append(w, " paired=");
        report_append(w, s_lbt.devices[i].paired ? "yes" : "no");
        report_append(w, " trusted=");
        report_append(w, s_lbt.devices[i].trusted ? "yes" : "no");
        report_append(w, " rssi=");
        report_i32(w, s_lbt.devices[i].rssi);
        report_append(w, "\n");
    }
    report_append(w, "SECTION Log\n");
    for (uint32_t i = 0; i < s_lbt.log_count; i++) {
        lbt_event_t e = s_lbt.log[(s_lbt.log_head + i) % LBT_LOG_MAX];
        report_append(w, "ITEM #");
        report_u32(w, e.seq);
        report_append(w, " ");
        report_append(w, e.action);
        if (e.addr[0]) {
            report_append(w, " ");
            report_append(w, e.addr);
        }
        if (e.detail[0]) {
            report_append(w, " ");
            report_append(w, e.detail);
        }
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    return 0;
}

int lbt_selftest(void)
{
    lbt_state_t saved = s_lbt;
    lbt_info_t info;
    int r = 0;
    lbt_init();
    if (lbt_enable(1) != 0) r = -1;
    if (!r && lbt_scan() != -2) r = -2;
    if (!r && lbt_add_manual("01:23:45:67:89:ab", "selftest", LBT_FLAG_SERIAL) != 0) r = -3;
    if (!r && lbt_pair("01:23:45:67:89:ab", "1234") != 0) r = -4;
    if (!r && lbt_trust("01:23:45:67:89:ab", 1) != 0) r = -5;
    if (!r && lbt_send("01:23:45:67:89:ab", "ping", 0) != -4) r = -6;
    lbt_info(&info);
    if (!r && (info.device_count != 1u || info.paired_count != 1u || info.trusted_count != 1u)) r = -7;
    if (!r && !fs_open_writable("bt.lardd")) r = -8;
    s_lbt = saved;
    (void)lbt_write_report();
    return r;
}
