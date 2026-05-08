#include "lardkit.h"

#include "bootprof.h"
#include "exexgui.h"
#include "exgui.h"
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
    lardkit_bugeye_info_t bugeye;
    lardkit_bugreplay_frame_t bugreplay[LARDKIT_BUGREPLAY_MAX];
    uint32_t bugreplay_count;
    uint32_t bugreplay_next;
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

void lardkit_bugreplay_clear(void)
{
    for (uint32_t i = 0; i < LARDKIT_BUGREPLAY_MAX; i++) {
        for (uint32_t j = 0; j < sizeof(s_lardkit.bugreplay[i]); j++) ((uint8_t*)&s_lardkit.bugreplay[i])[j] = 0;
    }
    s_lardkit.bugreplay_count = 0;
    s_lardkit.bugreplay_next = 1u;
    (void)lardkit_bugreplay_write();
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
    exgui_info_t eg;
    exexgui_info_t xg;
    bootprof_info_t bp;
    awake_info_t aw;
    lassist_info_t bi;
    exgui_info(&eg);
    exexgui_info(&xg);
    bootprof_info(&bp);
    awake_info(&aw);
    lassist_info(&bi);
    s_lardkit.rollback.valid = 1u;
    s_lardkit.rollback.snapshots++;
    scopy(s_lardkit.rollback.label, sizeof(s_lardkit.rollback.label),
          label && label[0] ? label : "manual");
    s_lardkit.rollback.exgui_enabled = eg.enabled;
    s_lardkit.rollback.exgui_style = eg.style;
    s_lardkit.rollback.exgui_layout = eg.layout;
    s_lardkit.rollback.exexgui_enabled = xg.enabled;
    s_lardkit.rollback.exexgui_focus = xg.focus;
    s_lardkit.rollback.buddy_enabled = bi.enabled;
    s_lardkit.rollback.http_post = (uint32_t)gui_http_post_mode();
    s_lardkit.rollback.task_default = taskprio_default_priority();
    scopy(s_lardkit.rollback.boot_profile, sizeof(s_lardkit.rollback.boot_profile), bp.name);
    s_lardkit.rollback.awake_enabled = aw.enabled;
    s_lardkit.rollback.theme = s_lardkit.active_theme;
    return 0;
}

static const char* style_name(uint32_t style)
{
    if (style == 1u) return "linux";
    if (style == 2u) return "mac";
    return "win";
}

static const char* layout_name(uint32_t layout)
{
    if (layout == 1u) return "tile";
    if (layout == 2u) return "stack";
    return "float";
}

static const char* focus_name(uint32_t focus)
{
    if (focus == 1u) return "term";
    if (focus == 2u) return "info";
    return "gui";
}

int lardkit_rollback_apply(void)
{
    if (!s_lardkit.rollback.valid) return -1;
    exgui_enable((int)s_lardkit.rollback.exgui_enabled);
    (void)exgui_set_style(style_name(s_lardkit.rollback.exgui_style));
    (void)exgui_set_layout(layout_name(s_lardkit.rollback.exgui_layout));
    exexgui_enable((int)s_lardkit.rollback.exexgui_enabled);
    (void)exexgui_set_focus(focus_name(s_lardkit.rollback.exexgui_focus));
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
    (void)exgui_set_style(style_name(s_themes[idx].style_hint));
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
    (void)exgui_set_style(style_name(parsed.style_hint));
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
    char out[256];
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
    scopy(s_lardkit.larsview.path, sizeof(s_lardkit.larsview.path), path);
    return 0;
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
    if (!w) return -1;
    w->size = 0;
    return fs_write(w, 0, (const uint8_t*)head, sizeof(head) - 1u) == sizeof(head) - 1u ? 0 : -2;
}

int lardkit_notes_append(const char* text)
{
    FsWritableFile* w = fs_open_writable("notes.lardd");
    static const char prefix[] = "TEXT ";
    static const char nl[] = "\n";
    if (!w || !text || !text[0]) return -1;
    if (w->size == 0 && lardkit_notes_reset() != 0) return -2;
    if (fs_append(w, (const uint8_t*)prefix, sizeof(prefix) - 1u) != sizeof(prefix) - 1u) return -3;
    uint32_t len = 0;
    while (text[len]) len++;
    if (fs_append(w, (const uint8_t*)text, len) != len) return -4;
    return fs_append(w, (const uint8_t*)nl, sizeof(nl) - 1u) == sizeof(nl) - 1u ? 0 : -5;
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
    lardkit_larsview_info(&li);
    if (!li.opened || li.last_error) {
        s_lardkit = saved;
        return -7;
    }
    if (lardkit_oldcheck_run(0) != 0 || s_lardkit.oldcheck.count == 0u) {
        s_lardkit = saved;
        return -8;
    }
    if (lardkit_lfsdoctor_scan(0) != 0 || s_lardkit.lfsdoctor.files == 0u || !fs_open("lfsdoctor.lardd")) {
        s_lardkit = saved;
        return -13;
    }
    if (lardkit_panic_capsule_write() != 0 || !fs_open("paniccapsule.lardd")) {
        s_lardkit = saved;
        return -14;
    }
    s_lardkit = saved;
    return 0;
}
