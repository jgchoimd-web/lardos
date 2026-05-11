#include "lardkit.h"

#include "bootprof.h"
#include "fs.h"
#include "gui.h"
#include "lard_doc.h"
#include "lassist.h"
#include "screencheck.h"
#include "taskprio.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[LARDKIT_NAME_MAX + 1u];
    uint32_t fg;
    uint32_t bg;
    uint32_t accent;
    uint32_t style_hint;
} theme_state_t;

typedef struct {
    char name[64];
    uint32_t ok;
} post_baseline_entry_t;

typedef struct {
    lardkit_bugeye_info_t bugeye;
    lardkit_bugreplay_frame_t bugreplay[LARDKIT_BUGREPLAY_MAX];
    uint32_t bugreplay_count;
    uint32_t bugreplay_next;
    uint32_t trace_enabled;
    lardkit_trace_entry_t trace[LARDKIT_TRACE_MAX];
    uint32_t trace_count;
    uint32_t trace_next;
    uint32_t trace_tick;
    uint32_t netwatch_enabled;
    lardkit_netwatch_entry_t netwatch[LARDKIT_NETWATCH_MAX];
    uint32_t netwatch_count;
    uint32_t netwatch_next;
    lardkit_netwatch_info_t netwatch_info;
    uint32_t journal_next;
    post_baseline_entry_t post_prev[LARDKIT_POST_BASELINE_MAX];
    post_baseline_entry_t post_cur[LARDKIT_POST_BASELINE_MAX];
    lardkit_post_baseline_info_t post_info;
    uint32_t post_loaded;
    lardkit_cfg_profile_t cfgprof[LARDKIT_CFGPROF_MAX];
    lardkit_rollback_info_t rollback;
    lardkit_trust_entry_t trust[5];
    lardkit_trust_history_entry_t trust_history[LARDKIT_TRUST_HISTORY_MAX];
    uint32_t trust_history_count;
    uint32_t trust_history_next;
    uint32_t panicroom_active;
    uint32_t panicroom_entries;
    uint32_t panic_capsules;
    lardkit_oldcheck_info_t oldcheck;
    lardkit_lfsdoctor_info_t lfsdoctor;
    uint32_t active_theme;
    uint32_t custom_theme_active;
    lardkit_theme_info_t custom_theme;
    lardkit_magic_info_t magic;
    lardkit_larsview_info_t larsview;
} lardkit_state_t;

static lardkit_state_t s_lardkit;

static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
static void draw_text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg);

static const theme_state_t s_themes[] = {
    { "classic", 0x00FFFFFFu, 0x00101820u, 0x0037A7FFu, 0u },
    { "contrast", 0x00FFFFFFu, 0x00000000u, 0x00FFD84Au, 1u },
    { "night", 0x00DDEBFFu, 0x00080C18u, 0x004DE1C1u, 1u },
    { "amber", 0x00FFE1A6u, 0x00120A00u, 0x00FF9F43u, 2u },
};

static const char* const s_bootmap[] = {
    "bios handoff",
    "stage1/stage2 load",
    "protected32 bridge",
    "long64 kernel entry",
    "heap and fs",
    "boot profile",
    "shell/control surface",
    "gui early surface",
    "drivers/languages",
    "post option",
    "network/oslink",
    "steady loop",
};

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

static void report_append(FsWritableFile* f, const char* s)
{
    uint32_t n = 0;
    if (!f || !s) return;
    while (s[n]) n++;
    (void)fs_append(f, (const uint8_t*)s, n);
}

static void report_append_u32(FsWritableFile* f, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    if (v == 0) {
        report_append(f, "0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        char c = tmp[--n];
        (void)fs_append(f, (const uint8_t*)&c, 1u);
    }
}

static void report_append_i32(FsWritableFile* f, int32_t v)
{
    if (v < 0) {
        report_append(f, "-");
        report_append_u32(f, (uint32_t)(-v));
    } else {
        report_append_u32(f, (uint32_t)v);
    }
}

static void report_append_bool(FsWritableFile* f, int on)
{
    report_append(f, on ? "yes" : "no");
}

static int streq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static uint32_t theme_index_by_name(const char* name)
{
    uint32_t count = sizeof(s_themes) / sizeof(s_themes[0]);
    for (uint32_t i = 0; i < count; i++) {
        if (streq(name, s_themes[i].name)) return i;
    }
    return 0xFFFFFFFFu;
}

static void trust_default(uint32_t idx, const char* subject, uint32_t caps)
{
    scopy(s_lardkit.trust[idx].subject, sizeof(s_lardkit.trust[idx].subject), subject);
    s_lardkit.trust[idx].caps = caps;
}

void lardkit_init(void)
{
    for (uint32_t i = 0; i < sizeof(s_lardkit); i++) ((uint8_t*)&s_lardkit)[i] = 0;
    s_lardkit.bugreplay_next = 1u;
    s_lardkit.trace_next = 1u;
    s_lardkit.netwatch_next = 1u;
    s_lardkit.journal_next = 1u;
    s_lardkit.trust_history_next = 1u;
    trust_default(0, "shell", LARDKIT_TRUST_FS | LARDKIT_TRUST_SCREEN | LARDKIT_TRUST_OSLINK);
    trust_default(1, "gui", LARDKIT_TRUST_SCREEN | LARDKIT_TRUST_NET);
    trust_default(2, "oslink", LARDKIT_TRUST_NET | LARDKIT_TRUST_OSLINK);
    trust_default(3, "package", LARDKIT_TRUST_FS);
    trust_default(4, "sum", LARDKIT_TRUST_RAW | LARDKIT_TRUST_FS | LARDKIT_TRUST_SCREEN);
    scopy(s_lardkit.rollback.label, sizeof(s_lardkit.rollback.label), "none");
    scopy(s_lardkit.larsview.path, sizeof(s_lardkit.larsview.path), "lardos.lars");
}

void lardkit_bugeye_enable(int on)
{
    s_lardkit.bugeye.enabled = on ? 1u : 0u;
}

int lardkit_bugeye_write_report(void)
{
    FsWritableFile* w = fs_open_writable("bugreport.lardd");
    const screencheck_info_t* sc = &s_lardkit.bugeye.screen;
    static const char header[] = "LARDD 1\nTITLE BugEye Report\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    report_append(w, "SECTION Last Scan\n");
    report_append(w, s_lardkit.bugeye.bug_count == 0 ? "TEXT result OK\n" : "TEXT result CHECK\n");
    report_append(w, "ITEM enabled ");
    report_append_bool(w, s_lardkit.bugeye.enabled != 0u);
    report_append(w, "\nITEM scans ");
    report_append_u32(w, s_lardkit.bugeye.scans);
    report_append(w, "\nITEM visible-bugs ");
    report_append_u32(w, s_lardkit.bugeye.bug_count);
    report_append(w, "\nITEM last-error ");
    report_append_u32(w, s_lardkit.bugeye.last_error);
    report_append(w, "\nSECTION Screen\nITEM width ");
    report_append_u32(w, sc->width);
    report_append(w, "\nITEM height ");
    report_append_u32(w, sc->height);
    report_append(w, "\nITEM changed-samples ");
    report_append_u32(w, sc->changed_samples);
    report_append(w, "\nITEM tiles-checked ");
    report_append_u32(w, sc->tiles_checked);
    report_append(w, "\nITEM bad-tiles ");
    report_append_u32(w, sc->bad_tiles);
    report_append(w, "\nITEM window-inside ");
    report_append_bool(w, sc->window_inside);
    report_append(w, "\nITEM response-view-ok ");
    report_append_bool(w, sc->response_view_ok);
    report_append(w, "\nSECTION Watchpoints\n");
    report_append(w, "ITEM overlap window-bounds and response-view probes\n");
    report_append(w, "ITEM clipped-text inferred from response-view layout health\n");
    report_append(w, "ITEM odd-color or broken-render inferred from low framebuffer change samples\n");
    report_append(w, "END\n");
    return 0;
}

static void bugreplay_record(void)
{
    uint32_t idx = (s_lardkit.bugreplay_next - 1u) % LARDKIT_BUGREPLAY_MAX;
    lardkit_bugreplay_frame_t* f = &s_lardkit.bugreplay[idx];
    f->seq = s_lardkit.bugreplay_next++;
    if (s_lardkit.bugreplay_next == 0) s_lardkit.bugreplay_next = 1u;
    f->scan = s_lardkit.bugeye.scans;
    f->width = s_lardkit.bugeye.screen.width;
    f->height = s_lardkit.bugeye.screen.height;
    f->changed_samples = s_lardkit.bugeye.screen.changed_samples;
    f->bad_tiles = s_lardkit.bugeye.bug_count;
    f->last_error = s_lardkit.bugeye.last_error;
    if (s_lardkit.bugreplay_count < LARDKIT_BUGREPLAY_MAX) s_lardkit.bugreplay_count++;
}

uint32_t lardkit_bugreplay_count(void)
{
    return s_lardkit.bugreplay_count;
}

int lardkit_bugreplay_at(uint32_t idx, lardkit_bugreplay_frame_t* out)
{
    uint32_t start;
    uint32_t slot;
    if (!out || idx >= s_lardkit.bugreplay_count) return -1;
    start = s_lardkit.bugreplay_count < LARDKIT_BUGREPLAY_MAX ? 0u :
        ((s_lardkit.bugreplay_next - 1u) % LARDKIT_BUGREPLAY_MAX);
    slot = (start + idx) % LARDKIT_BUGREPLAY_MAX;
    *out = s_lardkit.bugreplay[slot];
    return 0;
}

int lardkit_bugreplay_write(void)
{
    FsWritableFile* w = fs_open_writable("bugreplay.lardd");
    static const char header[] = "LARDD 1\nTITLE Bug Replay\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    report_append(w, "TEXT Last BugEye screen health frames.\n");
    report_append(w, "SECTION Frames\n");
    if (s_lardkit.bugreplay_count == 0u) {
        report_append(w, "ITEM none\nEND\n");
        return 0;
    }
    for (uint32_t i = 0; i < s_lardkit.bugreplay_count; i++) {
        lardkit_bugreplay_frame_t f;
        if (lardkit_bugreplay_at(i, &f) != 0) continue;
        report_append(w, "ITEM seq ");
        report_append_u32(w, f.seq);
        report_append(w, " scan ");
        report_append_u32(w, f.scan);
        report_append(w, " size ");
        report_append_u32(w, f.width);
        report_append(w, "x");
        report_append_u32(w, f.height);
        report_append(w, " changed ");
        report_append_u32(w, f.changed_samples);
        report_append(w, " bad ");
        report_append_u32(w, f.bad_tiles);
        report_append(w, " err ");
        report_append_u32(w, f.last_error);
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    return 0;
}

int lardkit_bugreplay_draw(void)
{
    uint32_t sw = gui_syscall_get_width();
    uint32_t sh = gui_syscall_get_height();
    uint32_t panel_w = sw > 420u ? 400u : (sw > 40u ? sw - 32u : sw);
    uint32_t panel_h = 170u;
    uint32_t x = sw > panel_w + 18u ? sw - panel_w - 18u : 8u;
    uint32_t y = sh > panel_h + 18u ? sh - panel_h - 18u : 8u;
    if (sw < 160u || sh < 100u) return -1;
    draw_rect(x, y, panel_w, panel_h, 0xFF071016u);
    draw_rect(x, y, panel_w, 2u, 0xFF72D6FFu);
    draw_rect(x, y + panel_h - 2u, panel_w, 2u, 0xFF72D6FFu);
    draw_text(x + 10u, y + 10u, "BUGREPLAY LAST GUI FRAMES", 0xFFFFFFFFu, 0xFF071016u);
    if (s_lardkit.bugreplay_count == 0u) {
        draw_text(x + 10u, y + 36u, "no frames yet - run bugeye scan", 0xFFFFD84Au, 0xFF071016u);
        return 0;
    }
    for (uint32_t i = 0; i < s_lardkit.bugreplay_count; i++) {
        lardkit_bugreplay_frame_t f;
        uint32_t bar_x = x + 16u + i * 46u;
        uint32_t bar_h;
        uint32_t bad_h;
        if (bar_x + 34u >= x + panel_w) break;
        if (lardkit_bugreplay_at(i, &f) != 0) continue;
        bar_h = f.changed_samples / 2048u;
        if (bar_h > 84u) bar_h = 84u;
        if (bar_h < 4u) bar_h = 4u;
        bad_h = f.bad_tiles ? 14u : 4u;
        draw_rect(bar_x, y + 136u - bar_h, 28u, bar_h, f.bad_tiles ? 0xFFFF6250u : 0xFF4DE1C1u);
        draw_rect(bar_x, y + 142u, 28u, bad_h, f.bad_tiles ? 0xFFFFD84Au : 0xFF243640u);
        draw_text(bar_x, y + 54u, "#", 0xFFFFFFFFu, 0xFF071016u);
    }
    return 0;
}

void lardkit_bugreplay_clear(void)
{
    for (uint32_t i = 0; i < LARDKIT_BUGREPLAY_MAX; i++) {
        for (uint32_t j = 0; j < sizeof(s_lardkit.bugreplay[i]); j++) ((uint8_t*)&s_lardkit.bugreplay[i])[j] = 0;
    }
    s_lardkit.bugreplay_count = 0;
    s_lardkit.bugreplay_next = 1u;
    (void)lardkit_bugreplay_write();
}

static uint32_t ring_start(uint32_t count, uint32_t max, uint32_t next)
{
    return count < max ? 0u : ((next - 1u) % max);
}

static void journal_header(FsWritableFile* w, const char* note)
{
    static const char header[] = "LARDD 1\nTITLE LardOS Journal\n";
    if (!w) return;
    w->size = 0;
    (void)fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u);
    if (note && note[0]) {
        report_append(w, "TEXT ");
        report_append(w, note);
        report_append(w, "\n");
    }
    report_append(w, "SECTION Events\n");
}

void lardkit_journal_event(const char* area, const char* text)
{
    FsWritableFile* w = fs_open_writable("journal.lardd");
    if (!w) return;
    if (w->size < 8u) journal_header(w, "journal started");
    if (w->cap - w->size < 128u) journal_header(w, "journal rotated because the RAM file filled");
    report_append(w, "ITEM ");
    report_append_u32(w, s_lardkit.journal_next++);
    if (s_lardkit.journal_next == 0) s_lardkit.journal_next = 1u;
    report_append(w, " ");
    report_append(w, area && area[0] ? area : "os");
    report_append(w, " ");
    report_append(w, text && text[0] ? text : "event");
    report_append(w, "\n");
}

int lardkit_journal_clear(void)
{
    FsWritableFile* w = fs_open_writable("journal.lardd");
    if (!w) return -1;
    s_lardkit.journal_next = 1u;
    journal_header(w, "journal cleared by user");
    return 0;
}

void lardkit_trace_enable(int on)
{
    s_lardkit.trace_enabled = on ? 1u : 0u;
    if (s_lardkit.trace_enabled) {
        lardkit_trace_event("trace", "enabled", 1);
        lardkit_journal_event("trace", "enabled");
    } else {
        lardkit_journal_event("trace", "disabled");
    }
}

void lardkit_trace_event(const char* module, const char* text, int32_t value)
{
    uint32_t idx;
    lardkit_trace_entry_t* e;
    if (!s_lardkit.trace_enabled) return;
    idx = (s_lardkit.trace_next - 1u) % LARDKIT_TRACE_MAX;
    e = &s_lardkit.trace[idx];
    e->seq = s_lardkit.trace_next++;
    if (s_lardkit.trace_next == 0) s_lardkit.trace_next = 1u;
    e->tick = ++s_lardkit.trace_tick;
    scopy(e->module, sizeof(e->module), module && module[0] ? module : "kernel");
    scopy(e->text, sizeof(e->text), text && text[0] ? text : "event");
    e->value = value;
    if (s_lardkit.trace_count < LARDKIT_TRACE_MAX) s_lardkit.trace_count++;
}

void lardkit_trace_info(lardkit_trace_info_t* out)
{
    if (!out) return;
    out->enabled = s_lardkit.trace_enabled;
    out->count = s_lardkit.trace_count;
    out->next_seq = s_lardkit.trace_next;
}

uint32_t lardkit_trace_count(void)
{
    return s_lardkit.trace_count;
}

int lardkit_trace_at(uint32_t idx, lardkit_trace_entry_t* out)
{
    uint32_t slot;
    if (!out || idx >= s_lardkit.trace_count) return -1;
    slot = (ring_start(s_lardkit.trace_count, LARDKIT_TRACE_MAX, s_lardkit.trace_next) + idx) % LARDKIT_TRACE_MAX;
    *out = s_lardkit.trace[slot];
    return 0;
}

void lardkit_trace_clear(void)
{
    for (uint32_t i = 0; i < LARDKIT_TRACE_MAX; i++) {
        for (uint32_t j = 0; j < sizeof(s_lardkit.trace[i]); j++) ((uint8_t*)&s_lardkit.trace[i])[j] = 0;
    }
    s_lardkit.trace_count = 0;
    s_lardkit.trace_next = 1u;
    s_lardkit.trace_tick = 0;
}

int lardkit_trace_write(void)
{
    FsWritableFile* w = fs_open_writable("trace.lardd");
    static const char header[] = "LARDD 1\nTITLE LardTrace\nSECTION Events\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    for (uint32_t i = 0; i < s_lardkit.trace_count; i++) {
        lardkit_trace_entry_t e;
        if (lardkit_trace_at(i, &e) != 0) continue;
        report_append(w, "ITEM #");
        report_append_u32(w, e.seq);
        report_append(w, " t");
        report_append_u32(w, e.tick);
        report_append(w, " ");
        report_append(w, e.module);
        report_append(w, " ");
        report_append(w, e.text);
        report_append(w, " value ");
        report_append_i32(w, e.value);
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    return 0;
}

void lardkit_netwatch_enable(int on)
{
    s_lardkit.netwatch_enabled = on ? 1u : 0u;
    s_lardkit.netwatch_info.enabled = s_lardkit.netwatch_enabled;
    lardkit_journal_event("netwatch", on ? "enabled" : "disabled");
}

void lardkit_netwatch_record(const char* kind, const char* detail, int32_t value)
{
    uint32_t idx;
    lardkit_netwatch_entry_t* e;
    if (!s_lardkit.netwatch_enabled) return;
    idx = (s_lardkit.netwatch_next - 1u) % LARDKIT_NETWATCH_MAX;
    e = &s_lardkit.netwatch[idx];
    e->seq = s_lardkit.netwatch_next++;
    if (s_lardkit.netwatch_next == 0) s_lardkit.netwatch_next = 1u;
    scopy(e->kind, sizeof(e->kind), kind && kind[0] ? kind : "net");
    scopy(e->detail, sizeof(e->detail), detail && detail[0] ? detail : "packet");
    e->value = value;
    if (s_lardkit.netwatch_count < LARDKIT_NETWATCH_MAX) s_lardkit.netwatch_count++;
    s_lardkit.netwatch_info.count = s_lardkit.netwatch_count;
    if (streq(e->kind, "udp-send") || streq(e->kind, "http") || streq(e->kind, "https")) s_lardkit.netwatch_info.sent++;
    if (streq(e->kind, "udp-recv")) s_lardkit.netwatch_info.received++;
    if (streq(e->kind, "http") || streq(e->kind, "https")) s_lardkit.netwatch_info.http++;
    if (streq(e->kind, "oslink")) s_lardkit.netwatch_info.oslink++;
}

void lardkit_netwatch_info(lardkit_netwatch_info_t* out)
{
    if (!out) return;
    s_lardkit.netwatch_info.enabled = s_lardkit.netwatch_enabled;
    s_lardkit.netwatch_info.count = s_lardkit.netwatch_count;
    *out = s_lardkit.netwatch_info;
}

uint32_t lardkit_netwatch_count(void)
{
    return s_lardkit.netwatch_count;
}

int lardkit_netwatch_at(uint32_t idx, lardkit_netwatch_entry_t* out)
{
    uint32_t slot;
    if (!out || idx >= s_lardkit.netwatch_count) return -1;
    slot = (ring_start(s_lardkit.netwatch_count, LARDKIT_NETWATCH_MAX, s_lardkit.netwatch_next) + idx) % LARDKIT_NETWATCH_MAX;
    *out = s_lardkit.netwatch[slot];
    return 0;
}

void lardkit_netwatch_clear(void)
{
    for (uint32_t i = 0; i < LARDKIT_NETWATCH_MAX; i++) {
        for (uint32_t j = 0; j < sizeof(s_lardkit.netwatch[i]); j++) ((uint8_t*)&s_lardkit.netwatch[i])[j] = 0;
    }
    s_lardkit.netwatch_count = 0;
    s_lardkit.netwatch_next = 1u;
    s_lardkit.netwatch_info.count = 0;
}

int lardkit_netwatch_write(void)
{
    FsWritableFile* w = fs_open_writable("netwatch.lardd");
    static const char header[] = "LARDD 1\nTITLE NetWatch\nSECTION Events\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    for (uint32_t i = 0; i < s_lardkit.netwatch_count; i++) {
        lardkit_netwatch_entry_t e;
        if (lardkit_netwatch_at(i, &e) != 0) continue;
        report_append(w, "ITEM #");
        report_append_u32(w, e.seq);
        report_append(w, " ");
        report_append(w, e.kind);
        report_append(w, " ");
        report_append(w, e.detail);
        report_append(w, " bytes ");
        report_append_i32(w, e.value);
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    return 0;
}

int lardkit_bugeye_scan(void)
{
    screencheck_info_t info;
    int r = screencheck_probe(&info);
    s_lardkit.bugeye.scans++;
    s_lardkit.bugeye.screen = info;
    s_lardkit.bugeye.last_error = (r == 0) ? 0u : (uint32_t)(-r);
    if (r != 0) {
        s_lardkit.bugeye.bug_count++;
        bugreplay_record();
        (void)lardkit_bugeye_write_report();
        (void)lardkit_bugreplay_write();
        return r;
    }
    s_lardkit.bugeye.bug_count = info.bad_tiles;
    bugreplay_record();
    (void)lardkit_bugeye_write_report();
    (void)lardkit_bugreplay_write();
    return info.bad_tiles == 0 ? 0 : 1;
}

void lardkit_bugeye_info(lardkit_bugeye_info_t* out)
{
    if (!out) return;
    out->enabled = s_lardkit.bugeye.enabled;
    out->scans = s_lardkit.bugeye.scans;
    out->bug_count = s_lardkit.bugeye.bug_count;
    out->last_error = s_lardkit.bugeye.last_error;
    out->screen = s_lardkit.bugeye.screen;
}

int lardkit_snapshot(const char* label)
{
    bootprof_info_t bp;
    awake_info_t aw;
    lassist_info_t bi;
    bootprof_info(&bp);
    awake_info(&aw);
    lassist_info(&bi);
    s_lardkit.rollback.valid = 1u;
    s_lardkit.rollback.snapshots++;
    scopy(s_lardkit.rollback.label, sizeof(s_lardkit.rollback.label),
          label && label[0] ? label : "manual");
    s_lardkit.rollback.buddy_enabled = bi.enabled;
    s_lardkit.rollback.http_post = (uint32_t)gui_http_post_mode();
    s_lardkit.rollback.task_default = taskprio_default_priority();
    scopy(s_lardkit.rollback.boot_profile, sizeof(s_lardkit.rollback.boot_profile), bp.name);
    s_lardkit.rollback.awake_enabled = aw.enabled;
    s_lardkit.rollback.theme = s_lardkit.active_theme;
    lardkit_journal_event("rollback", "snapshot saved");
    return 0;
}

int lardkit_rollback_apply(void)
{
    if (!s_lardkit.rollback.valid) return -1;
    lassist_enable((int)s_lardkit.rollback.buddy_enabled);
    gui_http_set_post_mode((int)s_lardkit.rollback.http_post);
    taskprio_set_default(s_lardkit.rollback.task_default);
    (void)bootprof_set(s_lardkit.rollback.boot_profile);
    if (s_lardkit.rollback.awake_enabled) awake_enable(1, 3u);
    else awake_enable(0, 0);
    s_lardkit.active_theme = s_lardkit.rollback.theme;
    s_lardkit.rollback.applied++;
    return 0;
}

void lardkit_rollback_info(lardkit_rollback_info_t* out)
{
    if (!out) return;
    *out = s_lardkit.rollback;
}

uint32_t lardkit_trust_count(void)
{
    return sizeof(s_lardkit.trust) / sizeof(s_lardkit.trust[0]);
}

int lardkit_trust_at(uint32_t idx, lardkit_trust_entry_t* out)
{
    if (!out || idx >= lardkit_trust_count()) return -1;
    scopy(out->subject, sizeof(out->subject), s_lardkit.trust[idx].subject);
    out->caps = s_lardkit.trust[idx].caps;
    return 0;
}

uint32_t lardkit_trust_caps(const char* subject)
{
    for (uint32_t i = 0; i < lardkit_trust_count(); i++) {
        if (streq(subject, s_lardkit.trust[i].subject)) return s_lardkit.trust[i].caps;
    }
    return 0;
}

static void trust_history_log(const char* subject, uint32_t cap, int allow, uint32_t caps_after)
{
    uint32_t idx = (s_lardkit.trust_history_next - 1u) % LARDKIT_TRUST_HISTORY_MAX;
    lardkit_trust_history_entry_t* e = &s_lardkit.trust_history[idx];
    e->seq = s_lardkit.trust_history_next++;
    if (s_lardkit.trust_history_next == 0) s_lardkit.trust_history_next = 1u;
    scopy(e->subject, sizeof(e->subject), subject);
    e->cap = cap;
    e->allowed = allow ? 1u : 0u;
    e->caps_after = caps_after;
    if (s_lardkit.trust_history_count < LARDKIT_TRUST_HISTORY_MAX) s_lardkit.trust_history_count++;
}

int lardkit_trust_set(const char* subject, uint32_t cap, int allow)
{
    for (uint32_t i = 0; i < lardkit_trust_count(); i++) {
        if (streq(subject, s_lardkit.trust[i].subject)) {
            if (allow) s_lardkit.trust[i].caps |= cap;
            else s_lardkit.trust[i].caps &= ~cap;
            trust_history_log(subject, cap, allow, s_lardkit.trust[i].caps);
            lardkit_trace_event("trust", allow ? "allow" : "deny", (int32_t)cap);
            lardkit_journal_event("trust", allow ? "allowed capability" : "denied capability");
            return 0;
        }
    }
    return -1;
}

uint32_t lardkit_trust_history_count(void)
{
    return s_lardkit.trust_history_count;
}

int lardkit_trust_history_at(uint32_t idx, lardkit_trust_history_entry_t* out)
{
    uint32_t start;
    uint32_t slot;
    if (!out || idx >= s_lardkit.trust_history_count) return -1;
    start = s_lardkit.trust_history_count < LARDKIT_TRUST_HISTORY_MAX ? 0u :
        ((s_lardkit.trust_history_next - 1u) % LARDKIT_TRUST_HISTORY_MAX);
    slot = (start + idx) % LARDKIT_TRUST_HISTORY_MAX;
    *out = s_lardkit.trust_history[slot];
    return 0;
}

void lardkit_trust_history_clear(void)
{
    for (uint32_t i = 0; i < LARDKIT_TRUST_HISTORY_MAX; i++) {
        for (uint32_t j = 0; j < sizeof(s_lardkit.trust_history[i]); j++) ((uint8_t*)&s_lardkit.trust_history[i])[j] = 0;
    }
    s_lardkit.trust_history_count = 0;
    s_lardkit.trust_history_next = 1u;
}

uint32_t lardkit_bootmap_count(void)
{
    return sizeof(s_bootmap) / sizeof(s_bootmap[0]);
}

const char* lardkit_bootmap_phase(uint32_t idx)
{
    if (idx >= lardkit_bootmap_count()) return "";
    return s_bootmap[idx];
}

void lardkit_panicroom_enter(void)
{
    s_lardkit.panicroom_active = 1u;
    s_lardkit.panicroom_entries++;
}

void lardkit_panicroom_exit(void)
{
    s_lardkit.panicroom_active = 0u;
}

uint32_t lardkit_panicroom_active(void)
{
    return s_lardkit.panicroom_active;
}

uint32_t lardkit_panicroom_entries(void)
{
    return s_lardkit.panicroom_entries;
}

static void oldcheck_count_cb(const char* name, uint32_t size, void* user)
{
    uint32_t* count = (uint32_t*)user;
    (void)name;
    (void)size;
    if (count) (*count)++;
}

static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (w == 0 || h == 0 || x > 65535u || y > 65535u) return;
    gui_syscall_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

static void draw_text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg)
{
    if (x > 65535u || y > 65535u) return;
    gui_syscall_draw_text((uint16_t)x, (uint16_t)y, s, fg, bg);
}

static void oldcheck_draw(uint32_t files, uint32_t dirty, uint32_t available)
{
    uint32_t w = gui_syscall_get_width();
    uint32_t h = gui_syscall_get_height();
    uint32_t cols = 24u;
    uint32_t cell = 18u;
    if (w < 320u || h < 200u) return;
    gui_syscall_clear(0xFF050606u);
    draw_text(28u, 24u, "LARDOS OLDCHECK STORAGE MAP", 0xFF3CE6A1u, 0xFF050606u);
    draw_text(28u, 48u, available ? "BLOCK DEVICE: ONLINE" : "BLOCK DEVICE: OFFLINE", 0xFFFFFFFFu, 0xFF050606u);
    draw_text(28u, 68u, dirty ? "LPST BANK: DIRTY" : "LPST BANK: CLEAN", 0xFFFFD84Au, 0xFF050606u);
    for (uint32_t i = 0; i < 96u; i++) {
        uint32_t x = 28u + (i % cols) * cell;
        uint32_t y = 104u + (i / cols) * cell;
        uint32_t color = (i < files) ? 0xFF26D07Cu : ((i & 1u) ? 0xFF203040u : 0xFF102030u);
        if (dirty && (i % 11u) == 0u) color = 0xFFFF9F43u;
        draw_rect(x, y, cell - 2u, cell - 2u, color);
    }
    draw_text(28u, h > 32u ? h - 28u : 0u, "oldcheck status shows counts; oldcheck draw redraws this map.", 0xFFFFFFFFu, 0xFF050606u);
}

int lardkit_oldcheck_run(int draw)
{
    uint32_t count = 0;
    uint32_t available = 0;
    uint32_t dirty = 0;
    uint32_t generation = 0;
    int last = 0;
    const char* driver;
    (void)driver;
    fs_list(oldcheck_count_cb, &count);
    fs_persist_info(&available, &dirty, &last, &driver, NULL, NULL);
    fs_persist_detail(NULL, &generation, NULL);
    s_lardkit.oldcheck.count = count;
    s_lardkit.oldcheck.available = available;
    s_lardkit.oldcheck.dirty = dirty;
    s_lardkit.oldcheck.generation = generation;
    s_lardkit.oldcheck.last_error = last < 0 ? (uint32_t)(-last) : 0u;
    if (draw) oldcheck_draw(count, dirty, available);
    return 0;
}

void lardkit_oldcheck_info(lardkit_oldcheck_info_t* out)
{
    if (!out) return;
    *out = s_lardkit.oldcheck;
}

static int lfsdoctor_write_report(void)
{
    FsWritableFile* w = fs_open_writable("lfsdoctor.lardd");
    lardkit_lfsdoctor_info_t* d = &s_lardkit.lfsdoctor;
    static const char header[] = "LARDD 1\nTITLE LFS Doctor\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    report_append(w, "SECTION Last Scan\nITEM files ");
    report_append_u32(w, d->files);
    report_append(w, "\nITEM storage ");
    report_append(w, d->storage_available ? "online" : "offline");
    report_append(w, "\nITEM dirty ");
    report_append_bool(w, d->dirty != 0u);
    report_append(w, "\nITEM generation ");
    report_append_u32(w, d->generation);
    report_append(w, "\nITEM last-result ");
    report_append_i32(w, d->last_result);
    report_append(w, "\nITEM repairs ");
    report_append_u32(w, d->repairs);
    report_append(w, "\nITEM last-repair ");
    report_append_i32(w, d->last_repair);
    report_append(w, "\nSECTION Advice\n");
    if (!d->storage_available) report_append(w, "ITEM storage offline; repair needs a block device\n");
    else if (d->dirty) report_append(w, "ITEM dirty RAM files detected; lfsdoctor repair will persist them\n");
    else report_append(w, "ITEM no dirty writable files; lfsdoctor repair refreshes from LPST\n");
    report_append(w, "END\n");
    return 0;
}

int lardkit_lfsdoctor_scan(int repair)
{
    uint32_t count = 0;
    uint32_t available = 0;
    uint32_t dirty = 0;
    uint32_t generation = 0;
    int last = 0;
    int rr = 0;
    const char* driver;
    (void)driver;
    fs_list(oldcheck_count_cb, &count);
    fs_persist_info(&available, &dirty, &last, &driver, NULL, NULL);
    fs_persist_detail(NULL, &generation, NULL);
    s_lardkit.lfsdoctor.files = count;
    s_lardkit.lfsdoctor.storage_available = available;
    s_lardkit.lfsdoctor.dirty = dirty;
    s_lardkit.lfsdoctor.generation = generation;
    s_lardkit.lfsdoctor.last_result = last;
    if (repair) {
        if (!available) rr = -1;
        else if (dirty) rr = fs_persist_save();
        else rr = fs_persist_load();
        s_lardkit.lfsdoctor.repairs++;
        s_lardkit.lfsdoctor.last_repair = rr;
        fs_persist_info(&available, &dirty, &last, &driver, NULL, NULL);
        fs_persist_detail(NULL, &generation, NULL);
        s_lardkit.lfsdoctor.storage_available = available;
        s_lardkit.lfsdoctor.dirty = dirty;
        s_lardkit.lfsdoctor.generation = generation;
        s_lardkit.lfsdoctor.last_result = last;
    }
    (void)lfsdoctor_write_report();
    return repair ? rr : 0;
}

void lardkit_lfsdoctor_info(lardkit_lfsdoctor_info_t* out)
{
    if (!out) return;
    *out = s_lardkit.lfsdoctor;
}

static int line_starts(const char* s, const char* end, const char* prefix, const char** rest)
{
    while (s < end && *prefix && *s == *prefix) {
        s++;
        prefix++;
    }
    if (*prefix) return 0;
    if (rest) *rest = s;
    return 1;
}

static void post_copy_name(char* dst, uint32_t cap, const char* s, const char* end)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (s < end && *s != '\r' && *s != '\n' && i + 1u < cap) dst[i++] = *s++;
    dst[i] = '\0';
}

static void post_baseline_parse_file(void)
{
    const FsFile* f = fs_open("postbaseline.lardd");
    const char* p;
    const char* end;
    s_lardkit.post_info.has_previous = 0;
    s_lardkit.post_info.previous_count = 0;
    if (!f || !f->data || f->size == 0) return;
    p = (const char*)f->data;
    end = p + f->size;
    while (p < end && s_lardkit.post_info.previous_count < LARDKIT_POST_BASELINE_MAX) {
        const char* ls = p;
        const char* le;
        const char* rest;
        while (p < end && *p != '\n' && *p != '\r') p++;
        le = p;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
        if (line_starts(ls, le, "ITEM PASS ", &rest) || line_starts(ls, le, "ITEM FAIL ", &rest)) {
            uint32_t idx = s_lardkit.post_info.previous_count++;
            s_lardkit.post_prev[idx].ok = (ls[5] == 'P') ? 1u : 0u;
            post_copy_name(s_lardkit.post_prev[idx].name, sizeof(s_lardkit.post_prev[idx].name), rest, le);
        }
    }
    s_lardkit.post_info.has_previous = s_lardkit.post_info.previous_count ? 1u : 0u;
}

void lardkit_post_baseline_begin(void)
{
    for (uint32_t i = 0; i < LARDKIT_POST_BASELINE_MAX; i++) {
        s_lardkit.post_cur[i].name[0] = '\0';
        s_lardkit.post_cur[i].ok = 0;
    }
    s_lardkit.post_info.current_count = 0;
    s_lardkit.post_info.changes = 0;
    s_lardkit.post_info.regressions = 0;
    post_baseline_parse_file();
    s_lardkit.post_loaded = 1u;
}

void lardkit_post_baseline_observe(const char* name, int ok)
{
    uint32_t idx;
    if (!s_lardkit.post_loaded) lardkit_post_baseline_begin();
    if (s_lardkit.post_info.current_count >= LARDKIT_POST_BASELINE_MAX) return;
    idx = s_lardkit.post_info.current_count++;
    scopy(s_lardkit.post_cur[idx].name, sizeof(s_lardkit.post_cur[idx].name), name && name[0] ? name : "check");
    s_lardkit.post_cur[idx].ok = ok ? 1u : 0u;
    for (uint32_t i = 0; i < s_lardkit.post_info.previous_count; i++) {
        if (streq(s_lardkit.post_prev[i].name, s_lardkit.post_cur[idx].name)) {
            if (s_lardkit.post_prev[i].ok != s_lardkit.post_cur[idx].ok) {
                s_lardkit.post_info.changes++;
                if (s_lardkit.post_prev[i].ok && !s_lardkit.post_cur[idx].ok) s_lardkit.post_info.regressions++;
            }
            break;
        }
    }
}

void lardkit_post_baseline_finish(void)
{
    FsWritableFile* w = fs_open_writable("postbaseline.lardd");
    static const char header[] = "LARDD 1\nTITLE POST Baseline\n";
    if (!w) return;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return;
    report_append(w, "TEXT Last normal POST result is used as the next baseline.\n");
    report_append(w, "SECTION Summary\nITEM previous ");
    report_append_u32(w, s_lardkit.post_info.previous_count);
    report_append(w, "\nITEM current ");
    report_append_u32(w, s_lardkit.post_info.current_count);
    report_append(w, "\nITEM changes ");
    report_append_u32(w, s_lardkit.post_info.changes);
    report_append(w, "\nITEM regressions ");
    report_append_u32(w, s_lardkit.post_info.regressions);
    report_append(w, "\nSECTION Checks\n");
    for (uint32_t i = 0; i < s_lardkit.post_info.current_count; i++) {
        report_append(w, s_lardkit.post_cur[i].ok ? "ITEM PASS " : "ITEM FAIL ");
        report_append(w, s_lardkit.post_cur[i].name);
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    if (s_lardkit.post_info.regressions) lardkit_journal_event("post", "baseline regression detected");
}

void lardkit_post_baseline_info(lardkit_post_baseline_info_t* out)
{
    if (!out) return;
    *out = s_lardkit.post_info;
}

int lardkit_bootreplay_write(void)
{
    static const char* const detail[] = {
        "BIOS loads stage1 and LardOS begins its own path",
        "stage1 loads stage2 and locates the kernel payload",
        "protected32 is used as a small bridge, not as the final home",
        "long64 entry takes over and the C kernel becomes the owner",
        "memory, filesystem, LPST, and crashlog are made visible",
        "boot profile chooses normal, safe, netoff, dev, or awakening",
        "LSH, GUI, LARS, LARDD, and user-control surfaces come online",
        "drivers, languages, network, OSLink, and background work settle",
    };
    FsWritableFile* w = fs_open_writable("bootreplay.lardd");
    static const char header[] = "LARDD 1\nTITLE Boot Replay\nSECTION Timeline\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    for (uint32_t i = 0; i < sizeof(detail) / sizeof(detail[0]); i++) {
        report_append(w, "ITEM ");
        report_append_u32(w, i);
        report_append(w, " ");
        report_append(w, detail[i]);
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    lardkit_journal_event("bootreplay", "timeline captured");
    return 0;
}

int lardkit_panic_capsule_write(void)
{
    FsWritableFile* w = fs_open_writable("paniccapsule.lardd");
    lardkit_bugeye_info_t bi;
    lardkit_lfsdoctor_info_t lf;
    const FsFile* crash = fs_open("crashlog.txt");
    static const char header[] = "LARDD 1\nTITLE Panic Capsule\n";
    if (!w) return -1;
    (void)lardkit_bugeye_write_report();
    (void)lardkit_bugreplay_write();
    (void)lardkit_oldcheck_run(0);
    (void)lardkit_lfsdoctor_scan(0);
    lardkit_bugeye_info(&bi);
    lardkit_lfsdoctor_info(&lf);
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    s_lardkit.panic_capsules++;
    report_append(w, "SECTION Summary\nITEM capsule ");
    report_append_u32(w, s_lardkit.panic_capsules);
    report_append(w, "\nITEM panicroom ");
    report_append(w, s_lardkit.panicroom_active ? "entered" : "standby");
    report_append(w, "\nITEM panicroom-entries ");
    report_append_u32(w, s_lardkit.panicroom_entries);
    report_append(w, "\nITEM crashlog-bytes ");
    report_append_u32(w, crash ? crash->size : 0u);
    report_append(w, "\nITEM bugeye-bugs ");
    report_append_u32(w, bi.bug_count);
    report_append(w, "\nITEM replay-frames ");
    report_append_u32(w, s_lardkit.bugreplay_count);
    report_append(w, "\nITEM trust-events ");
    report_append_u32(w, s_lardkit.trust_history_count);
    report_append(w, "\nITEM priority-lev10-events ");
    report_append_u32(w, taskprio_history_count());
    report_append(w, "\nITEM trace-events ");
    report_append_u32(w, s_lardkit.trace_count);
    report_append(w, "\nITEM netwatch-events ");
    report_append_u32(w, s_lardkit.netwatch_count);
    report_append(w, "\nITEM post-regressions ");
    report_append_u32(w, s_lardkit.post_info.regressions);
    report_append(w, "\nITEM files ");
    report_append_u32(w, lf.files);
    report_append(w, "\nITEM storage ");
    report_append(w, lf.storage_available ? "online" : "offline");
    report_append(w, "\nITEM dirty ");
    report_append_bool(w, lf.dirty != 0u);
    report_append(w, "\nSECTION Linked Reports\n");
    report_append(w, "ITEM crashlog.txt\n");
    report_append(w, "ITEM bugreport.lardd\n");
    report_append(w, "ITEM bugreplay.lardd\n");
    report_append(w, "ITEM lfsdoctor.lardd\n");
    report_append(w, "ITEM postbaseline.lardd\n");
    report_append(w, "ITEM bootreplay.lardd\n");
    report_append(w, "ITEM journal.lardd\n");
    report_append(w, "ITEM trace.lardd\n");
    report_append(w, "ITEM netwatch.lardd\n");
    report_append(w, "SECTION BootMap\n");
    for (uint32_t i = 0; i < lardkit_bootmap_count(); i++) {
        report_append(w, "ITEM ");
        report_append_u32(w, i);
        report_append(w, " ");
        report_append(w, lardkit_bootmap_phase(i));
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    return 0;
}

uint32_t lardkit_theme_count(void)
{
    return sizeof(s_themes) / sizeof(s_themes[0]);
}

int lardkit_theme_at(uint32_t idx, lardkit_theme_info_t* out)
{
    if (!out || idx >= lardkit_theme_count()) return -1;
    scopy(out->name, sizeof(out->name), s_themes[idx].name);
    out->fg = s_themes[idx].fg;
    out->bg = s_themes[idx].bg;
    out->accent = s_themes[idx].accent;
    out->style_hint = s_themes[idx].style_hint;
    return 0;
}

int lardkit_theme_use(const char* name)
{
    uint32_t idx = theme_index_by_name(name);
    if (idx == 0xFFFFFFFFu) return -1;
    s_lardkit.active_theme = idx;
    s_lardkit.custom_theme_active = 0u;
    return 0;
}

static uint32_t hex_digit(char c, int* ok)
{
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint32_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint32_t)(c - 'A' + 10);
    *ok = 0;
    return 0;
}

static int parse_hex_u32(const char* p, const char* end, uint32_t* out)
{
    uint32_t v = 0;
    int ok = 1;
    int any = 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (p < end && *p != ' ' && *p != '\t') {
        v = (v << 4) | hex_digit(*p, &ok);
        if (!ok) return -1;
        any = 1;
        p++;
    }
    if (!any || !out) return -1;
    *out = v;
    return 0;
}

static int line_word(const char* p, const char* end, const char* word, const char** value)
{
    while (p < end && *word && *p == *word) {
        p++;
        word++;
    }
    if (*word) return 0;
    if (p < end && *p != ' ' && *p != '\t') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (value) *value = p;
    return 1;
}

static int next_line(const char** p, const char* end, const char** ls, const char** le)
{
    const char* s = *p;
    while (s < end && (*s == '\r' || *s == '\n')) s++;
    if (s >= end) {
        *p = s;
        return 0;
    }
    *ls = s;
    while (s < end && *s != '\r' && *s != '\n') s++;
    *le = s;
    while (s < end && (*s == '\r' || *s == '\n')) s++;
    *p = s;
    return 1;
}

int lardkit_theme_parse(const uint8_t* data, uint32_t len, lardkit_theme_info_t* out)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    int saw_header = 0;
    if (!data || !out || len < 8u) return -1;
    scopy(out->name, sizeof(out->name), "custom");
    out->fg = 0x00FFFFFFu;
    out->bg = 0x00101820u;
    out->accent = 0x0037A7FFu;
    out->style_hint = 0u;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (!saw_header) {
            if ((uint32_t)(le - ls) != 8u || !line_word(ls, le, "LTHEME", &v) || !v || *v != '1') return -2;
            saw_header = 1;
            continue;
        }
        if (line_word(ls, le, "NAME", &v)) {
            uint32_t n = 0;
            while (v < le && *v != ' ' && *v != '\t' && n + 1u < sizeof(out->name)) out->name[n++] = *v++;
            out->name[n] = '\0';
        } else if (line_word(ls, le, "FG", &v)) {
            if (parse_hex_u32(v, le, &out->fg) != 0) return -3;
        } else if (line_word(ls, le, "BG", &v)) {
            if (parse_hex_u32(v, le, &out->bg) != 0) return -4;
        } else if (line_word(ls, le, "ACCENT", &v)) {
            if (parse_hex_u32(v, le, &out->accent) != 0) return -5;
        } else if (line_word(ls, le, "STYLE", &v)) {
            if (le - v >= 5 && v[0] == 'l') out->style_hint = 1u;
            else if (le - v >= 3 && v[0] == 'm') out->style_hint = 2u;
            else out->style_hint = 0u;
        }
    }
    return saw_header ? 0 : -6;
}

int lardkit_theme_use_data(const uint8_t* data, uint32_t len)
{
    lardkit_theme_info_t parsed;
    if (lardkit_theme_parse(data, len, &parsed) != 0) return -1;
    s_lardkit.custom_theme = parsed;
    s_lardkit.custom_theme_active = 1u;
    return 0;
}

void lardkit_theme_info(lardkit_theme_info_t* out)
{
    if (!out) return;
    if (s_lardkit.custom_theme_active) {
        *out = s_lardkit.custom_theme;
        return;
    }
    (void)lardkit_theme_at(s_lardkit.active_theme, out);
}

int lardkit_theme_preview_draw(const lardkit_theme_info_t* theme)
{
    uint32_t sw = gui_syscall_get_width();
    uint32_t x = sw > 360u ? 24u : 8u;
    uint32_t y = 56u;
    uint32_t fg;
    uint32_t bg;
    uint32_t accent;
    if (!theme) return -1;
    fg = 0xFF000000u | theme->fg;
    bg = 0xFF000000u | theme->bg;
    accent = 0xFF000000u | theme->accent;
    draw_rect(x, y, 320u, 140u, bg);
    draw_rect(x, y, 320u, 4u, accent);
    draw_rect(x, y + 136u, 320u, 4u, accent);
    draw_text(x + 12u, y + 16u, "LTHEME PREVIEW", fg, bg);
    draw_rect(x + 12u, y + 44u, 120u, 32u, accent);
    draw_text(x + 20u, y + 56u, "active", bg, accent);
    draw_rect(x + 146u, y + 44u, 148u, 32u, 0xFF222830u);
    draw_text(x + 154u, y + 56u, "inactive", fg, 0xFF222830u);
    draw_text(x + 12u, y + 96u, theme->name, fg, bg);
    return 0;
}

static void cfg_profile_from_current(lardkit_cfg_profile_t* p, const char* name)
{
    bootprof_info_t bp;
    awake_info_t aw;
    lassist_info_t bi;
    if (!p) return;
    bootprof_info(&bp);
    awake_info(&aw);
    lassist_info(&bi);
    p->valid = 1u;
    scopy(p->name, sizeof(p->name), name && name[0] ? name : "profile");
    p->buddy_enabled = bi.enabled;
    p->http_post = (uint32_t)gui_http_post_mode();
    p->task_default = taskprio_default_priority();
    scopy(p->boot_profile, sizeof(p->boot_profile), bp.name);
    p->awake_enabled = aw.enabled;
    p->theme = s_lardkit.active_theme;
}

static int cfg_profile_apply(const lardkit_cfg_profile_t* p)
{
    if (!p || !p->valid) return -1;
    lassist_enable((int)p->buddy_enabled);
    gui_http_set_post_mode((int)p->http_post);
    taskprio_set_default(p->task_default);
    (void)bootprof_set(p->boot_profile);
    if (p->awake_enabled) awake_enable(1, 3u);
    else awake_enable(0, 0);
    s_lardkit.active_theme = p->theme;
    return 0;
}

int lardkit_cfgprof_save(const char* name)
{
    uint32_t slot = 0xFFFFFFFFu;
    if (!name || !name[0]) return -1;
    for (uint32_t i = 0; i < LARDKIT_CFGPROF_MAX; i++) {
        if (s_lardkit.cfgprof[i].valid && streq(s_lardkit.cfgprof[i].name, name)) {
            slot = i;
            break;
        }
        if (slot == 0xFFFFFFFFu && !s_lardkit.cfgprof[i].valid) slot = i;
    }
    if (slot == 0xFFFFFFFFu) slot = 0u;
    cfg_profile_from_current(&s_lardkit.cfgprof[slot], name);
    (void)lardkit_cfgprof_write();
    lardkit_journal_event("cfgprof", "saved profile");
    return 0;
}

int lardkit_cfgprof_load(const char* name)
{
    for (uint32_t i = 0; i < LARDKIT_CFGPROF_MAX; i++) {
        if (s_lardkit.cfgprof[i].valid && streq(s_lardkit.cfgprof[i].name, name)) {
            int r = cfg_profile_apply(&s_lardkit.cfgprof[i]);
            if (r == 0) lardkit_journal_event("cfgprof", "loaded profile");
            return r;
        }
    }
    return -1;
}

uint32_t lardkit_cfgprof_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < LARDKIT_CFGPROF_MAX; i++) if (s_lardkit.cfgprof[i].valid) n++;
    return n;
}

int lardkit_cfgprof_at(uint32_t idx, lardkit_cfg_profile_t* out)
{
    uint32_t seen = 0;
    if (!out) return -1;
    for (uint32_t i = 0; i < LARDKIT_CFGPROF_MAX; i++) {
        if (!s_lardkit.cfgprof[i].valid) continue;
        if (seen == idx) {
            *out = s_lardkit.cfgprof[i];
            return 0;
        }
        seen++;
    }
    return -1;
}

int lardkit_cfgprof_write(void)
{
    FsWritableFile* w = fs_open_writable("cfgprof.lardd");
    static const char header[] = "LARDD 1\nTITLE CFG Profiles\nSECTION Profiles\n";
    if (!w) return -1;
    w->size = 0;
    if (fs_write(w, 0, (const uint8_t*)header, sizeof(header) - 1u) != sizeof(header) - 1u) return -2;
    for (uint32_t i = 0; i < LARDKIT_CFGPROF_MAX; i++) {
        if (!s_lardkit.cfgprof[i].valid) continue;
        report_append(w, "ITEM ");
        report_append(w, s_lardkit.cfgprof[i].name);
        report_append(w, " boot ");
        report_append(w, s_lardkit.cfgprof[i].boot_profile);
        report_append(w, " priority ");
        report_append_i32(w, s_lardkit.cfgprof[i].task_default);
        report_append(w, "\n");
    }
    report_append(w, "END\n");
    return 0;
}

void lardkit_magic_record(const char* input, const char* predicted, int executed, const char* reason)
{
    s_lardkit.magic.has_record = 1u;
    s_lardkit.magic.executed = executed ? 1u : 0u;
    scopy(s_lardkit.magic.input, sizeof(s_lardkit.magic.input), input);
    scopy(s_lardkit.magic.predicted, sizeof(s_lardkit.magic.predicted), predicted);
    scopy(s_lardkit.magic.reason, sizeof(s_lardkit.magic.reason), reason);
}

void lardkit_magic_info(lardkit_magic_info_t* out)
{
    if (!out) return;
    *out = s_lardkit.magic;
}

void lardkit_awakemon_info(lardkit_awakemon_info_t* out)
{
    awake_info_t aw;
    if (!out) return;
    awake_info(&aw);
    out->phase_count = aw.total;
    out->current_phase = aw.phase;
    out->done = aw.done;
    out->percent = aw.total ? (aw.phase * 100u) / aw.total : 0u;
    if (out->percent > 100u) out->percent = 100u;
    scopy(out->current, sizeof(out->current), aw.current);
}

int lardkit_larsview_open(const char* path)
{
    const FsFile* f;
    char out[1024];
    if (!path || !path[0]) path = "lardos.lars";
    f = fs_open(path);
    if (!f || !f->data || f->size == 0) {
        s_lardkit.larsview.last_error = 1u;
        return -1;
    }
    if (lard_doc_to_text((const char*)f->data, f->size, out, sizeof(out)) != 0) {
        s_lardkit.larsview.last_error = 2u;
        return -2;
    }
    s_lardkit.larsview.opened++;
    s_lardkit.larsview.size = f->size;
    s_lardkit.larsview.last_error = 0u;
    if (!streq(s_lardkit.larsview.path, path)) {
        scopy(s_lardkit.larsview.previous_path, sizeof(s_lardkit.larsview.previous_path),
              s_lardkit.larsview.path[0] ? s_lardkit.larsview.path : "lardos.lars");
    }
    scopy(s_lardkit.larsview.path, sizeof(s_lardkit.larsview.path), path);
    return 0;
}

int lardkit_larsview_back(void)
{
    char target[LARDKIT_NAME_MAX + 1u];
    int r;
    if (!s_lardkit.larsview.previous_path[0]) {
        s_lardkit.larsview.last_error = 3u;
        return -1;
    }
    scopy(target, sizeof(target), s_lardkit.larsview.previous_path);
    r = lardkit_larsview_open(target);
    if (r == 0) s_lardkit.larsview.back_count++;
    return r;
}

void lardkit_larsview_info(lardkit_larsview_info_t* out)
{
    if (!out) return;
    *out = s_lardkit.larsview;
}

int lardkit_notes_reset(void)
{
    static const char head[] = "LARDD 1\nTITLE LardOS Notes\n";
    FsWritableFile* w = fs_open_writable("notes.lardd");
    FsWritableFile* plain = fs_open_writable("notes.txt");
    if (!w) return -1;
    w->size = 0;
    if (plain) {
        plain->size = 0;
        fs_mark_dirty();
    }
    return fs_write(w, 0, (const uint8_t*)head, sizeof(head) - 1u) == sizeof(head) - 1u ? 0 : -2;
}

int lardkit_notes_append(const char* text)
{
    FsWritableFile* w = fs_open_writable("notes.lardd");
    FsWritableFile* plain = fs_open_writable("notes.txt");
    static const char prefix[] = "TEXT ";
    static const char nl[] = "\n";
    uint32_t len = 0;
    if (!w || !text || !text[0]) return -1;
    if (w->size == 0 && lardkit_notes_reset() != 0) return -2;
    if (fs_append(w, (const uint8_t*)prefix, sizeof(prefix) - 1u) != sizeof(prefix) - 1u) return -3;
    while (text[len]) len++;
    if (fs_append(w, (const uint8_t*)text, len) != len) return -4;
    if (fs_append(w, (const uint8_t*)nl, sizeof(nl) - 1u) != sizeof(nl) - 1u) return -5;
    if (plain) {
        if (fs_append(plain, (const uint8_t*)text, len) != len) return -6;
        if (fs_append(plain, (const uint8_t*)nl, sizeof(nl) - 1u) != sizeof(nl) - 1u) return -7;
    }
    return 0;
}

int lardkit_userlaw_reset(void)
{
    FsWritableFile* w = fs_open_writable("userlaw.lardd");
    static const char law[] =
        "LARDD 1\n"
        "TITLE LardOS User Law\n"
        "TEXT LardOS is a user-owned, inspectable, self-hosted-feeling operating system.\n"
        "TEXT The system should give control first, then explain risks in plain local files.\n"
        "SECTION Core Values\n"
        "ITEM User ownership: the user may inspect, change, override, repair, and replace OS behavior.\n"
        "ITEM Visibility: powerful actions, recovery state, boot state, permissions, and automatic choices must be visible.\n"
        "ITEM Local self-reliance: OS features use in-tree C, native file formats, and LardOS languages before outside dependencies.\n"
        "ITEM Explainable automation: magic may execute predicted commands, but magic explain must say why.\n"
        "ITEM Reversibility: settings, packages, and risky changes should have rollback, history, or capsule trails.\n"
        "ITEM Repair over halt: panic room, lfsdoctor, bugeye, post, and bootmap exist so the user can recover.\n"
        "ITEM User-grantable power: the user may grant priority lev.10 and enter SUM/raw control knowingly.\n"
        "ITEM Native expression: LARS, LARDD, LGUILIB, LTHEME, LPACK, LFS, and picture Unicode keep the system's surface its own.\n"
        "ITEM Honest releases: a is official, b is beta-experimental, p is hotpatch; hardware profiles name the target.\n"
        "ITEM Communication: OS modules, processes, and other systems should communicate through visible OSLink paths.\n"
        "SECTION Commands\n"
        "ITEM values -> read this law.\n"
        "ITEM userlaw show -> read this law.\n"
        "ITEM userlaw reset -> restore this law.\n"
        "ITEM trust history, priority history, magic explain, bootreplay show, panic capsule -> audit power after it is used.\n"
        "END\n";
    if (!w) return -1;
    w->size = 0;
    return fs_write(w, 0, (const uint8_t*)law, sizeof(law) - 1u) == sizeof(law) - 1u ? 0 : -2;
}

int lardkit_userlaw_check(void)
{
    const FsFile* f = fs_open("userlaw.lardd");
    if (!f || !f->data || f->size < 16u) return -1;
    if (f->data[0] != 'L' || f->data[1] != 'A' || f->data[2] != 'R' || f->data[3] != 'D') return -2;
    return 0;
}

int lardkit_selftest(void)
{
    lardkit_state_t saved = s_lardkit;
    lardkit_magic_info_t mi;
    lardkit_larsview_info_t li;
    if (lardkit_bugeye_scan() < 0) {
        s_lardkit = saved;
        return -1;
    }
    if (!fs_open("bugreport.lardd")) {
        s_lardkit = saved;
        return -10;
    }
    if (lardkit_bugreplay_count() == 0u || !fs_open("bugreplay.lardd")) {
        s_lardkit = saved;
        return -11;
    }
    lardkit_trace_enable(1);
    lardkit_trace_event("selftest", "trace", 7);
    if (lardkit_trace_count() == 0u || lardkit_trace_write() != 0 || !fs_open("trace.lardd")) {
        s_lardkit = saved;
        return -14;
    }
    lardkit_netwatch_enable(1);
    lardkit_netwatch_record("http", "GET selftest", 4);
    if (lardkit_netwatch_count() == 0u || lardkit_netwatch_write() != 0 || !fs_open("netwatch.lardd")) {
        s_lardkit = saved;
        return -15;
    }
    lardkit_journal_event("selftest", "journal");
    if (!fs_open("journal.lardd")) {
        s_lardkit = saved;
        return -16;
    }
    lardkit_post_baseline_begin();
    lardkit_post_baseline_observe("selftest", 1);
    lardkit_post_baseline_finish();
    if (!fs_open("postbaseline.lardd")) {
        s_lardkit = saved;
        return -17;
    }
    if (lardkit_bootreplay_write() != 0 || !fs_open("bootreplay.lardd")) {
        s_lardkit = saved;
        return -18;
    }
    if (lardkit_snapshot("selftest") != 0 || !s_lardkit.rollback.valid) {
        s_lardkit = saved;
        return -2;
    }
    if ((lardkit_trust_caps("shell") & LARDKIT_TRUST_FS) == 0u) {
        s_lardkit = saved;
        return -3;
    }
    if (lardkit_trust_set("shell", LARDKIT_TRUST_RAW, 1) != 0 ||
        lardkit_trust_history_count() == 0u) {
        s_lardkit = saved;
        return -12;
    }
    if (lardkit_bootmap_count() < 6u || !lardkit_bootmap_phase(0)[0]) {
        s_lardkit = saved;
        return -4;
    }
    static const uint8_t theme[] =
        "LTHEME 1\nNAME self\nFG 0xffffff\nBG 0x000000\nACCENT 0xffd84a\nSTYLE linux\nEND\n";
    lardkit_theme_info_t ti;
    if (lardkit_theme_parse(theme, sizeof(theme) - 1u, &ti) != 0 || ti.style_hint != 1u) {
        s_lardkit = saved;
        return -9;
    }
    if (lardkit_cfgprof_save("selftest") != 0 || lardkit_cfgprof_count() == 0u || !fs_open("cfgprof.lardd")) {
        s_lardkit = saved;
        return -19;
    }
    if (lardkit_userlaw_reset() != 0 || lardkit_userlaw_check() != 0 || !fs_open("userlaw.lardd")) {
        s_lardkit = saved;
        return -20;
    }
    lardkit_magic_record("statsu", "status", 1, "edit-distance safe command");
    lardkit_magic_info(&mi);
    if (!mi.has_record || !mi.executed || mi.predicted[0] != 's') {
        s_lardkit = saved;
        return -5;
    }
    if (lardkit_larsview_open("lardos.lars") != 0) {
        s_lardkit = saved;
        return -6;
    }
    if (lardkit_larsview_open("glyph_guide.lardd") != 0 || lardkit_larsview_back() != 0) {
        s_lardkit = saved;
        return -21;
    }
    lardkit_larsview_info(&li);
    if (!li.opened || li.last_error || !li.back_count) {
        s_lardkit = saved;
        return -7;
    }
    if (lardkit_notes_reset() != 0 || lardkit_notes_append("selftest note") != 0 ||
        !fs_open("notes.lardd") || !fs_open("notes.txt")) {
        s_lardkit = saved;
        return -22;
    }
    if (lardkit_oldcheck_run(0) != 0 || s_lardkit.oldcheck.count == 0u) {
        s_lardkit = saved;
        return -8;
    }
    if (lardkit_lfsdoctor_scan(0) != 0 || s_lardkit.lfsdoctor.files == 0u || !fs_open("lfsdoctor.lardd")) {
        s_lardkit = saved;
        return -13;
    }
    lardkit_panicroom_enter();
    if (!lardkit_panicroom_active() || lardkit_panicroom_entries() == 0u ||
        lardkit_panic_capsule_write() != 0 || !fs_open("paniccapsule.lardd")) {
        s_lardkit = saved;
        return -23;
    }
    lardkit_panicroom_exit();
    if (lardkit_panicroom_active()) {
        s_lardkit = saved;
        return -24;
    }
    s_lardkit = saved;
    return 0;
}
