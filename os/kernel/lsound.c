#include "lsound.h"

#include "fs.h"
#include "io.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

#define LSOUND_CONFIG_FILE "sound.lardd"
#define LSOUND_DEFAULT_BOOT "boot.lsnd"
#define LSOUND_EVENT_MAX 96u
#define LSOUND_SWEEP_STEP_MAX 32u
#define LSOUND_NOTE_MS_MAX 2000u
#define LSOUND_REST_MS_MAX 2000u
#define LSOUND_PIT_HZ 1193180u

static uint32_t s_enabled = 1u;
static uint32_t s_boot_enabled = 1u;
static uint32_t s_fx_enabled = 1u;
static uint32_t s_events;
static uint32_t s_notes;
static uint32_t s_rests;
static uint32_t s_sweeps;
static uint32_t s_last_error;
static char s_boot_file[LSOUND_NAME_MAX + 1u] = LSOUND_DEFAULT_BOOT;
static char s_last_file[LSOUND_NAME_MAX + 1u];

static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && lower_ascii(a[i]) == lower_ascii(b[i])) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static void copy_name(char* dst, uint32_t cap, const char* src)
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

static const char* skip_space(const char* p, const char* e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    return p;
}

static int read_word(const char** io, const char* e, char* out, uint32_t cap)
{
    const char* p = skip_space(*io, e);
    uint32_t n = 0;
    if (!out || cap == 0 || p >= e || *p == '\n' || *p == '#') return -1;
    while (p < e && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
           *p != '#' && n + 1u < cap) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    *io = p;
    return n ? 0 : -1;
}

static int parse_u32_word(const char* s, uint32_t* out)
{
    uint32_t v = 0;
    int any = 0;
    if (!s || !out) return -1;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        any = 1;
        s++;
    }
    if (!any) return -1;
    *out = v;
    return 0;
}

static int on_word(const char* s)
{
    return streq_ci(s, "on") || streq_ci(s, "1") || streq_ci(s, "yes") ||
           streq_ci(s, "true") || streq_ci(s, "enable") || streq_ci(s, "enabled");
}

static int off_word(const char* s)
{
    return streq_ci(s, "off") || streq_ci(s, "0") || streq_ci(s, "no") ||
           streq_ci(s, "false") || streq_ci(s, "disable") || streq_ci(s, "disabled");
}

static int parse_onoff(const char* s, uint32_t* out)
{
    if (!s || !out) return -1;
    if (on_word(s)) { *out = 1u; return 0; }
    if (off_word(s)) { *out = 0u; return 0; }
    return -1;
}

static void speaker_off(void)
{
    outb(0x61u, (uint8_t)(inb(0x61u) & 0xFCu));
}

static void speaker_tone(uint32_t hz)
{
    uint32_t div;
    uint8_t v;
    if (hz < 37u) hz = 37u;
    if (hz > 20000u) hz = 20000u;
    div = LSOUND_PIT_HZ / hz;
    if (div == 0) div = 1;
    outb(0x43u, 0xB6u);
    outb(0x42u, (uint8_t)(div & 0xFFu));
    outb(0x42u, (uint8_t)((div >> 8) & 0xFFu));
    v = inb(0x61u);
    if ((v & 0x03u) != 0x03u) outb(0x61u, (uint8_t)(v | 0x03u));
}

static void delay_ms(uint32_t ms)
{
    volatile uint32_t spin;
    if (ms > LSOUND_NOTE_MS_MAX) ms = LSOUND_NOTE_MS_MAX;
    spin = ms * 12000u;
    while (spin--) __asm__ __volatile__("pause");
}

static void play_note(uint32_t hz, uint32_t ms)
{
    if (ms > LSOUND_NOTE_MS_MAX) ms = LSOUND_NOTE_MS_MAX;
    if (ms == 0) return;
    speaker_tone(hz);
    delay_ms(ms);
    speaker_off();
}

static int play_rest(uint32_t ms)
{
    if (ms > LSOUND_REST_MS_MAX) ms = LSOUND_REST_MS_MAX;
    speaker_off();
    delay_ms(ms);
    return 0;
}

static int parse_lsnd_text(const char* name, const uint8_t* data, uint32_t size, int play)
{
    const char* p = (const char*)data;
    const char* e = p + size;
    uint32_t seen = 0;
    uint32_t events = 0;
    uint32_t notes = 0;
    uint32_t rests = 0;
    uint32_t sweeps = 0;
    if (!data || size == 0) return -1;
    while (p < e && events < LSOUND_EVENT_MAX) {
        const char* line = p;
        const char* lend = p;
        const char* q;
        char key[16];
        while (lend < e && *lend != '\n') lend++;
        q = line;
        if (read_word(&q, lend, key, sizeof(key)) == 0) {
            if (key[0] != '#') {
                if (streq_ci(key, "LSND") || streq_ci(key, "LVSND")) {
                    seen = 1u;
                } else if (streq_ci(key, "NOTE") || streq_ci(key, "TONE")) {
                    char whz[16];
                    char wms[16];
                    uint32_t hz;
                    uint32_t ms;
                    if (read_word(&q, lend, whz, sizeof(whz)) != 0 ||
                        read_word(&q, lend, wms, sizeof(wms)) != 0 ||
                        parse_u32_word(whz, &hz) != 0 ||
                        parse_u32_word(wms, &ms) != 0) {
                        s_last_error = 12u;
                        return -12;
                    }
                    events++;
                    notes++;
                    if (play) play_note(hz, ms);
                } else if (streq_ci(key, "REST") || streq_ci(key, "WAIT")) {
                    char wms[16];
                    uint32_t ms;
                    if (read_word(&q, lend, wms, sizeof(wms)) != 0 ||
                        parse_u32_word(wms, &ms) != 0) {
                        s_last_error = 13u;
                        return -13;
                    }
                    events++;
                    rests++;
                    if (play) (void)play_rest(ms);
                } else if (streq_ci(key, "SWEEP") || streq_ci(key, "VEC")) {
                    char wa[16];
                    char wb[16];
                    char wms[16];
                    char ws[16];
                    uint32_t a;
                    uint32_t b;
                    uint32_t ms;
                    uint32_t steps = 8u;
                    if (read_word(&q, lend, wa, sizeof(wa)) != 0 ||
                        read_word(&q, lend, wb, sizeof(wb)) != 0 ||
                        read_word(&q, lend, wms, sizeof(wms)) != 0 ||
                        parse_u32_word(wa, &a) != 0 ||
                        parse_u32_word(wb, &b) != 0 ||
                        parse_u32_word(wms, &ms) != 0) {
                        s_last_error = 14u;
                        return -14;
                    }
                    if (read_word(&q, lend, ws, sizeof(ws)) == 0) {
                        (void)parse_u32_word(ws, &steps);
                    }
                    if (steps == 0) steps = 1u;
                    if (steps > LSOUND_SWEEP_STEP_MAX) steps = LSOUND_SWEEP_STEP_MAX;
                    events++;
                    sweeps++;
                    if (play) {
                        uint32_t part = ms / steps;
                        if (part == 0) part = 1u;
                        for (uint32_t i = 0; i < steps; i++) {
                            uint32_t hz = (steps == 1u) ? b : (a + ((b > a ? b - a : 0u) * i) / (steps - 1u));
                            if (b < a && steps > 1u) hz = a - ((a - b) * i) / (steps - 1u);
                            play_note(hz, part);
                        }
                    }
                } else if (streq_ci(key, "TITLE") || streq_ci(key, "VOL") ||
                           streq_ci(key, "VOLUME") || streq_ci(key, "TEMPO") ||
                           streq_ci(key, "END")) {
                    /* Metadata-only vector records stay user-editable. */
                } else {
                    s_last_error = 15u;
                    return -15;
                }
            }
        }
        p = (lend < e) ? lend + 1 : lend;
    }
    if (!seen) {
        s_last_error = 16u;
        return -16;
    }
    if (events >= LSOUND_EVENT_MAX && p < e) {
        s_last_error = 17u;
        return -17;
    }
    s_events = events;
    s_notes = notes;
    s_rests = rests;
    s_sweeps = sweeps;
    s_last_error = 0;
    copy_name(s_last_file, sizeof(s_last_file), name);
    if (play) speaker_off();
    return 0;
}

static int write_config(void)
{
    FsWritableFile* w = fs_open_or_create_writable(LSOUND_CONFIG_FILE);
    char buf[256];
    int n;
    if (!w) return -1;
    n = snprintf(buf, sizeof(buf),
                 "LARDD 1\nTITLE LardOS Sound\nSOUND %s\nBOOT %s\nFX %s\nBOOTFILE %s\nEND\n",
                 s_enabled ? "on" : "off",
                 s_boot_enabled ? "on" : "off",
                 s_fx_enabled ? "on" : "off",
                 s_boot_file);
    if (n < 0) return -2;
    if ((uint32_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    (void)fs_write(w, 0, (const uint8_t*)buf, (uint32_t)n);
    return 0;
}

static void parse_config_data(const uint8_t* data, uint32_t size)
{
    const char* p = (const char*)data;
    const char* e = p + size;
    while (p < e) {
        const char* line = p;
        const char* lend = p;
        const char* q;
        char key[20];
        char val[32];
        uint32_t on;
        while (lend < e && *lend != '\n') lend++;
        q = line;
        if (read_word(&q, lend, key, sizeof(key)) == 0 &&
            read_word(&q, lend, val, sizeof(val)) == 0) {
            if ((streq_ci(key, "SOUND") || streq_ci(key, "ENABLED")) &&
                parse_onoff(val, &on) == 0) {
                s_enabled = on;
            } else if (streq_ci(key, "BOOT") && parse_onoff(val, &on) == 0) {
                s_boot_enabled = on;
            } else if ((streq_ci(key, "FX") || streq_ci(key, "EFFECTS")) &&
                       parse_onoff(val, &on) == 0) {
                s_fx_enabled = on;
            } else if (streq_ci(key, "BOOTFILE") || streq_ci(key, "BOOT_FILE")) {
                copy_name(s_boot_file, sizeof(s_boot_file), val);
            }
        }
        p = (lend < e) ? lend + 1 : lend;
    }
}

void lsound_init(void)
{
    s_enabled = 1u;
    s_boot_enabled = 1u;
    s_fx_enabled = 1u;
    copy_name(s_boot_file, sizeof(s_boot_file), LSOUND_DEFAULT_BOOT);
    copy_name(s_last_file, sizeof(s_last_file), "");
    s_events = s_notes = s_rests = s_sweeps = 0;
    s_last_error = 0;
    (void)lsound_reload();
}

int lsound_reload(void)
{
    const FsFile* f = fs_open(LSOUND_CONFIG_FILE);
    if (!f || !f->data || f->size == 0) return write_config();
    parse_config_data(f->data, f->size);
    s_last_error = 0;
    return 0;
}

int lsound_set_enabled(int on)
{
    s_enabled = on ? 1u : 0u;
    speaker_off();
    return write_config();
}

int lsound_set_boot_enabled(int on)
{
    s_boot_enabled = on ? 1u : 0u;
    return write_config();
}

int lsound_set_fx_enabled(int on)
{
    s_fx_enabled = on ? 1u : 0u;
    return write_config();
}

int lsound_set_boot_file(const char* file)
{
    if (!file || !file[0]) return -1;
    copy_name(s_boot_file, sizeof(s_boot_file), file);
    return write_config();
}

int lsound_play_file(const char* file)
{
    const FsFile* f;
    const char* name = (file && file[0]) ? file : s_boot_file;
    if (!s_enabled) {
        copy_name(s_last_file, sizeof(s_last_file), name);
        s_last_error = 0;
        return 1;
    }
    f = fs_open(name);
    if (!f || !f->data || f->size == 0) {
        s_last_error = 21u;
        return -21;
    }
    return parse_lsnd_text(name, f->data, f->size, 1);
}

int lsound_play_effect(const char* name)
{
    const char* file = NULL;
    if (!name || !name[0]) name = "ok";
    if (streq_ci(name, "boot") || streq_ci(name, "startup")) {
        if (!s_boot_enabled) return 1;
        file = s_boot_file;
    } else {
        if (!s_fx_enabled) return 1;
        if (streq_ci(name, "click")) file = "click.lsnd";
        else if (streq_ci(name, "ok") || streq_ci(name, "success")) file = "ok.lsnd";
        else if (streq_ci(name, "error") || streq_ci(name, "bad")) file = "error.lsnd";
        else if (streq_ci(name, "notify") || streq_ci(name, "notice")) file = "notify.lsnd";
        else file = name;
    }
    return lsound_play_file(file);
}

int lsound_boot(void)
{
    return lsound_play_effect("boot");
}

int lsound_write_template(const char* file)
{
    static const char tmpl[] =
        "LSND 1\n"
        "TITLE User Vector Sound\n"
        "NOTE 440 80\n"
        "REST 40\n"
        "SWEEP 520 760 180 6\n"
        "END\n";
    FsWritableFile* w = fs_open_or_create_writable(file && file[0] ? file : "usersound.lsnd");
    if (!w) return -1;
    (void)fs_write(w, 0, (const uint8_t*)tmpl, sizeof(tmpl) - 1u);
    return 0;
}

void lsound_info(lsound_info_t* out)
{
    if (!out) return;
    out->enabled = s_enabled;
    out->boot_enabled = s_boot_enabled;
    out->fx_enabled = s_fx_enabled;
    out->events = s_events;
    out->notes = s_notes;
    out->rests = s_rests;
    out->sweeps = s_sweeps;
    out->last_error = s_last_error;
    copy_name(out->boot_file, sizeof(out->boot_file), s_boot_file);
    copy_name(out->last_file, sizeof(out->last_file), s_last_file);
}

int lsound_selftest(void)
{
    static const uint8_t data[] =
        "LSND 1\n"
        "TITLE test\n"
        "NOTE 440 1\n"
        "REST 1\n"
        "SWEEP 330 660 3 3\n"
        "END\n";
    int r = parse_lsnd_text("selftest.lsnd", data, sizeof(data) - 1u, 0);
    if (r != 0) return r;
    if (s_events != 3u || s_notes != 1u || s_rests != 1u || s_sweeps != 1u) return -30;
    return 0;
}
