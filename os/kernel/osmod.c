#include "osmod.h"

#include "awake.h"
#include "bluetooth.h"
#include "bootprof.h"
#include "fs.h"
#include "gui.h"
#include "lardkit.h"
#include "lconnect.h"
#include "lsound.h"
#include "string.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const char* s_osmod_error = "ok";

static const char s_default_osmod[] =
    "OSMOD 1\n"
    "NAME default-user-mode\n"
    "BOOT normal\n"
    "AWAKE off\n"
    "RENDER_AA none\n"
    "BRIGHTNESS 100\n"
    "RESIZE stretch\n"
    "LSB off\n"
    "VBLANK off\n"
    "SOUND on\n"
    "BLUETOOTH off\n"
    "LCONNECT off\n"
    "END\n";

static char lower_ch(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && lower_ch(a[i]) == lower_ch(b[i])) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char* trim(char* s)
{
    char* e;
    while (*s && is_space(*s)) s++;
    e = s + strlen(s);
    while (e > s && is_space(e[-1])) e--;
    *e = '\0';
    return s;
}

static void copy_word(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && !is_space(src[i]) && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int parse_bool(const char* s, int32_t* out)
{
    if (streq_ci(s, "on") || streq_ci(s, "yes") || streq_ci(s, "true") ||
        streq_ci(s, "enable") || streq_ci(s, "enabled") || streq_ci(s, "1")) {
        *out = 1;
        return 0;
    }
    if (streq_ci(s, "off") || streq_ci(s, "no") || streq_ci(s, "false") ||
        streq_ci(s, "disable") || streq_ci(s, "disabled") || streq_ci(s, "0")) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int parse_u32(const char* s, uint32_t* out)
{
    uint32_t v = 0;
    uint32_t i = 0;
    if (!s || !s[0]) return -1;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10u + (uint32_t)(s[i] - '0');
        i++;
    }
    *out = v;
    return 0;
}

static int parse_boot(const char* s, char* out)
{
    if (streq_ci(s, "awake")) s = "awakening";
    if (streq_ci(s, "normal") || streq_ci(s, "safe") ||
        streq_ci(s, "netoff") || streq_ci(s, "dev") ||
        streq_ci(s, "awakening")) {
        copy_word(out, OSMOD_BOOT_MAX + 1u, s);
        return 0;
    }
    return -1;
}

static int parse_aa(const char* s, int32_t* out)
{
    if (streq_ci(s, "none") || streq_ci(s, "noaa") || streq_ci(s, "0")) {
        *out = GUI_AA_NONE;
        return 0;
    }
    if (streq_ci(s, "antianti") || streq_ci(s, "unaa") || streq_ci(s, "1")) {
        *out = GUI_AA_UNAA;
        return 0;
    }
    if (streq_ci(s, "basic") || streq_ci(s, "aa") || streq_ci(s, "2")) {
        *out = GUI_AA_BASIC;
        return 0;
    }
    if (streq_ci(s, "nonlinear") || streq_ci(s, "non-linear") ||
        streq_ci(s, "sharp") || streq_ci(s, "3")) {
        *out = GUI_AA_NONLINEAR;
        return 0;
    }
    return -1;
}

static int parse_resize(const char* s, int32_t* out)
{
    if (streq_ci(s, "live") || streq_ci(s, "reflow") || streq_ci(s, "0")) {
        *out = GUI_RESIZE_LIVE;
        return 0;
    }
    if (streq_ci(s, "stretch") || streq_ci(s, "stable") ||
        streq_ci(s, "squash") || streq_ci(s, "rubber") ||
        streq_ci(s, "1") || streq_ci(s, "on")) {
        *out = GUI_RESIZE_STRETCH;
        return 0;
    }
    return -1;
}

static int read_pair(char* line, char** key, char** value)
{
    char* p;
    *key = trim(line);
    if (!(*key)[0]) return -1;
    p = *key;
    while (*p && !is_space(*p)) p++;
    if (*p) *p++ = '\0';
    *value = trim(p);
    return 0;
}

void osmod_init_profile(osmod_profile_t* out)
{
    if (!out) return;
    for (uint32_t i = 0; i < sizeof(*out); i++) ((uint8_t*)out)[i] = 0;
    out->awake = OSMOD_UNSET;
    out->aa = OSMOD_UNSET;
    out->brightness = OSMOD_UNSET;
    out->resize = OSMOD_UNSET;
    out->lsb = OSMOD_UNSET;
    out->vblank = OSMOD_UNSET;
    out->sound = OSMOD_UNSET;
    out->bluetooth = OSMOD_UNSET;
    out->lconnect = OSMOD_UNSET;
}

const char* osmod_last_error(void)
{
    return s_osmod_error;
}

const char* osmod_aa_name(int32_t mode)
{
    if (mode == GUI_AA_NONE) return "none";
    if (mode == GUI_AA_UNAA) return "antianti";
    if (mode == GUI_AA_BASIC) return "basic";
    if (mode == GUI_AA_NONLINEAR) return "nonlinear";
    return "unset";
}

const char* osmod_resize_name(int32_t mode)
{
    if (mode == GUI_RESIZE_LIVE) return "live";
    if (mode == GUI_RESIZE_STRETCH) return "stretch";
    return "unset";
}

const char* osmod_default_sample(void)
{
    return s_default_osmod;
}

uint32_t osmod_default_sample_size(void)
{
    return (uint32_t)(sizeof(s_default_osmod) - 1u);
}

int osmod_parse(const uint8_t* data, uint32_t size, osmod_profile_t* out)
{
    osmod_profile_t p;
    uint32_t pos = 0;
    uint32_t header = 0;
    uint32_t saw_end = 0;
    char line[160];

    if (!data || !out || size == 0) {
        s_osmod_error = "empty osmod";
        return -1;
    }
    osmod_init_profile(&p);

    while (pos < size) {
        uint32_t n = 0;
        char* hash;
        char* key;
        char* value;
        while (pos < size && data[pos] != '\n' && n + 1u < sizeof(line)) {
            line[n++] = (char)data[pos++];
        }
        while (pos < size && data[pos] != '\n') pos++;
        if (pos < size && data[pos] == '\n') pos++;
        line[n] = '\0';
        p.lines++;
        hash = line;
        while (*hash) {
            if (*hash == '#') {
                *hash = '\0';
                break;
            }
            hash++;
        }
        if (read_pair(line, &key, &value) != 0) continue;
        if (!header) {
            if (!streq_ci(key, "OSMOD") || !streq_ci(value, "1")) {
                s_osmod_error = "missing OSMOD 1 header";
                return -2;
            }
            header = 1;
            continue;
        }
        if (streq_ci(key, "END")) {
            saw_end = 1;
            break;
        }
        if (streq_ci(key, "NAME")) {
            copy_word(p.name, sizeof(p.name), value);
            continue;
        }
        if (streq_ci(key, "BOOT") || streq_ci(key, "BOOTPROF") || streq_ci(key, "PROFILE")) {
            if (parse_boot(value, p.boot) != 0) {
                s_osmod_error = "bad BOOT profile";
                return -3;
            }
            p.have_boot = 1;
            p.directives++;
            continue;
        }
        if (streq_ci(key, "AWAKE") || streq_ci(key, "AWAKENING")) {
            if (parse_bool(value, &p.awake) != 0) {
                s_osmod_error = "bad AWAKE value";
                return -4;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "RENDER_AA") || streq_ci(key, "AA") || streq_ci(key, "ANTIALIAS")) {
            if (parse_aa(value, &p.aa) != 0) {
                s_osmod_error = "bad RENDER_AA value";
                return -5;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "BRIGHTNESS") || streq_ci(key, "BRIGHT")) {
            uint32_t v;
            if (parse_u32(value, &v) != 0 || v < 50u || v > 150u) {
                s_osmod_error = "bad BRIGHTNESS range";
                return -6;
            }
            p.brightness = (int32_t)v;
            p.directives++;
            continue;
        }
        if (streq_ci(key, "RESIZE") || streq_ci(key, "WINRESIZE")) {
            if (parse_resize(value, &p.resize) != 0) {
                s_osmod_error = "bad RESIZE value";
                return -7;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "LSB") || streq_ci(key, "SCREENRAM_LSB")) {
            if (parse_bool(value, &p.lsb) != 0) {
                s_osmod_error = "bad LSB value";
                return -8;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "VBLANK") || streq_ci(key, "VSYNC")) {
            if (parse_bool(value, &p.vblank) != 0) {
                s_osmod_error = "bad VBLANK value";
                return -9;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "SOUND") || streq_ci(key, "AUDIO")) {
            if (parse_bool(value, &p.sound) != 0) {
                s_osmod_error = "bad SOUND value";
                return -10;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "BLUETOOTH") || streq_ci(key, "BT")) {
            if (parse_bool(value, &p.bluetooth) != 0) {
                s_osmod_error = "bad BLUETOOTH value";
                return -11;
            }
            p.directives++;
            continue;
        }
        if (streq_ci(key, "LCONNECT") || streq_ci(key, "CONNECT")) {
            if (parse_bool(value, &p.lconnect) != 0) {
                s_osmod_error = "bad LCONNECT value";
                return -12;
            }
            p.directives++;
            continue;
        }
        p.warnings++;
        if (!p.note[0]) {
            snprintf(p.note, sizeof(p.note), "ignored unknown key %s", key);
        }
    }
    if (!header) {
        s_osmod_error = "missing OSMOD 1 header";
        return -13;
    }
    if (!saw_end) p.warnings++;
    if (!p.name[0]) copy_word(p.name, sizeof(p.name), "unnamed-osmod");
    if (p.have_boot && p.awake != OSMOD_UNSET) {
        p.warnings++;
        if (!p.note[0]) snprintf(p.note, sizeof(p.note), "BOOT then AWAKE: AWAKE wins when applied");
    }
    if (p.directives == 0) {
        s_osmod_error = "no mode directives";
        return -14;
    }
    *out = p;
    s_osmod_error = "ok";
    return 0;
}

int osmod_load_file(const char* name, osmod_profile_t* out)
{
    const FsFile* f = fs_open(name ? name : "default.osmod");
    if (!f || !f->data || f->size == 0) {
        s_osmod_error = "osmod file not found";
        return -1;
    }
    return osmod_parse(f->data, f->size, out);
}

int osmod_write_report(const osmod_profile_t* p, int applied)
{
    FsWritableFile* w;
    char buf[1536];
    int n;
    if (!p) return -1;
    w = fs_open_or_create_writable("osmod.lardd");
    if (!w) return -2;
    n = snprintf(buf, sizeof(buf),
                 "LARDD 1\n"
                 "TITLE OSMOD Report\n"
                 "TEXT Last .osmod mode file %s.\n"
                 "SECTION Profile\n"
                 "ITEM name %s\n"
                 "ITEM applied %s\n"
                 "ITEM directives %u\n"
                 "ITEM warnings %u\n"
                 "ITEM boot %s\n"
                 "ITEM awake %s\n"
                 "ITEM aa %s\n"
                 "ITEM brightness %d\n"
                 "ITEM resize %s\n"
                 "ITEM lsb %s\n"
                 "ITEM vblank %s\n"
                 "ITEM sound %s\n"
                 "ITEM bluetooth %s\n"
                 "ITEM lconnect %s\n"
                 "TEXT %s\n"
                 "END\n",
                 applied > 0 ? "applied" : (applied < 0 ? "failed to apply" : "previewed"),
                 p->name,
                 applied > 0 ? "yes" : (applied < 0 ? "failed" : "no"),
                 p->directives,
                 p->warnings,
                 p->have_boot ? p->boot : "unset",
                 p->awake == OSMOD_UNSET ? "unset" : (p->awake ? "on" : "off"),
                 osmod_aa_name(p->aa),
                 (int)p->brightness,
                 osmod_resize_name(p->resize),
                 p->lsb == OSMOD_UNSET ? "unset" : (p->lsb ? "on" : "off"),
                 p->vblank == OSMOD_UNSET ? "unset" : (p->vblank ? "on" : "off"),
                 p->sound == OSMOD_UNSET ? "unset" : (p->sound ? "on" : "off"),
                 p->bluetooth == OSMOD_UNSET ? "unset" : (p->bluetooth ? "on" : "off"),
                 p->lconnect == OSMOD_UNSET ? "unset" : (p->lconnect ? "on" : "off"),
                 p->note[0] ? p->note : "All recognized settings stay visible and editable.");
    if (n < 0) return -3;
    if ((uint32_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    return fs_write(w, 0, (const uint8_t*)buf, (uint32_t)n) == (uint32_t)n ? 0 : -4;
}

int osmod_apply(const osmod_profile_t* p)
{
    uint32_t changes = 0;
    uint32_t errors = 0;
    if (!p) return -1;
    (void)lardkit_snapshot("osmod");
    if (p->have_boot) {
        if (bootprof_set(p->boot) == 0) changes++;
        else errors++;
    }
    if (p->awake != OSMOD_UNSET) {
        if (bootprof_set(p->awake ? "awakening" : "normal") == 0) {
            if (!p->awake) awake_enable(0, 0);
            changes++;
        } else {
            errors++;
        }
    }
    if (p->aa != OSMOD_UNSET) {
        if (gui_render_set_aa_mode((int)p->aa) == 0) changes++;
        else errors++;
    }
    if (p->brightness != OSMOD_UNSET) {
        if (gui_render_set_brightness((int)p->brightness) == 0) changes++;
        else errors++;
    }
    if (p->resize != OSMOD_UNSET) {
        if (gui_resize_set_mode((int)p->resize) == 0) changes++;
        else errors++;
    }
    if (p->lsb != OSMOD_UNSET) {
        if (gui_screenram_lsb_enable((int)p->lsb) == 0) changes++;
        else errors++;
    }
    if (p->vblank != OSMOD_UNSET) {
        if (gui_vblank_enable((int)p->vblank) == 0) changes++;
        else errors++;
    }
    if (p->sound != OSMOD_UNSET) {
        if (lsound_set_enabled((int)p->sound) == 0) changes++;
        else errors++;
    }
    if (p->bluetooth != OSMOD_UNSET) {
        if (lbt_enable((int)p->bluetooth) == 0) changes++;
        else errors++;
    }
    if (p->lconnect != OSMOD_UNSET) {
        if (lconnect_enable((int)p->lconnect) == 0) changes++;
        else errors++;
    }
    (void)changes;
    lardkit_journal_event("osmod", p->name);
    lardkit_trace_event("osmod", errors ? "apply-failed" : "apply", (int32_t)p->directives);
    (void)osmod_write_report(p, errors ? -1 : 1);
    if (errors) {
        s_osmod_error = "one or more settings failed";
        return -2;
    }
    s_osmod_error = "ok";
    return 0;
}

int osmod_selftest(void)
{
    osmod_profile_t p;
    if (osmod_parse((const uint8_t*)s_default_osmod, (uint32_t)(sizeof(s_default_osmod) - 1u), &p) != 0) return -1;
    if (!p.have_boot || strcmp(p.boot, "normal") != 0) return -2;
    if (p.awake != 0 || p.aa != GUI_AA_NONE || p.brightness != 100) return -3;
    if (p.resize != GUI_RESIZE_STRETCH || p.sound != 1 || p.lconnect != 0) return -4;
    if (p.directives < 9u) return -5;
    return 0;
}
