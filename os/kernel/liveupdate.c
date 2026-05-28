#include "liveupdate.h"

#include "drfl.h"
#include "fs.h"
#include "fstwt.h"
#include "gui.h"
#include "kmo.h"
#include "lardkit.h"
#include "lguilib.h"
#include "rxe.h"
#include "string.h"
#include "sysrxe.h"

#include <stddef.h>
#include <stdint.h>

#define LIVEUPDATE_PAYLOAD_MAX 8192u

static liveupdate_info_t s_live;
static char s_decode_buf[LIVEUPDATE_PAYLOAD_MAX];

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static uint32_t slen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
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

static void sappend(char* dst, uint32_t cap, const char* src)
{
    uint32_t n = slen(dst);
    uint32_t i = 0;
    if (!dst || cap == 0 || !src) return;
    while (src[i] && n + 1u < cap) dst[n++] = src[i++];
    dst[n] = '\0';
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && lower_char(a[i]) == lower_char(b[i])) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int suffix_ci(const char* name, const char* suffix)
{
    uint32_t nl = slen(name);
    uint32_t sl = slen(suffix);
    if (!name || !suffix || nl < sl) return 0;
    for (uint32_t i = 0; i < sl; i++) {
        if (lower_char(name[nl - sl + i]) != lower_char(suffix[i])) return 0;
    }
    return 1;
}

static void record_result(const char* file, const char* scope, const char* detail, int r)
{
    s_live.last_result = r;
    scopy(s_live.last_file, sizeof(s_live.last_file), file);
    scopy(s_live.last_scope, sizeof(s_live.last_scope), scope);
    scopy(s_live.last_detail, sizeof(s_live.last_detail), detail);
    if (r < 0) s_live.failures++;
}

static const char* auto_scope_for(const char* name)
{
    if (suffix_ci(name, ".kmo")) return "kmo";
    if (suffix_ci(name, ".sysrxe")) return "sysrxe";
    if (suffix_ci(name, ".rxe")) return "rxe";
    if (suffix_ci(name, ".drfl")) return "drivers";
    if (suffix_ci(name, ".fstwts")) return "fstwt";
    if (suffix_ci(name, ".ltheme")) return "ltheme";
    if (suffix_ci(name, ".lguilib")) return "lguilib";
    if (suffix_ci(name, ".lwall") || streq_ci(name, "wallpaper.lardd")) return "wallpaper";
    return "file";
}

static int decode_text(const char* in, const char** out_data, uint32_t* out_len)
{
    uint32_t i = 0;
    uint32_t o = 0;
    if (!in || !out_data || !out_len) return -1;
    while (in[i]) {
        char c = in[i++];
        if (c == '\\' && in[i]) {
            char n = in[i++];
            if (n == 'n') c = '\n';
            else if (n == 'r') c = '\r';
            else if (n == 't') c = '\t';
            else if (n == '\\') c = '\\';
            else {
                if (o + 1u >= sizeof(s_decode_buf)) return -2;
                s_decode_buf[o++] = '\\';
                c = n;
            }
        }
        if (o + 1u >= sizeof(s_decode_buf)) return -2;
        s_decode_buf[o++] = c;
    }
    s_decode_buf[o] = '\0';
    *out_data = s_decode_buf;
    *out_len = o;
    return 0;
}

static uint32_t append_u32(char* out, uint32_t cap, uint32_t pos, uint32_t v)
{
    char tmp[12];
    uint32_t n = 0;
    if (pos >= cap) return pos;
    if (v == 0) {
        if (pos + 1u < cap) out[pos++] = '0';
        out[pos] = '\0';
        return pos;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0 && pos + 1u < cap) out[pos++] = tmp[--n];
    out[pos] = '\0';
    return pos;
}

void liveupdate_init(void)
{
    memset(&s_live, 0, sizeof(s_live));
    scopy(s_live.last_scope, sizeof(s_live.last_scope), "none");
    scopy(s_live.last_detail, sizeof(s_live.last_detail), "ready");
}

void liveupdate_info(liveupdate_info_t* out)
{
    if (out) *out = s_live;
}

int liveupdate_set_auto(int on)
{
    s_live.auto_enabled = on ? 1u : 0u;
    s_live.generation++;
    record_result("auto", "policy", on ? "auto apply enabled" : "auto apply disabled", 0);
    lardkit_journal_event("liveupdate", on ? "auto on" : "auto off");
    return 0;
}

int liveupdate_reload(const char* scope, char* out, uint32_t out_cap)
{
    uint32_t pos = 0;
    int r = 0;
    if (!scope || !scope[0]) scope = "all";
    if (out && out_cap) out[0] = '\0';

    if (streq_ci(scope, "file")) {
        record_result("", "file", "file bytes visible immediately", 0);
        return 0;
    }
    if (streq_ci(scope, "kmo") || streq_ci(scope, "all")) {
        uint32_t n = kmo_reload();
        s_live.reloads++;
        if (out && out_cap) {
            sappend(out, out_cap, "kmo=");
            pos = slen(out);
            pos = append_u32(out, out_cap, pos, n);
            (void)pos;
            sappend(out, out_cap, " ");
        }
    }
    if (streq_ci(scope, "sysrxe") || streq_ci(scope, "all")) {
        uint32_t n = sysrxe_reload();
        gui_reload_sysrxe_apps();
        s_live.reloads++;
        if (out && out_cap) {
            sappend(out, out_cap, "sysrxe=");
            pos = slen(out);
            pos = append_u32(out, out_cap, pos, n);
            (void)pos;
            sappend(out, out_cap, " gui ");
        }
    }
    if (streq_ci(scope, "rxe") || streq_ci(scope, "all")) {
        uint32_t n = rxe_reload();
        gui_reload_sysrxe_apps();
        s_live.reloads++;
        if (out && out_cap) {
            sappend(out, out_cap, "rxe=");
            pos = slen(out);
            pos = append_u32(out, out_cap, pos, n);
            (void)pos;
            sappend(out, out_cap, " gui ");
        }
    }
    if (streq_ci(scope, "drivers") || streq_ci(scope, "drfl") || streq_ci(scope, "all")) {
        drfl_load_all();
        s_live.reloads++;
        if (out && out_cap) sappend(out, out_cap, "drivers ");
    }
    if (streq_ci(scope, "fstwt") || streq_ci(scope, "fstwts")) {
        const FsFile* f = fs_open(s_live.last_file[0] ? s_live.last_file : "fstwt.fstwts");
        if (f && f->data && f->size) r = fstwt_load_script(f->data, f->size, f->name);
        else r = -3;
        if (out && out_cap) sappend(out, out_cap, r == 0 ? "fstwt " : "fstwt-fail ");
    }
    if (streq_ci(scope, "ltheme")) {
        const FsFile* f = fs_open(s_live.last_file);
        if (f && f->data && f->size) r = lardkit_theme_use_data(f->data, f->size);
        else r = -4;
        if (out && out_cap) sappend(out, out_cap, r == 0 ? "ltheme " : "ltheme-fail ");
    }
    if (streq_ci(scope, "lguilib")) {
        const FsFile* f = fs_open(s_live.last_file);
        if (f && f->data && f->size) r = lguilib_load_active(f->data, f->size);
        else r = -5;
        if (out && out_cap) sappend(out, out_cap, r == 0 ? "lguilib " : "lguilib-fail ");
    }
    if (streq_ci(scope, "wallpaper") || streq_ci(scope, "lwall")) {
        const FsFile* f = fs_open(s_live.last_file[0] ? s_live.last_file : "wallpaper.lardd");
        if (f && f->data && f->size) r = gui_wallpaper_load_config_file(f->name);
        else r = -6;
        if (out && out_cap) sappend(out, out_cap, r == 0 ? "wallpaper " : "wallpaper-fail ");
    }

    if (out && out_cap && out[0] == '\0') {
        scopy(out, out_cap, "unknown scope");
        r = -2;
    }
    if (r == 0) {
        s_live.generation++;
        record_result(s_live.last_file, scope, out && out[0] ? out : "reload complete", 0);
        lardkit_trace_event("liveupdate", scope, 0);
        lardkit_journal_event("liveupdate", scope);
    } else {
        record_result(s_live.last_file, scope, out && out[0] ? out : "reload failed", r);
    }
    return r;
}

int liveupdate_apply_text(const char* name, const char* text, uint32_t flags,
                          char* out, uint32_t out_cap)
{
    const char* data = text ? text : "";
    uint32_t len = slen(data);
    const FsFile* seed;
    FsWritableFile* w;
    int append = (flags & LIVEUPDATE_FLAG_APPEND) != 0;
    int had_writable;
    int r;
    const char* scope;
    char reload_msg[96];

    if (out && out_cap) out[0] = '\0';
    if (!name || !name[0]) {
        record_result("", "write", "missing target", -1);
        return -1;
    }
    if (flags & LIVEUPDATE_FLAG_DECODE) {
        r = decode_text(data, &data, &len);
        if (r != 0) {
            record_result(name, "write", "payload too large", r);
            return r;
        }
    }

    seed = fs_open_readonly(name);
    w = fs_open_writable(name);
    had_writable = w ? 1 : 0;
    w = fs_open_or_create_writable(name);
    if (!w) {
        record_result(name, "write", "target cannot become user-owned writable file", -3);
        return -3;
    }
    if (append && !had_writable && seed && seed->data && seed->size) {
        if (seed->size > w->cap) {
            record_result(name, "write", "seed/default too large for writable overlay", -4);
            return -4;
        }
        (void)fs_write(w, 0, seed->data, seed->size);
    }
    if (append) {
        if (w->size + len > w->cap) {
            record_result(name, "append", "not enough writable capacity", -5);
            return -5;
        }
        (void)fs_append(w, (const uint8_t*)data, len);
    } else {
        if (len > w->cap) {
            record_result(name, "write", "payload larger than writable capacity", -6);
            return -6;
        }
        (void)fs_write(w, 0, (const uint8_t*)data, len);
    }

    s_live.writes++;
    s_live.generation++;
    scope = auto_scope_for(name);
    record_result(name, scope, append ? "appended live bytes" : "replaced live bytes", 0);
    lardkit_trace_event("liveupdate", name, (int32_t)len);
    lardkit_journal_event("liveupdate", name);

    if (flags & LIVEUPDATE_FLAG_RELOAD) {
        reload_msg[0] = '\0';
        r = liveupdate_reload(scope, reload_msg, sizeof(reload_msg));
        if (out && out_cap) {
            snprintf(out, out_cap, "%s %u bytes; reload %s", append ? "appended" : "wrote", len, reload_msg);
        }
        return r;
    }
    if (out && out_cap) {
        snprintf(out, out_cap, "%s %u bytes to %s; scope=%s",
                 append ? "appended" : "wrote", len, name, scope);
    }
    return 0;
}

int liveupdate_apply_from_file(const char* src, const char* dst, uint32_t flags,
                               char* out, uint32_t out_cap)
{
    const FsFile* f = fs_open(src);
    if (!f || !f->data) {
        record_result(dst, "from", "source not found", -1);
        return -1;
    }
    if (f->size >= LIVEUPDATE_PAYLOAD_MAX) {
        record_result(dst, "from", "source too large for live apply buffer", -2);
        return -2;
    }
    memcpy(s_decode_buf, f->data, f->size);
    s_decode_buf[f->size] = '\0';
    flags &= ~LIVEUPDATE_FLAG_DECODE;
    return liveupdate_apply_text(dst, s_decode_buf, flags, out, out_cap);
}

int liveupdate_selftest(void)
{
    char out[128];
    const FsFile* f;
    if (liveupdate_apply_text("liveprobe.txt", "alpha", LIVEUPDATE_FLAG_DECODE, out, sizeof(out)) != 0) return -1;
    f = fs_open("liveprobe.txt");
    if (!f || f->size < 5 || memcmp(f->data, "alpha", 5) != 0) return -2;
    if (liveupdate_apply_text("liveprobe.kmo",
            "KMO 1\\nID liveprobe\\nCOMMAND liveprobe\\nTARGET boot\\nDEFAULT status\\nTEXT live one\\n",
            LIVEUPDATE_FLAG_DECODE | LIVEUPDATE_FLAG_RELOAD, out, sizeof(out)) != 0) return -3;
    if (!kmo_find("liveprobe", NULL)) return -4;
    if (liveupdate_apply_text("liveprobe.kmo",
            "KMO 1\\nID liveprobe\\nCOMMAND live2\\nTARGET boot\\nDEFAULT status\\nTEXT live two\\n",
            LIVEUPDATE_FLAG_DECODE | LIVEUPDATE_FLAG_RELOAD, out, sizeof(out)) != 0) return -5;
    if (!kmo_find_command("live2", NULL)) return -6;
    return 0;
}
