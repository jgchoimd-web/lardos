/*
 * LSH - Lard Shell
 * Drive X=default, Y=floppy, Z=RAM, A~W=extra.
 */
#include "lsh.h"
#include "fs.h"
#include "bmp.h"
#include "img_glyph.h"
#include "bosl_vm.h"
#include "lafillo_vm.h"
#include "os_vm.h"
#include "lardx_load.h"
#include "lafillo.h"
#include "lard_doc.h"
#include "lil.h"
#include "lar.h"
#include "lvcs.h"
#include "drfl.h"
#include "lcontainer.h"
#include "lpack.h"
#include "lardkit.h"
#include "gui.h"
#include "post.h"
#include "cpumode.h"
#include "screencheck.h"
#include "exgui.h"
#include "exexgui.h"
#include "lguilib.h"
#include "lassist.h"
#include "lardtime.h"
#include "oslink.h"
#include "taskprio.h"
#include "awake.h"
#include "bootprof.h"
#include "crashlog.h"
#include "version.h"
#include "io.h"
#include "pci.h"
#include "string.h"
#include <stddef.h>
#include <stdint.h>

#define LSH_MAGIC  0x0048534Cu  /* "LSH\0" LE */

static char s_drive = 'X';
static char s_output[LSH_MAX_OUTPUT];
static uint32_t s_out_len;

/* Environment variables: s_env_buf[i][0]=name, s_env_buf[i][1]=value */
static char s_env_buf[LSH_MAX_ENV][2][LSH_MAX_VAR_LEN];
static uint32_t s_nenv;

/* Pipe: buffer for cmd1 output, consumed by cmd2 */
static char s_pipe_buf[LSH_PIPE_BUF];
static uint32_t s_pipe_len;
static int s_redirect_to_pipe;  /* 1 = out_append goes to pipe */
static int s_pipe_has_input;    /* 1 = segment 2, stdin is s_pipe_buf */

/* SUM (Super User Mode): ring 0, full permissions, asm_ for hardware I/O */
static int s_in_sum_mode;

/* Sandbox: run LARDX with restricted syscalls (no file/LDLL/network) */
static int s_sandbox_mode;
static int s_cfgsh_mode;
static int s_magic_depth;

static void lsh_putc(char c, void* user);
static void parse_and_run(const char* cmd, const char* args);

static const char* env_get(const char* name)
{
    for (uint32_t i = 0; i < s_nenv; i++)
        if (strcmp(s_env_buf[i][0], name) == 0)
            return s_env_buf[i][1];
    return 0;
}

static int env_set(const char* name, const char* value)
{
    for (uint32_t i = 0; i < s_nenv; i++) {
        if (strcmp(s_env_buf[i][0], name) == 0) {
            uint32_t j = 0;
            while (value[j] && j + 1 < LSH_MAX_VAR_LEN) {
                s_env_buf[i][1][j] = value[j];
                j++;
            }
            s_env_buf[i][1][j] = '\0';
            return 0;
        }
    }
    if (s_nenv >= LSH_MAX_ENV) return -1;
    uint32_t ni = 0, vi = 0;
    while (name[ni] && ni + 1 < LSH_MAX_VAR_LEN) {
        s_env_buf[s_nenv][0][ni] = name[ni];
        ni++;
    }
    s_env_buf[s_nenv][0][ni] = '\0';
    while (value[vi] && vi + 1 < LSH_MAX_VAR_LEN) {
        s_env_buf[s_nenv][1][vi] = value[vi];
        vi++;
    }
    s_env_buf[s_nenv][1][vi] = '\0';
    s_nenv++;
    return 0;
}

/* Expand %VAR% in buf in-place. buf must have LSH_MAX_LINE capacity. */
static void expand_env(char* buf)
{
    char tmp[LSH_MAX_LINE];
    uint32_t ti = 0;
    const char* p = buf;
    while (*p && ti + 1 < LSH_MAX_LINE) {
        if (*p == '%') {
            p++;
            char var[LSH_MAX_VAR_LEN];
            uint32_t vi = 0;
            while (*p && *p != '%' && vi + 1 < LSH_MAX_VAR_LEN) var[vi++] = *p++;
            var[vi] = '\0';
            if (*p == '%') {
                p++;
                const char* val = env_get(var);
                if (val) {
                    while (*val && ti + 1 < LSH_MAX_LINE) tmp[ti++] = *val++;
                }
                continue;
            }
            tmp[ti++] = '%';
            for (uint32_t k = 0; k < vi && ti + 1 < LSH_MAX_LINE; k++) tmp[ti++] = var[k];
            continue;
        }
        tmp[ti++] = *p++;
    }
    tmp[ti] = '\0';
    uint32_t i = 0;
    while (tmp[i] && i < LSH_MAX_LINE - 1) { buf[i] = tmp[i]; i++; }
    buf[i] = '\0';
}

static void out_append(const char* s)
{
    if (!s) return;
    if (s_redirect_to_pipe) {
        while (*s && s_pipe_len + 1 < LSH_PIPE_BUF) s_pipe_buf[s_pipe_len++] = *s++;
        s_pipe_buf[s_pipe_len] = '\0';
    } else {
        while (*s && s_out_len + 1 < LSH_MAX_OUTPUT) s_output[s_out_len++] = *s++;
        s_output[s_out_len] = '\0';
    }
}

static void out_append_char(char c)
{
    if (s_redirect_to_pipe) {
        if (s_pipe_len + 1 < LSH_PIPE_BUF) {
            s_pipe_buf[s_pipe_len++] = c;
            s_pipe_buf[s_pipe_len] = '\0';
        }
    } else {
        if (s_out_len + 1 < LSH_MAX_OUTPUT) {
            s_output[s_out_len++] = c;
            s_output[s_out_len] = '\0';
        }
    }
}

static void out_append_u32(uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    if (v == 0) {
        out_append_char('0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) out_append_char(tmp[--n]);
}

static void out_append_u64(uint64_t v)
{
    char tmp[20];
    uint32_t n = 0;
    if (v == 0) {
        out_append_char('0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) out_append_char(tmp[--n]);
}

static void out_append_i32(int32_t v)
{
    if (v < 0) {
        out_append_char('-');
        out_append_u32((uint32_t)(-v));
    } else {
        out_append_u32((uint32_t)v);
    }
}

static void out_append_i64(int64_t v)
{
    if (v < 0) {
        out_append_char('-');
        out_append_u64((uint64_t)(-v));
    } else {
        out_append_u64((uint64_t)v);
    }
}

static void out_append_hex16(uint16_t v)
{
    static const char hex[] = "0123456789abcdef";
    out_append("0x");
    for (int i = 3; i >= 0; i--) {
        out_append_char(hex[(v >> (uint16_t)(i * 4)) & 0xFu]);
    }
}

static void out_append_hex32(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    out_append("0x");
    for (int i = 7; i >= 0; i--) {
        out_append_char(hex[(v >> (uint32_t)(i * 4)) & 0xFu]);
    }
}

static void out_append_hex64(uint64_t v)
{
    static const char hex[] = "0123456789abcdef";
    out_append("0x");
    for (int i = 15; i >= 0; i--) {
        out_append_char(hex[(v >> (uint32_t)(i * 4)) & 0xFu]);
    }
}

static void out_append_hex8(uint8_t v)
{
    static const char hex[] = "0123456789abcdef";
    out_append_char(hex[(v >> 4) & 0xFu]);
    out_append_char(hex[v & 0xFu]);
}

static void out_append_ip4(ip4_t ip)
{
    out_append_u32(ip.b[0]);
    out_append_char('.');
    out_append_u32(ip.b[1]);
    out_append_char('.');
    out_append_u32(ip.b[2]);
    out_append_char('.');
    out_append_u32(ip.b[3]);
}

static char lardos_version_suffix(void)
{
    uint32_t i = 0;
    while (LARDOS_VERSION[i]) i++;
    return i ? LARDOS_VERSION[i - 1] : '?';
}

static const char* lardos_version_channel(void)
{
    char suffix = lardos_version_suffix();
    if (suffix == 'a') return "official";
    if (suffix == 'b') return "beta-experimental";
    if (suffix == 'p') return "hotpatch";
    return "unknown";
}

typedef struct {
    const char* name;
    uint8_t magic_safe;
} magic_cmd_entry_t;

static const magic_cmd_entry_t s_magic_cmds[] = {
    { "help", 1 }, { "control", 1 }, { "status", 1 }, { "time", 1 }, { "date", 1 }, { "lardtime", 1 }, { "ltime", 1 }, { "lunar", 1 }, { "dangun", 1 }, { "release", 1 }, { "releases", 1 },
    { "ver", 1 }, { "post", 1 }, { "selftest", 1 }, { "mode", 1 }, { "cfgsh", 1 }, { "cfg", 1 }, { "settings", 1 }, { "exitcfg", 1 },
    { "buddy", 1 }, { "assistant", 1 }, { "lardbuddy", 1 },
    { "oslink", 1 }, { "oschat", 1 }, { "exgui", 1 }, { "exexgui", 1 }, { "lguilib", 1 }, { "ltheme", 1 }, { "glyph", 1 }, { "glyphs", 1 }, { "uglyph", 1 }, { "picglyph", 1 }, { "cursor", 1 }, { "ucursor", 1 }, { "awake", 1 }, { "awakening", 1 }, { "awakemon", 1 }, { "task", 1 }, { "tasks", 1 }, { "tasktop", 1 }, { "bootprof", 1 }, { "bootmap", 1 }, { "bootreplay", 1 }, { "postbaseline", 1 }, { "trace", 1 }, { "lardtrace", 1 }, { "netwatch", 1 }, { "devmap", 1 }, { "crashlog", 1 }, { "panicroom", 1 }, { "panic", 1 }, { "paniccapsule", 1 }, { "nice", 1 }, { "prio", 1 }, { "priority", 1 }, { "rollback", 1 }, { "trust", 1 }, { "bugeye", 1 }, { "bugreplay", 1 }, { "oldcheck", 1 }, { "lfsdoctor", 1 }, { "cfgprof", 1 }, { "userlaw", 1 }, { "journal", 1 }, { "larsview", 1 }, { "larsapp", 1 }, { "lunit", 1 }, { "larddnotes", 1 }, { "notes", 1 }, { "cls", 1 },
    { "dir", 1 }, { "type", 1 }, { "more", 1 }, { "lars", 1 }, { "lardd", 1 }, { "doc", 1 }, { "larsform", 1 }, { "larsact", 1 },
    { "lpack", 1 }, { "lpackls", 1 }, { "lpackinstall", 1 }, { "lpackverify", 1 }, { "lpackundo", 1 },
    { "copy", 1 }, { "cp", 1 }, { "write", 1 }, { "append", 1 }, { "set", 1 }, { "echo", 1 }, { "cd", 1 },
    { "lafillo", 1 }, { "larls", 1 }, { "larx", 1 }, { "larsh", 1 },
    { "bosl", 1 }, { "lil", 1 }, { "lafvm", 1 }, { "osvm", 1 }, { "run", 1 },
    { "lcnt", 1 }, { "container", 1 },
    { "vcs", 1 }, { "vcsinit", 1 }, { "vcsstatus", 1 }, { "vcsadd", 1 }, { "vcscommit", 1 },
    { "vcslog", 1 }, { "vcsshow", 1 },
    { "drivers", 1 }, { "fsstat", 1 }, { "fsload", 1 }, { "fssave", 1 }, { "sync", 1 },
    { "sram", 1 }, { "screenram", 1 }, { "screencheck", 1 }, { "scrcheck", 1 }, { "sandbox", 1 }, { "exitsandbox", 1 },
    { "sum", 0 }, { "exitsum", 0 }, { "peek", 0 }, { "poke", 0 }, { "asm_", 0 },
};

static uint32_t magic_strlen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static int magic_cmd_equals(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static uint32_t magic_min3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t m = a < b ? a : b;
    return m < c ? m : c;
}

static uint32_t magic_edit_distance(const char* a, const char* b)
{
    uint32_t la = magic_strlen(a);
    uint32_t lb = magic_strlen(b);
    uint32_t prev[33];
    uint32_t cur[33];
    if (la > 32u) la = 32u;
    if (lb > 32u) lb = 32u;
    for (uint32_t j = 0; j <= lb; j++) prev[j] = j;
    for (uint32_t i = 1; i <= la; i++) {
        cur[0] = i;
        for (uint32_t j = 1; j <= lb; j++) {
            uint32_t sub = prev[j - 1u] + (a[i - 1u] == b[j - 1u] ? 0u : 1u);
            uint32_t del = prev[j] + 1u;
            uint32_t ins = cur[j - 1u] + 1u;
            cur[j] = magic_min3(sub, del, ins);
        }
        for (uint32_t j = 0; j <= lb; j++) prev[j] = cur[j];
    }
    return prev[lb];
}

static uint32_t magic_threshold(uint32_t len)
{
    if (len <= 3u) return 1u;
    if (len <= 7u) return 2u;
    return 3u;
}

static const magic_cmd_entry_t* magic_find_exact(const char* cmd)
{
    uint32_t count = sizeof(s_magic_cmds) / sizeof(s_magic_cmds[0]);
    for (uint32_t i = 0; i < count; i++) {
        if (magic_cmd_equals(cmd, s_magic_cmds[i].name)) return &s_magic_cmds[i];
    }
    return NULL;
}

static const magic_cmd_entry_t* magic_predict(const char* cmd)
{
    const magic_cmd_entry_t* best = NULL;
    uint32_t best_score = 999u;
    uint32_t best_len = 0;
    uint32_t cmd_len = magic_strlen(cmd);
    uint32_t count = sizeof(s_magic_cmds) / sizeof(s_magic_cmds[0]);
    if (!cmd || !cmd[0]) return NULL;
    for (uint32_t i = 0; i < count; i++) {
        const magic_cmd_entry_t* c = &s_magic_cmds[i];
        if (!c->magic_safe) continue;
        uint32_t name_len = magic_strlen(c->name);
        uint32_t score = magic_edit_distance(cmd, c->name);
        if (cmd_len >= 2u && name_len >= cmd_len) {
            int prefix = 1;
            for (uint32_t j = 0; j < cmd_len; j++) {
                if (cmd[j] != c->name[j]) {
                    prefix = 0;
                    break;
                }
            }
            if (prefix && score > 0) score--;
        }
        if (score < best_score || (score == best_score && name_len > best_len)) {
            best = c;
            best_score = score;
            best_len = name_len;
        }
    }
    if (!best) return NULL;
    if (best_score <= magic_threshold(cmd_len > best_len ? cmd_len : best_len)) return best;
    return NULL;
}

static void cmd_magic(const char* args)
{
    char cmd[64];
    char rest[192];
    uint32_t i = 0;
    uint32_t ci = 0;
    uint32_t ri = 0;
    while (args[i] == ' ' || args[i] == '\t') i++;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && ci + 1u < sizeof(cmd)) cmd[ci++] = args[i++];
    cmd[ci] = '\0';
    while (args[i] && ri + 1u < sizeof(rest)) rest[ri++] = args[i++];
    rest[ri] = '\0';
    if (!cmd[0]) {
        out_append("Usage: magic command [args] | magic explain\n");
        return;
    }
    if (magic_cmd_equals(cmd, "explain")) {
        lardkit_magic_info_t info;
        lardkit_magic_info(&info);
        if (!info.has_record) {
            out_append("magic: no recorded prediction yet.\n");
            return;
        }
        out_append("magic explain\n");
        out_append("input=");
        out_append(info.input);
        out_append(" predicted=");
        out_append(info.predicted[0] ? info.predicted : "none");
        out_append(" executed=");
        out_append(info.executed ? "yes" : "no");
        out_append("\nreason=");
        out_append(info.reason);
        out_append("\n");
        return;
    }
    if (magic_cmd_equals(cmd, "dryrun")) {
        char dry[64];
        uint32_t di = 0;
        uint32_t rp = 0;
        while (rest[rp] == ' ' || rest[rp] == '\t') rp++;
        while (rest[rp] && rest[rp] != ' ' && rest[rp] != '\t' && di + 1u < sizeof(dry)) dry[di++] = rest[rp++];
        dry[di] = '\0';
        if (!dry[0]) {
            out_append("Usage: magic dryrun command [args]\n");
            return;
        }
        const magic_cmd_entry_t* exact_dry = magic_find_exact(dry);
        const magic_cmd_entry_t* pick_dry = exact_dry ? exact_dry : magic_predict(dry);
        if (!pick_dry || !pick_dry->magic_safe) {
            lardkit_magic_record(dry, pick_dry ? pick_dry->name : "", 0, "dryrun found no safe executable prediction");
            out_append("magic dryrun: would not execute ");
            out_append(dry);
            out_append("\n");
            return;
        }
        lardkit_magic_record(dry, pick_dry->name, 0,
                             exact_dry ? "dryrun exact safe command" : "dryrun edit-distance safe command prediction");
        out_append("magic dryrun: ");
        out_append(dry);
        if (!magic_cmd_equals(dry, pick_dry->name)) {
            out_append(" -> ");
            out_append(pick_dry->name);
        }
        out_append(" (not executed)\n");
        return;
    }
    const magic_cmd_entry_t* exact = magic_find_exact(cmd);
    const magic_cmd_entry_t* pick = exact ? exact : magic_predict(cmd);
    if (!pick) {
        lardkit_magic_record(cmd, "", 0, "no confident safe command match");
        out_append("magic: no confident command match for ");
        out_append(cmd);
        out_append("\n");
        return;
    }
    if (!pick->magic_safe) {
        lardkit_magic_record(cmd, pick->name, 0, "matched raw-control command; explicit run required");
        out_append("magic: ");
        out_append(pick->name);
        out_append(" is raw-control; run it explicitly.\n");
        return;
    }
    out_append("magic: ");
    out_append(cmd);
    if (!magic_cmd_equals(cmd, pick->name)) {
        out_append(" -> ");
        out_append(pick->name);
    }
    out_append("\n");
    lardkit_magic_record(cmd, pick->name, 1,
                         exact ? "exact safe command" : "edit-distance safe command prediction");
    s_magic_depth++;
    parse_and_run(pick->name, rest);
    s_magic_depth--;
}

void lsh_enter_sum_shortcut(void)
{
    if (!s_in_sum_mode) {
        s_in_sum_mode = 1;
        out_append("SUM (ring 0) enabled by Fn+0/F10. asm_ for hardware I/O. exitsum to leave.\n");
    } else {
        out_append("SUM already enabled.\n");
    }
}

static void dir_cb(const char* nm, uint32_t sz, void* u)
{
    (void)u;
    char ln[96];
    uint32_t i = 0;
    while (nm[i] && i < 48) { ln[i] = nm[i]; i++; }
    ln[i++] = ' ';
    ln[i++] = ' ';
    uint32_t s = sz;
    if (s == 0) { ln[i++] = '0'; }
    else {
        char tmp[16];
        uint32_t t = 0;
        while (s) { tmp[t++] = (char)('0' + (s % 10)); s /= 10; }
        while (t--) ln[i++] = tmp[t];
    }
    ln[i++] = '\n';
    ln[i] = '\0';
    out_append(ln);
}

static int drive_to_fs(char d)
{
    d = (char)((d >= 'a' && d <= 'z') ? d - 32 : d);
    if (d == 'X' || d == 'Y' || (d >= 'A' && d <= 'W')) return 0; /* main FS */
    if (d == 'Z') return 1; /* RAM */
    return -1;
}

static void resolve_path(const char* path, char* out_drive, char* out_name, uint32_t name_cap)
{
    char drv = s_drive;
    const char* p = path;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] && p[1] == ':') {
        drv = (char)((p[0] >= 'a' && p[0] <= 'z') ? p[0] - 32 : p[0]);
        p += 2;
        while (*p == '\\' || *p == '/') p++;
    }
    *out_drive = drv;
    uint32_t i = 0;
    while (*p && *p != ' ' && *p != '\t' && i + 1 < name_cap) out_name[i++] = *p++;
    out_name[i] = '\0';
}

static const FsFile* lsh_open_read(char drive, const char* name)
{
    if (drive_to_fs(drive) == 1) {
        if (name[0] == 'n' && name[1] == 'o' && name[2] == 't' && name[3] == 'e' &&
            name[4] == 's' && name[5] == '.' && name[6] == 't' && name[7] == 'x' && name[8] == 't' && name[9] == '\0') {
            return NULL; /* Z: - we need fs_open for ram, but fs_open handles notes.txt */
        }
    }
    return fs_open(name);
}

static void cmd_dir(const char* args)
{
    char drv = s_drive;
    if (args[0] == ' ' || args[0] == '\t') {
        while (*args == ' ' || *args == '\t') args++;
        if (args[0] && args[1] == ':') {
            drv = (char)((args[0] >= 'a' && args[0] <= 'z') ? args[0] - 32 : args[0]);
        }
    }
    char buf[128];
    uint32_t n = 0;
    buf[n++] = (char)drv;
    buf[n++] = ':';
    buf[n++] = '\\';
    buf[n++] = '\0';
    out_append(buf);
    out_append("\n");

    if (drive_to_fs(drv) == 0) {
        fs_list_readonly(dir_cb, NULL);
    } else if (drive_to_fs(drv) == 1) {
        fs_list_writable(dir_cb, NULL);
    } else {
        out_append("dir: bad drive.\n");
    }
}

static void cmd_type(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: type [drive:]file\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    if (!f) {
        FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
        if (w) {
            for (uint32_t i = 0; i < w->size && i < 1024; i++) out_append_char((char)w->data[i]);
            if (w->size > 0) out_append("\n");
        } else {
            out_append("File not found.\n");
        }
        return;
    }
    for (uint32_t i = 0; i < f->size && i < 1024; i++) out_append_char((char)f->data[i]);
    if (f->size > 0) out_append("\n");
}

static void cmd_lafillo(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: lafillo [drive:]file\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    const uint8_t* d = f ? f->data : (w ? w->data : NULL);
    uint32_t sz = f ? f->size : (w ? w->size : 0);
    if (!d || sz == 0 || sz >= 4096) {
        out_append("lafillo: file not found or too large\n");
        return;
    }
    static char in[4096], out[4096];
    for (uint32_t i = 0; i < sz; i++) in[i] = (char)d[i];
    in[sz] = '\0';
    if (lafillo_http_to_text(in, sz, out, sizeof(out)) != 0) {
        out_append("lafillo: conversion failed\n");
        return;
    }
    out_append(out);
    out_append("\n");
}

static void cmd_larddoc(const char* args, const char* usage)
{
    char drv;
    char name[64];
    const uint8_t* data = NULL;
    uint32_t size = 0;
    char out[2048];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append(usage);
        out_append("\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    if (f) {
        data = f->data;
        size = f->size;
    } else if (w) {
        data = w->data;
        size = w->size;
    }
    if (!data || size == 0 || size >= 4096) {
        out_append("doc: file not found or too large.\n");
        return;
    }
    if (lard_doc_to_text((const char*)data, size, out, sizeof(out)) != 0) {
        out_append("doc: not a LARS/LARDD document.\n");
        return;
    }
    out_append(out);
    out_append("\n");
}

static void cmd_release(const char* args)
{
    while (args && (*args == ' ' || *args == '\t')) args++;
    if (args && (strcmp(args, "policy") == 0 || strcmp(args, "channel") == 0 || strcmp(args, "channels") == 0)) {
        out_append("Release channel policy\n");
        out_append("  current: ");
        out_append(LARDOS_VERSION);
        out_append(" (");
        out_append(lardos_version_channel());
        out_append(")\n");
        out_append("  a = official: promoted stable builds after boot, POST, GUI, and media checks.\n");
        out_append("  b = beta-experimental: new or risky feature bundles before promotion.\n");
        out_append("  p = hotpatch: narrow emergency fixes for an existing release line.\n");
        out_append("  Do not use a just because a feature was added; choose the channel by risk.\n");
        return;
    }
    if (args && args[0]) {
        out_append("Usage: release [policy]\n");
        return;
    }
    cmd_larddoc("releases.lardd", "Usage: release");
}

static void lar_list_lsh_cb(const lar_entry_t* entry, void* user)
{
    (void)user;
    out_append("  ");
    for (uint32_t i = 0; i < entry->name_len; i++) out_append_char(entry->name[i]);
    out_append("  ");
    out_append_u32(entry->unpacked_size);
    out_append(entry->method == LAR_METHOD_STORE ? " bytes stored\n" : " bytes unsupported\n");
}

static void cmd_larls(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        uint32_t i = 0;
        const char* def = "bundle.lar";
        while (def[i] && i + 1 < sizeof(name)) { name[i] = def[i]; i++; }
        name[i] = '\0';
        drv = 'X';
    }

    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    const uint8_t* data = f ? f->data : (w ? w->data : NULL);
    uint32_t size = f ? f->size : (w ? w->size : 0);
    if (!data || size == 0) {
        out_append("larls: archive not found.\n");
        return;
    }

    out_append("LAR1 ");
    out_append(name);
    out_append("\n");
    int r = lar_list(data, size, lar_list_lsh_cb, NULL);
    if (r != 0) out_append("larls: invalid LAR archive.\n");
}

static void cmd_larx(const char* args)
{
    char archive_arg[64];
    char member[64];
    uint32_t ai = 0;
    uint32_t mi = 0;
    while (*args == ' ' || *args == '\t') args++;
    while (*args && *args != ' ' && *args != '\t' && ai + 1 < sizeof(archive_arg)) archive_arg[ai++] = *args++;
    archive_arg[ai] = '\0';
    while (*args == ' ' || *args == '\t') args++;
    while (*args && *args != ' ' && *args != '\t' && mi + 1 < sizeof(member)) member[mi++] = *args++;
    member[mi] = '\0';

    if (!archive_arg[0]) {
        const char* def_member = "hello.txt";
        while (def_member[mi] && mi + 1 < sizeof(member)) { member[mi] = def_member[mi]; mi++; }
        member[mi] = '\0';
    } else if (!member[0]) {
        uint32_t i = 0;
        while (archive_arg[i] && i + 1 < sizeof(member)) { member[i] = archive_arg[i]; i++; }
        member[i] = '\0';
        archive_arg[0] = '\0';
    }
    if (!archive_arg[0]) {
        ai = 0;
        const char* def_archive = "bundle.lar";
        while (def_archive[ai] && ai + 1 < sizeof(archive_arg)) { archive_arg[ai] = def_archive[ai]; ai++; }
        archive_arg[ai] = '\0';
    }

    char drv;
    char archive[64];
    resolve_path(archive_arg, &drv, archive, sizeof(archive));
    const FsFile* f = lsh_open_read(drv, archive);
    FsWritableFile* wsrc = (drive_to_fs(drv) == 1) ? fs_open_writable(archive) : NULL;
    const uint8_t* data = f ? f->data : (wsrc ? wsrc->data : NULL);
    uint32_t size = f ? f->size : (wsrc ? wsrc->size : 0);
    FsWritableFile* out = fs_open_writable("lar_extract.txt");
    if (!data || size == 0 || !out) {
        out_append("larx: storage not available.\n");
        return;
    }

    uint32_t out_len = out->cap > 0 ? out->cap - 1 : 0;
    int r = lar_extract(data, size, member, out->data, &out_len);
    if (r != 0) {
        out_append("larx: extract failed.\n");
        return;
    }
    out->size = out_len;
    out->data[out_len] = 0;
    fs_mark_dirty();
    out_append("Extracted ");
    out_append(member);
    out_append(" -> lar_extract.txt\n");
}

static int vcs_read_word(const char** args, char* out, uint32_t cap)
{
    uint32_t i = 0;
    const char* p = *args;
    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != ' ' && *p != '\t' && i + 1 < cap) out[i++] = *p++;
    out[i] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    *args = p;
    return i > 0 ? 0 : -1;
}

static int vcs_parse_u32(const char** args, uint32_t* out)
{
    uint32_t v = 0;
    const char* p = *args;
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    *args = p;
    *out = v;
    return 0;
}

static int lsh_parse_u64(const char** args, uint64_t* out)
{
    const char* p = *args;
    uint64_t v = 0;
    int any = 0;
    int hex = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        hex = 1;
        p += 2;
    }
    while (*p) {
        uint32_t d;
        uint64_t next;
        if (*p >= '0' && *p <= '9') d = (uint32_t)(*p - '0');
        else if (hex && *p >= 'a' && *p <= 'f') d = (uint32_t)(*p - 'a' + 10);
        else if (hex && *p >= 'A' && *p <= 'F') d = (uint32_t)(*p - 'A' + 10);
        else break;
        if (!hex && d > 9u) break;
        next = hex ? ((v << 4) | (uint64_t)d) : (v * 10u + (uint64_t)d);
        if (next < v) return -2;
        v = next;
        any = 1;
        p++;
    }
    if (!any) return -1;
    while (*p == ' ' || *p == '\t') p++;
    *args = p;
    *out = v;
    return 0;
}

static void vcs_file_lsh_cb(const lvcs_file_info_t* info, void* user)
{
    (void)user;
    out_append("  ");
    out_append(info->name);
    out_append(" ");
    out_append_u32(info->size);
    out_append(" bytes ");
    out_append_hex32(info->hash);
    out_append("\n");
}

static void vcs_log_lsh_cb(const lvcs_commit_info_t* info, void* user)
{
    (void)user;
    out_append("commit ");
    out_append_u32(info->id);
    if (info->parent) {
        out_append(" parent ");
        out_append_u32(info->parent);
    }
    out_append(" ");
    out_append_hex32(info->hash);
    out_append("\n  files ");
    out_append_u32(info->file_count);
    out_append("  ");
    out_append(info->message);
    out_append("\n");
}

static void cmd_vcsinit(const char* args)
{
    (void)args;
    lvcs_init();
    out_append("LVCS repo reset.\n");
}

static void cmd_vcsstatus(const char* args)
{
    uint32_t staged;
    uint32_t commits;
    uint32_t used;
    uint32_t cap;
    (void)args;
    lvcs_status(&staged, &commits, &used, &cap);
    out_append("LVCS: ");
    out_append_u32(commits);
    out_append(" commits, ");
    out_append_u32(staged);
    out_append(" staged, store ");
    out_append_u32(used);
    out_append("/");
    out_append_u32(cap);
    out_append("\n");
    if (staged) lvcs_stage(vcs_file_lsh_cb, NULL);
}

static void cmd_vcsadd(const char* args)
{
    char drv;
    char name[64];
    const FsFile* f;
    FsWritableFile* w;
    const uint8_t* data;
    uint32_t size;
    int r;

    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: vcsadd [drive:]file\n");
        return;
    }
    f = lsh_open_read(drv, name);
    w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    data = f ? f->data : (w ? w->data : NULL);
    size = f ? f->size : (w ? w->size : 0);
    if (!f && !w) {
        out_append("vcsadd: file not found.\n");
        return;
    }

    r = lvcs_add(name, data, size);
    if (r == -12) out_append("vcsadd: file too large for LVCS.\n");
    else if (r == -13) out_append("vcsadd: object store full.\n");
    else if (r == -20) out_append("vcsadd: stage full.\n");
    else if (r != 0) out_append("vcsadd: failed.\n");
    else {
        out_append("Staged ");
        out_append(name);
        out_append(" ");
        out_append_u32(size);
        out_append(" bytes ");
        out_append_hex32(lvcs_hash(data, size));
        out_append("\n");
    }
}

static void cmd_vcscommit(const char* args)
{
    uint32_t id = 0;
    int r;
    while (*args == ' ' || *args == '\t') args++;
    r = lvcs_commit(args, &id);
    if (r == -1) out_append("vcscommit: nothing staged.\n");
    else if (r == -2) out_append("vcscommit: commit limit reached.\n");
    else if (r != 0) out_append("vcscommit: failed.\n");
    else {
        out_append("Committed ");
        out_append_u32(id);
        out_append("\n");
    }
}

static void cmd_vcslog(const char* args)
{
    int n;
    (void)args;
    n = lvcs_log(vcs_log_lsh_cb, NULL);
    if (n == 0) out_append("No LVCS commits.\n");
}

static void cmd_vcsshow(const char* args)
{
    uint32_t id;
    char name[64];
    FsWritableFile* out;
    uint32_t out_len;
    int r;

    if (vcs_parse_u32(&args, &id) != 0) {
        out_append("Usage: vcsshow commit [file]\n");
        return;
    }
    if (vcs_read_word(&args, name, sizeof(name)) != 0) {
        r = lvcs_commit_files(id, vcs_file_lsh_cb, NULL);
        if (r < 0) out_append("vcsshow: commit not found.\n");
        return;
    }

    out = fs_open_writable("vcs_restore.txt");
    if (!out) {
        out_append("vcsshow: restore file missing.\n");
        return;
    }
    out_len = out->cap > 0 ? out->cap - 1 : 0;
    r = lvcs_checkout(id, name, out->data, &out_len);
    if (r == -1) out_append("vcsshow: commit not found.\n");
    else if (r == -3) out_append("vcsshow: output too small.\n");
    else if (r != 0) out_append("vcsshow: file not found in history.\n");
    else {
        out->size = out_len;
        out->data[out_len] = 0;
        fs_mark_dirty();
        out_append("Restored ");
        out_append(name);
        out_append(" from commit ");
        out_append_u32(id);
        out_append(" -> vcs_restore.txt\n");
        for (uint32_t i = 0; i < out_len && i < 1024; i++) out_append_char((char)out->data[i]);
        if (out_len > 0) out_append("\n");
    }
}

static void cmd_vcs(const char* args)
{
    char sub[16];
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        out_append("Usage: vcs init|status|add|commit|log|show\n");
        return;
    }
    if (strcmp(sub, "init") == 0) { cmd_vcsinit(args); return; }
    if (strcmp(sub, "status") == 0) { cmd_vcsstatus(args); return; }
    if (strcmp(sub, "add") == 0) { cmd_vcsadd(args); return; }
    if (strcmp(sub, "commit") == 0) { cmd_vcscommit(args); return; }
    if (strcmp(sub, "log") == 0) { cmd_vcslog(args); return; }
    if (strcmp(sub, "show") == 0) { cmd_vcsshow(args); return; }
    out_append("vcs: unknown subcommand.\n");
}

static void drivers_lsh_cb(uint16_t vendor_id, uint16_t device_id, uint8_t type,
                           const char* name, void* user)
{
    (void)user;
    out_append("  ");
    out_append(name);
    out_append("  ");
    if (type == DRFL_TYPE_NET) out_append("net");
    else if (type == DRFL_TYPE_BLOCK) out_append("block");
    else {
        out_append("type ");
        out_append_u32(type);
    }
    out_append("  pci ");
    out_append_hex16(vendor_id);
    out_append(":");
    out_append_hex16(device_id);
    out_append("\n");
}

static void cmd_drivers(const char* args)
{
    uint32_t available;
    uint32_t dirty;
    uint32_t lba;
    uint32_t sectors;
    int last;
    const char* driver;
    uint32_t count;
    (void)args;

    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    out_append("Storage: ");
    out_append(driver);
    out_append(available ? " online" : " offline");
    out_append(" LPST ");
    out_append_u32(lba);
    out_append("+");
    out_append_u32(sectors);
    out_append(dirty ? " dirty\n" : " clean\n");
    out_append("DRFL:\n");
    count = drfl_list(drivers_lsh_cb, NULL);
    if (count == 0) out_append("  none\n");
}

static void cmd_fsstat(const char* args)
{
    uint32_t available;
    uint32_t dirty;
    uint32_t lba;
    uint32_t sectors;
    uint32_t bank;
    uint32_t generation;
    uint32_t bank_sectors;
    int last;
    const char* driver;
    (void)args;

    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    fs_persist_detail(&bank, &generation, &bank_sectors);
    out_append("FS persist: ");
    out_append(available ? "available" : "offline");
    out_append(" driver=");
    out_append(driver);
    out_append(" lba=");
    out_append_u32(lba);
    out_append(" sectors=");
    out_append_u32(sectors);
    out_append(" dirty=");
    out_append_u32(dirty);
    out_append(" last=");
    out_append_i32(last);
    out_append(" bank=");
    if (bank == 0xFFFFFFFFu) out_append("none");
    else out_append_u32(bank);
    out_append(" gen=");
    out_append_u32(generation);
    out_append(" banksectors=");
    out_append_u32(bank_sectors);
    out_append("\n");
}

static void cmd_fssave(const char* args)
{
    int r;
    (void)args;
    r = fs_persist_save();
    if (r == 0) {
        out_append("FS saved to non-volatile LPST store.\n");
    } else {
        out_append("fssave: failed ");
        out_append_i32(r);
        out_append("\n");
    }
}

static void cmd_fsload(const char* args)
{
    int r;
    (void)args;
    r = fs_persist_load();
    if (r == 0) {
        out_append("FS loaded from non-volatile LPST store.\n");
    } else {
        out_append("fsload: failed ");
        out_append_i32(r);
        out_append("\n");
    }
}

static void selftest_emit(const char* status, const char* name, void* user)
{
    (void)user;
    out_append(status);
    out_append(" ");
    out_append(name);
    out_append("\n");
}

static void cmd_selftest(const char* args)
{
    lard_post_result_t post;
    (void)args;

    out_append("LardOS ");
    out_append(LARDOS_VERSION);
    out_append(" ");
    out_append(lardos_version_channel());
    out_append(" Power-On Self-Test\n");

    lard_post_run(selftest_emit, NULL, &post);

    out_append("POST: ");
    out_append_u32(post.pass);
    out_append(" passed, ");
    out_append_u32(post.fail);
    out_append(" failed");
    if (post.storage_available) {
        out_append(", storage online");
        out_append(post.storage_dirty ? ", dirty" : ", clean");
    } else {
        out_append(", storage offline");
    }
    out_append(", last=");
    out_append_i32(post.storage_last_result);
    out_append(", gen=");
    out_append_u32(post.storage_generation);
    out_append("\n");
    {
        lardkit_post_baseline_info_t bi;
        lardkit_post_baseline_info(&bi);
        out_append("POST baseline: previous=");
        out_append_u32(bi.previous_count);
        out_append(" current=");
        out_append_u32(bi.current_count);
        out_append(" changes=");
        out_append_u32(bi.changes);
        out_append(" regressions=");
        out_append_u32(bi.regressions);
        out_append("\n");
    }
}

static void cmd_ver(const char* args)
{
    (void)args;
    out_append("LardOS ");
    out_append(LARDOS_VERSION);
    out_append(" (");
    out_append(lardos_version_channel());
    out_append(")\n");
}

static void out_append_year5(uint32_t y)
{
    if (y < 10000u) {
        uint32_t div = 10000u;
        while (div > 1u) {
            out_append_char((char)('0' + ((y / div) % 10u)));
            div /= 10u;
        }
        out_append_char((char)('0' + (y % 10u)));
    } else {
        out_append_u32(y);
    }
}

static void out_append_2(uint32_t v)
{
    out_append_char((char)('0' + ((v / 10u) % 10u)));
    out_append_char((char)('0' + (v % 10u)));
}

static void lardtime_out_solar(const lardtime_civil_t* c)
{
    out_append_year5(c->year);
    out_append_char('-');
    out_append_2(c->month);
    out_append_char('-');
    out_append_2(c->day);
    out_append_char(' ');
    out_append_2(c->hour);
    out_append_char(':');
    out_append_2(c->minute);
    out_append_char(':');
    out_append_2(c->second);
}

static void lardtime_out_lunar(const lardtime_lunar_t* l)
{
    out_append_year5(l->year);
    out_append("-L");
    out_append_2(l->month);
    if (l->leap_month) out_append(" leap");
    out_append_char('-');
    out_append_2(l->day);
}

static void cmd_lardtime_mode(const char* args, const char* default_mode)
{
    char sub[16];
    const char* mode = default_mode;
    lardtime_snapshot_t now;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) == 0) mode = sub;
    if (strcmp(mode, "explain") == 0 || strcmp(mode, "policy") == 0) {
        out_append("LardOS Time uses ticks since 00000-01-01 00:00:00, not Unix epoch seconds.\n");
        out_append("Years print with at least five digits to keep year 10000 visible.\n");
        out_append("Calendar views include solar CE, Dangun year (CE+2333), and a native lunar estimate.\n");
        return;
    }
    if (lardtime_now(&now) != 0) {
        out_append("lardtime: RTC unavailable.\n");
        return;
    }
    if (strcmp(mode, "raw") == 0 || strcmp(mode, "ticks") == 0) {
        out_append_i64(now.ticks);
        out_append("\n");
        return;
    }
    if (strcmp(mode, "solar") == 0 || strcmp(mode, "date") == 0) {
        lardtime_out_solar(&now.civil);
        out_append("\n");
        return;
    }
    if (strcmp(mode, "dangun") == 0 || strcmp(mode, "dan") == 0) {
        out_append("Dangun ");
        out_append_year5(now.dangun_year);
        out_append("-");
        out_append_2(now.civil.month);
        out_append("-");
        out_append_2(now.civil.day);
        out_append("\n");
        return;
    }
    if (strcmp(mode, "lunar") == 0 || strcmp(mode, "moon") == 0) {
        lardtime_out_lunar(&now.lunar);
        out_append(" (Lard lunar)\n");
        return;
    }
    if (!(strcmp(mode, "now") == 0 || strcmp(mode, "status") == 0 || strcmp(mode, "all") == 0)) {
        out_append("Usage: time|lardtime [now|raw|solar|dangun|lunar|explain]\n");
        return;
    }
    out_append("LardOS Time ticks=");
    out_append_i64(now.ticks);
    out_append("\nSolar ");
    lardtime_out_solar(&now.civil);
    out_append("\nDangun ");
    out_append_year5(now.dangun_year);
    out_append("-");
    out_append_2(now.civil.month);
    out_append("-");
    out_append_2(now.civil.day);
    out_append("\nLunar ");
    lardtime_out_lunar(&now.lunar);
    out_append(" (Lard lunar)\nYear width: >=5, epoch: 00000-01-01, source: CMOS RTC -> LardOS Time\n");
}

static void cmd_help(const char* args)
{
    (void)args;
    out_append("Lard Shell commands\n");
    out_append("  help control status time date lunar dangun release [policy] ver post baseline selftest magic mode cfgsh cfgprof buddy bugeye bugreplay rollback trust lardtrace trace netwatch journal oslink oschat exgui exexgui lguilib ltheme glyph awake task bootprof bootmap bootreplay devmap crashlog panicroom cls\n");
    out_append("  dir [drive:]  type file  more  lars file  lardd file  larsform file\n");
    out_append("  lpack info|list|verify|checksum|install file.lpack; lpack undo last\n");
    out_append("  exgui on|off|style win|linux|mac|layout float|tile|stack|next\n");
    out_append("  exexgui on|off|focus gui|term|info|next|workspace 1|save 1|load 1|test\n");
    out_append("  cfgsh              enter settings shell: mode-name on|off or 1|2|3\n");
    out_append("  buddy on|off|joke|next|mood     optional easygoing helper overlay\n");
    out_append("  bugeye on|off|scan              visual bug monitor; writes bugreport.lardd\n");
    out_append("  bugreplay status|last|show|draw replay last BugEye screen-health frames\n");
    out_append("  rollback snap|last|apply        save/restore user-visible settings\n");
    out_append("  trust list|history|allow|deny   user-owned permission policy map\n");
    out_append("  post baseline, bootreplay show, bootmap, oldcheck, devmap boot/POST/device views\n");
    out_append("  lardtrace on|show|module gui, netwatch on|show, journal show\n");
    out_append("  lunit run tests.lunit, cfgprof save name/load name, userlaw show\n");
    out_append("  ltheme list|use name            native theme presets for the LardOS shell\n");
    out_append("  time|lardtime [raw|solar|dangun|lunar|explain]  LardOS Time, 5-digit years\n");
    out_append("  glyph demo|list|load|auto|show|move|copy|rename|pixel|live|click|insert|write  editable live PUA pictures\n");
    out_append("  cursor set U+E000|off           use a picture Unicode slot as the GUI cursor\n");
    out_append("  oschat say|send|read            local OSLink chat-style messages\n");
    out_append("  larsview open|reload|back|actions file  native LARS/LARDD browser state\n");
    out_append("  notes show|add|clear            syncs notes.lardd and GUI notes.txt\n");
    out_append("  lguilib status|show|use|test [file.lguilib]\n");
    out_append("  awake on|off|status|test\n");
    out_append("  write file text  append file text  copy src dst\n");
    out_append("  set NAME=value  echo text  cd drive:  X: Y: Z:\n");
    out_append("  lafillo file  larls archive  larx archive member  larsh file\n");
    out_append("  bosl file  lil file  lafvm file  osvm file  run file.bosx [args]\n");
    out_append("  lcnt list|create|rm|use|exit|run|info\n");
    out_append("  vcs init|status|add|commit|log|show\n");
    out_append("  drivers fsstat fsload fssave sync sram screencheck sandbox exitsandbox\n");
    out_append("  tasktop  task list|set|urgent|history|up|down|pause|resume|drop  nice prio cmd\n");
    out_append("  task priorities are 0..10; lev.10 is user-grantable urgent work\n");
    out_append("  bootprof status|set normal|safe|netoff|dev|awakening\n");
    out_append("  panicroom texture / panic capsule  draw real16 default texture or collect recovery state\n");
    out_append("  crashlog show|clear|test\n");
    out_append("  sum exitsum peek addr [len] poke addr value [8|16|32] asm_ ...\n");
    out_append("Tips: open file://lardos.lars in Doc, use Z: for RAM files, sync persists them.\n");
}

static void cmd_control(const char* args)
{
    (void)args;
    out_append("LardOS control surface\n");
    out_append("  Kernel and host tools are C. Runtime features stay in-tree.\n");
    out_append("  Files live in LFS, RAM files, LPST persistence, and embedded FS tables.\n");
    out_append("  Local docs use LARS; LARDD replaces Markdown for LardOS docs.\n");
    out_append("  Code runs through LSH, BOSL, LIL, GASM, LML, Lafillo VM, OSVM, and LARDX.\n");
    out_append("  The user owns the machine: SUM exposes raw I/O and memory controls.\n");
    out_append("  Release suffix: a=official, b=beta-experimental, p=hotpatch.\n");
    out_append("  Do not default to a: risky or broad feature bundles ship as b first.\n");
    out_append("  Each feature addition gets a version bump and releases.lardd entry.\n");
    out_append("\n");
    out_append("Start points:\n");
    out_append("  status              inspect version, drivers, storage, containers\n");
    out_append("  magic statsu        predict and execute the intended safe command\n");
    out_append("  magic dryrun statsu show what magic would execute without running it\n");
    out_append("  magic explain       show why magic executed or refused its last prediction\n");
    out_append("  time                show LardOS Time ticks, 5-digit CE year, Dangun year, and lunar view\n");
    out_append("  mode guard          guarded real16 <-> long64 roundtrip with restore check\n");
    out_append("  trace on            record shell/module/oslink/taskprio events in order\n");
    out_append("  netwatch on         watch readable packet/oslink/HTTP activity\n");
    out_append("  journal show        view automatic LARDD system journal\n");
    out_append("  bugeye scan         scan for visible bugs and write bugreport.lardd\n");
    out_append("  bugreplay draw      draw the last BugEye replay frames as a panel\n");
    out_append("  rollback snap       snapshot settings before experiments\n");
    out_append("  trust list          inspect user-controlled permission policy\n");
    out_append("  trust history       audit permission allow/deny changes\n");
    out_append("  oslink bus          inspect LardOS-internal OSLink messages\n");
    out_append("  oschat say hello    send a local OSLink chat-style message\n");
    out_append("  exgui style mac     enable familiar desktop/window chrome\n");
    out_append("  exexgui on          use sketch split: GUI, terminal, status\n");
    out_append("  cfgsh               enter mode-name value settings shell\n");
    out_append("  cfg style 2         set desktop style by number\n");
    out_append("  buddy on            enable the roaming casual assistant\n");
    out_append("  task list           inspect and reprioritize queued tasks\n");
    out_append("  priority history    show who granted priority lev.10 and when\n");
    out_append("  awake on            enable fast screen boot for next boot\n");
    out_append("  awake off           return next boot to normal and stop loader\n");
    out_append("  bootmap             show the boot structure as numbered phases\n");
    out_append("  bootreplay show     replay a detailed boot timeline from bootreplay.lardd\n");
    out_append("  devmap draw         draw PCI/storage/network device map\n");
    out_append("  oldcheck draw       draw the retro storage check map\n");
    out_append("  lfsdoctor scan      diagnose writable files and LPST persistence state\n");
    out_append("  panicroom texture   draw the real16 LPR default texture\n");
    out_append("  panic capsule       write a recovery capsule to paniccapsule.lardd\n");
    out_append("  ltheme preview default.ltheme draw a theme preview before applying\n");
    out_append("  glyph live U+E000 on            enable realtime hover/click rendering for a picture glyph\n");
    out_append("  glyph pixel U+E000 0 0 ff00ff  edit an assigned Unicode picture slot in-place\n");
    out_append("  cursor U+E000     assign the mouse cursor to a user-owned Unicode picture slot\n");
    out_append("  cfgprof save safe-ui save/load a bundle of settings\n");
    out_append("  userlaw show        inspect user-right policy principles\n");
    out_append("  lunit run tests.lunit run small OS feature tests\n");
    out_append("  notes add text      append to notes.lardd and GUI notes.txt\n");
    out_append("  crashlog show       inspect panic and diagnostic history\n");
    out_append("  lpack verify sample.lpack inspect package integrity before install\n");
    out_append("  lpack undo last     restore files changed by the last install\n");
    out_append("  sram on             use a quiet screen corner as scratch RAM\n");
    out_append("  screencheck retro   draw a retro boot/storage-style screen check\n");
    out_append("  write notes.txt ... edit the writable RAM filesystem\n");
    out_append("  vcs status          inspect the in-OS source/history layer\n");
    out_append("  lcnt info           inspect syscall-cap containers\n");
    out_append("  sum                 enter full-control ring-0 mode\n");
    out_append("  peek 0xb8000 32     read raw memory in SUM\n");
    out_append("  poke addr val 8     write raw memory in SUM\n");
}

static void cmd_status(const char* args)
{
    uint32_t available;
    uint32_t dirty;
    uint32_t lba;
    uint32_t sectors;
    uint32_t bank;
    uint32_t generation;
    uint32_t bank_sectors;
    uint32_t drivers;
    int last;
    const char* driver;
    lardtime_snapshot_t time_now;
    (void)args;

    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    fs_persist_detail(&bank, &generation, &bank_sectors);
    drivers = drfl_list(NULL, NULL);

    out_append("LardOS ");
    out_append(LARDOS_VERSION);
    out_append(" (");
    out_append(lardos_version_channel());
    out_append(")\n");
    if (lardtime_now(&time_now) == 0) {
        out_append("Time: ");
        lardtime_out_solar(&time_now.civil);
        out_append(", LT=");
        out_append_i64(time_now.ticks);
        out_append(", Dangun=");
        out_append_year5(time_now.dangun_year);
        out_append(", Lunar=");
        lardtime_out_lunar(&time_now.lunar);
        out_append("\n");
    }
    out_append("Drive: ");
    out_append_char(s_drive);
    out_append(":\\\n");
    out_append("Mode: ");
    if (s_cfgsh_mode) {
        out_append("settings shell\n");
    } else if (s_in_sum_mode) {
        out_append("SUM ring0\n");
    } else if (s_sandbox_mode) {
        out_append("sandbox\n");
    } else if (lcontainer_has_active()) {
        out_append("container ");
        out_append(lcontainer_active_name());
        out_append("\n");
    } else {
        out_append("normal\n");
    }
    out_append("LPST: ");
    out_append(available ? "online" : "offline");
    out_append(dirty ? ", dirty" : ", clean");
    out_append(", driver=");
    out_append(driver && driver[0] ? driver : "none");
    out_append(", last=");
    out_append_i32(last);
    out_append("\n");
    out_append("LPST layout: lba=");
    out_append_u32(lba);
    out_append(", sectors=");
    out_append_u32(sectors);
    out_append(", bank_sectors=");
    out_append_u32(bank_sectors);
    out_append(", active_bank=");
    if (bank == 0xFFFFFFFFu) out_append("none");
    else out_append_u32(bank);
    out_append(", gen=");
    out_append_u32(generation);
    out_append("\n");
    out_append("Drivers: ");
    out_append_u32(drivers);
    out_append(" DRFL entries\n");
    out_append("Containers: ");
    out_append_u32(lcontainer_count());
    if (lcontainer_has_active()) {
        out_append(", active=");
        out_append(lcontainer_active_name());
    }
    out_append("\n");

    cpu_mode_info_t mode;
    cpu_mode_info(&mode);
    out_append("CPU: ");
    out_append(cpu_mode_current_name());
    out_append(", bridge=");
    out_append(mode.bridge_ready ? "ready" : "offline");
    out_append(", trips=");
    out_append_u32(mode.roundtrip_count);
    out_append(", last=");
    out_append(mode.last_roundtrip_ok ? "ok" : "none");
    out_append("\n");

    gui_screenram_info_t sram;
    gui_screenram_info(&sram);
    out_append("ScreenRAM: ");
    out_append(sram.enabled ? "on" : "off");
    out_append(", cap=");
    out_append_u32(sram.capacity);
    out_append(", used=");
    out_append_u32(sram.used);
    out_append("\n");

    exgui_info_t xg;
    exgui_info(&xg);
    out_append("EXGUI: ");
    out_append(xg.enabled ? "on" : "off");
    out_append(", style=");
    out_append(exgui_style_name(xg.style));
    out_append(", layout=");
    out_append(exgui_layout_name(xg.layout));
    out_append("\n");

    exexgui_info_t xxg;
    exexgui_info(&xxg);
    out_append("EXEXGUI: ");
    out_append(xxg.enabled ? "on" : "off");
    out_append(", focus=");
    out_append(exexgui_focus_name(xxg.focus));
    out_append(", gui=");
    out_append_u32(xxg.layout.gui.w);
    out_append("x");
    out_append_u32(xxg.layout.gui.h);
    out_append(", term=");
    out_append_u32(xxg.layout.term.w);
    out_append("x");
    out_append_u32(xxg.layout.term.h);
    out_append("\n");

    lguilib_info_t lgui;
    lguilib_active(&lgui);
    out_append("LGUILIB: ");
    out_append(lgui.valid ? "active" : "inactive");
    out_append(", name=");
    out_append(lgui.theme.name);
    out_append(", widgets=");
    out_append_u32(lgui.theme.widget_count);
    out_append(", err=");
    out_append(lguilib_error_name(lgui.theme.last_error));
    out_append("\n");

    lassist_info_t buddy;
    lassist_info(&buddy);
    out_append("Lard Buddy: ");
    out_append(buddy.enabled ? "on" : "off");
    out_append(", mood=");
    out_append(lassist_mood_name(buddy.mood));
    out_append(", jokes=");
    out_append_u32(buddy.jokes);
    out_append(", msg=");
    out_append(buddy.message);
    out_append("\n");

    lardkit_bugeye_info_t be;
    lardkit_rollback_info_t rb;
    lardkit_theme_info_t th;
    lardkit_bugeye_info(&be);
    lardkit_rollback_info(&rb);
    lardkit_theme_info(&th);
    out_append("LardKit: bugeye=");
    out_append(be.enabled ? "on" : "off");
    out_append(", bugs=");
    out_append_u32(be.bug_count);
    out_append(", rollback=");
    out_append(rb.valid ? rb.label : "empty");
    out_append(", theme=");
    out_append(th.name);
    out_append(", panicroom=");
    out_append(lardkit_panicroom_active() ? "entered" : "standby");
    out_append("\n");

    oslink_info_t link;
    oslink_info(&link);
    out_append("OSLink: ");
    out_append(link.ready ? "ready" : "offline");
    out_append(", inbox=");
    out_append_u32(link.inbox_count);
    out_append(", peers=");
    out_append_u32(link.peer_count);
    out_append("\n");

    taskprio_info_t tasks;
    taskprio_info(&tasks);
    out_append("Tasks: queued=");
    out_append_u32(tasks.queued);
    out_append(", runnable=");
    out_append_u32(tasks.runnable);
    out_append(", paused=");
    out_append_u32(tasks.paused);
    out_append(", lev10=");
    out_append_u32(tasks.os_urgent);
    out_append(", default-prio=");
    out_append_i32(tasks.default_priority);
    out_append(", completed=");
    out_append_u32(tasks.completed);
    out_append("\n");

    bootprof_info_t bp;
    bootprof_info(&bp);
    out_append("BootProf: ");
    out_append(bp.name);
    out_append(", net=");
    out_append(bp.network ? "on" : "off");
    out_append(", post=");
    out_append(bp.force_post ? "on" : "off");
    out_append(", dev=");
    out_append(bp.dev_mode ? "on" : "off");
    out_append(", awake=");
    out_append(bp.awakening_mode ? "on" : "off");
    out_append("\n");

    awake_info_t ai;
    awake_info(&ai);
    out_append("Awakening: ");
    out_append(ai.enabled ? (ai.done ? "complete" : "loading") : "off");
    out_append(", phase=");
    out_append_u32(ai.phase);
    out_append("/");
    out_append_u32(ai.total);
    out_append(", current=");
    out_append(ai.current);
    out_append(", runs=");
    out_append_u32(ai.background_runs);
    out_append(", err=");
    out_append_u32(ai.last_error);
    out_append("\n");

    out_append("CrashLog: events=");
    out_append_u32(crashlog_count());
    out_append("\n");
}

static int args_word_is(const char* args, const char* word)
{
    uint32_t i = 0;
    if (!args || !word) return 0;
    while (args[i] == ' ' || args[i] == '\t') i++;
    uint32_t j = 0;
    while (word[j]) {
        if (args[i + j] != word[j]) return 0;
        j++;
    }
    char tail = args[i + j];
    return tail == '\0' || tail == ' ' || tail == '\t';
}

static int args_has_text(const char* args)
{
    uint32_t i = 0;
    if (!args) return 0;
    while (args[i] == ' ' || args[i] == '\t') i++;
    return args[i] != '\0';
}

static void cmd_mode(const char* args)
{
    cpu_mode_info_t info;
    if (!args) args = "";
    cpu_mode_info(&info);

    if (args_word_is(args, "guard")) {
        uint32_t before = info.roundtrip_count;
        out_append("mode guard: checking bridge before/after real16 window...\n");
        if (info.bridge_ready && cpu_mode_roundtrip_probe() == 0) {
            cpu_mode_info(&info);
            out_append("mode guard: OK, restored ");
            out_append(cpu_mode_current_name());
            out_append(" roundtrips=");
            out_append_u32(info.roundtrip_count - before);
            out_append("\n");
        } else {
            cpu_mode_info(&info);
            out_append("mode guard: blocked or restored previous long64 state, err=");
            out_append_u32(info.last_error);
            out_append("\n");
        }
        return;
    }

    if (args_word_is(args, "probe") || args_word_is(args, "real")) {
        out_append("mode: entering a controlled real16 window and returning to long64...\n");
        if (cpu_mode_roundtrip_probe() == 0) {
            out_append("mode: real16 -> long64 roundtrip OK\n");
        } else {
            cpu_mode_info(&info);
            out_append("mode: roundtrip failed, err=");
            out_append_u32(info.last_error);
            out_append("\n");
        }
        return;
    }

    if (args_has_text(args) && !args_word_is(args, "status")) {
        out_append("Usage: mode [status|probe|real|guard]\n");
        return;
    }

    out_append("CPU mode: ");
    out_append(cpu_mode_current_name());
    out_append("\nBridge: ");
    out_append(info.bridge_ready ? "ready" : "offline");
    out_append(" at ");
    out_append_hex32(info.trampoline_pa);
    out_append(", size=");
    out_append_u32(info.trampoline_size);
    out_append("\nRoundtrips: ");
    out_append_u32(info.roundtrip_count);
    out_append(", last=");
    out_append(info.last_roundtrip_ok ? "ok" : "none");
    out_append("\nPanicRoom real16 texture: ");
    out_append_u32(info.panicroom_texture_count);
    out_append(", last=");
    out_append(info.last_panicroom_texture_ok ? "ok" : "none");
    out_append(", err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_sram_status(void)
{
    gui_screenram_info_t info;
    gui_screenram_info(&info);
    out_append("ScreenRAM: ");
    out_append(info.enabled ? "on" : "off");
    out_append("\nrect ");
    out_append_u32(info.x);
    out_append(",");
    out_append_u32(info.y);
    out_append(" ");
    out_append_u32(info.w);
    out_append("x");
    out_append_u32(info.h);
    out_append(" cap=");
    out_append_u32(info.capacity);
    out_append("/");
    out_append_u32(info.max_capacity);
    out_append(" used=");
    out_append_u32(info.used);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_sram_read(const char* args)
{
    uint64_t off;
    uint64_t len;
    uint8_t buf[128];
    int r;
    if (lsh_parse_u64(&args, &off) != 0 || lsh_parse_u64(&args, &len) != 0 || len == 0) {
        out_append("Usage: sram read offset len\n");
        return;
    }
    if (len > sizeof(buf)) {
        out_append("sram: read capped to 128 bytes.\n");
        len = sizeof(buf);
    }
    r = gui_screenram_read((uint32_t)off, buf, (uint32_t)len);
    if (r < 0) {
        out_append("sram: read failed. Try sram on first.\n");
        return;
    }
    out_append("hex:");
    for (int i = 0; i < r; i++) {
        out_append_char(' ');
        out_append_hex8(buf[i]);
    }
    out_append("\ntext: ");
    for (int i = 0; i < r; i++) {
        char c = (char)buf[i];
        out_append_char((c >= 32 && c <= 126) ? c : '.');
    }
    out_append("\n");
}

static void cmd_sram_write(const char* args)
{
    uint64_t off;
    const char* text;
    uint32_t len = 0;
    int r;
    if (lsh_parse_u64(&args, &off) != 0) {
        out_append("Usage: sram write offset text\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    text = args;
    while (text[len]) len++;
    r = gui_screenram_write((uint32_t)off, (const uint8_t*)text, len);
    if (r < 0) {
        out_append("sram: write failed. Try sram on first.\n");
        return;
    }
    out_append("sram: wrote ");
    out_append_u32((uint32_t)r);
    out_append(" bytes\n");
}

static void cmd_sram(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        cmd_sram_status();
        return;
    }
    if (strcmp(sub, "status") == 0) {
        cmd_sram_status();
        return;
    }
    if (strcmp(sub, "on") == 0) {
        if (gui_screenram_enable(1) == 0) out_append("sram: enabled default quiet corner.\n");
        else out_append("sram: no framebuffer available.\n");
        return;
    }
    if (strcmp(sub, "off") == 0) {
        gui_screenram_enable(0);
        out_append("sram: disabled.\n");
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        gui_screenram_clear();
        out_append("sram: cleared.\n");
        return;
    }
    if (strcmp(sub, "test") == 0) {
        out_append(gui_screenram_selftest() == 0 ? "sram: selftest OK\n" : "sram: selftest failed\n");
        return;
    }
    if (strcmp(sub, "corner") == 0) {
        char corner[8];
        uint64_t w = 0;
        uint64_t h = 0;
        if (vcs_read_word(&args, corner, sizeof(corner)) != 0) {
            corner[0] = 'b'; corner[1] = 'r'; corner[2] = '\0';
        }
        (void)lsh_parse_u64(&args, &w);
        (void)lsh_parse_u64(&args, &h);
        if (gui_screenram_set_corner(corner, (uint32_t)w, (uint32_t)h) == 0) {
            out_append("sram: corner ");
            out_append(corner);
            out_append(" enabled.\n");
        } else {
            out_append("Usage: sram corner tl|tr|bl|br [w h]\n");
        }
        return;
    }
    if (strcmp(sub, "rect") == 0) {
        uint64_t x;
        uint64_t y;
        uint64_t w;
        uint64_t h;
        if (lsh_parse_u64(&args, &x) != 0 || lsh_parse_u64(&args, &y) != 0 ||
            lsh_parse_u64(&args, &w) != 0 || lsh_parse_u64(&args, &h) != 0) {
            out_append("Usage: sram rect x y w h\n");
            return;
        }
        if (gui_screenram_set_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h) == 0) {
            out_append("sram: selected rectangle enabled.\n");
        } else {
            out_append("sram: rectangle outside framebuffer.\n");
        }
        return;
    }
    if (strcmp(sub, "write") == 0) {
        cmd_sram_write(args);
        return;
    }
    if (strcmp(sub, "read") == 0) {
        cmd_sram_read(args);
        return;
    }
    out_append("Usage: sram status|on|off|corner|rect|write|read|clear|test\n");
}

static void screencheck_report(const screencheck_info_t* info)
{
    out_append("ScreenCheck ");
    out_append(info->bad_tiles == 0 ? "OK" : "CHECK");
    out_append("\nsize=");
    out_append_u32(info->width);
    out_append("x");
    out_append_u32(info->height);
    out_append(" changed=");
    out_append_u32(info->changed_samples);
    out_append(" tiles=");
    out_append_u32(info->tiles_checked);
    out_append(" bad=");
    out_append_u32(info->bad_tiles);
    out_append("\nwindow=");
    out_append(info->window_inside ? "inside" : "bad");
    out_append(" response=");
    out_append(info->response_view_ok ? "ok" : "bad");
    out_append(" err=");
    out_append_u32(info->last_error);
    out_append("\n");
}

static void cmd_screencheck(const char* args)
{
    char sub[16];
    screencheck_info_t info;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        if (screencheck_probe(&info) == 0) screencheck_report(&info);
        else out_append("screencheck: no framebuffer.\n");
        return;
    }
    if (strcmp(sub, "retro") == 0 || strcmp(sub, "run") == 0 || strcmp(sub, "draw") == 0) {
        screencheck_draw_retro();
        if (screencheck_probe(&info) == 0) screencheck_report(&info);
        else out_append("screencheck: no framebuffer.\n");
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(screencheck_selftest() == 0 ? "screencheck: selftest OK\n" : "screencheck: selftest failed\n");
        return;
    }
    out_append("Usage: screencheck status|retro|test\n");
}

static void cmd_exgui_status(void)
{
    exgui_info_t info;
    exgui_info(&info);
    out_append("EXGUI ");
    out_append(info.enabled ? "on" : "off");
    out_append(" style=");
    out_append(exgui_style_name(info.style));
    out_append(" layout=");
    out_append(exgui_layout_name(info.layout));
    out_append(" focused=");
    out_append_u32(info.focused);
    out_append("/");
    out_append_u32(info.window_count);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_exgui(const char* args)
{
    char sub[16];
    char value[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_exgui_status();
        return;
    }
    if (strcmp(sub, "on") == 0) {
        exgui_enable(1);
        out_append("exgui: enabled.\n");
        return;
    }
    if (strcmp(sub, "off") == 0) {
        exgui_enable(0);
        out_append("exgui: disabled; classic GUI remains active.\n");
        return;
    }
    if (strcmp(sub, "style") == 0 || strcmp(sub, "theme") == 0) {
        if (vcs_read_word(&args, value, sizeof(value)) != 0 || exgui_set_style(value) != 0) {
            out_append("Usage: exgui style win|linux|mac\n");
            return;
        }
        cmd_exgui_status();
        return;
    }
    if (strcmp(sub, "layout") == 0 || strcmp(sub, "wm") == 0) {
        if (vcs_read_word(&args, value, sizeof(value)) != 0 || exgui_set_layout(value) != 0) {
            out_append("Usage: exgui layout float|tile|stack\n");
            return;
        }
        cmd_exgui_status();
        return;
    }
    if (strcmp(sub, "next") == 0 || strcmp(sub, "focus") == 0) {
        exgui_focus_next();
        cmd_exgui_status();
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(exgui_selftest() == 0 ? "exgui: selftest OK\n" : "exgui: selftest failed\n");
        return;
    }
    out_append("Usage: exgui on|off|status|style|layout|next|test\n");
}

static void cmd_exexgui_status(void)
{
    exexgui_info_t info;
    exexgui_info(&info);
    out_append("EXEXGUI ");
    out_append(info.enabled ? "on" : "off");
    out_append(" focus=");
    out_append(exexgui_focus_name(info.focus));
    out_append(" workspace=");
    out_append_u32(info.workspace);
    out_append(" gui=");
    out_append_u32(info.layout.gui.w);
    out_append("x");
    out_append_u32(info.layout.gui.h);
    out_append(" term=");
    out_append_u32(info.layout.term.w);
    out_append("x");
    out_append_u32(info.layout.term.h);
    out_append(" info=");
    out_append_u32(info.layout.info.w);
    out_append("x");
    out_append_u32(info.layout.info.h);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_exexgui(const char* args)
{
    char sub[16];
    char value[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_exexgui_status();
        return;
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "apply") == 0 || strcmp(sub, "sketch") == 0) {
        exexgui_enable(1);
        out_append("exexgui: enabled sketch split layout.\n");
        cmd_exexgui_status();
        return;
    }
    if (strcmp(sub, "off") == 0) {
        exexgui_enable(0);
        out_append("exexgui: disabled; classic GUI and EXGUI stay available.\n");
        return;
    }
    if (strcmp(sub, "focus") == 0 || strcmp(sub, "pane") == 0) {
        if (vcs_read_word(&args, value, sizeof(value)) != 0 || exexgui_set_focus(value) != 0) {
            out_append("Usage: exexgui focus gui|term|info\n");
            return;
        }
        cmd_exexgui_status();
        return;
    }
    if (strcmp(sub, "next") == 0) {
        exexgui_focus_next();
        cmd_exexgui_status();
        return;
    }
    if (strcmp(sub, "workspace") == 0 || strcmp(sub, "ws") == 0) {
        uint64_t id;
        if (lsh_parse_u64(&args, &id) != 0 || exexgui_workspace_select((uint32_t)id) != 0) {
            out_append("Usage: exexgui workspace 1|2|3\n");
            return;
        }
        cmd_exexgui_status();
        return;
    }
    if (strcmp(sub, "save") == 0) {
        uint64_t id;
        if (lsh_parse_u64(&args, &id) != 0 || exexgui_workspace_save((uint32_t)id) != 0) {
            out_append("Usage: exexgui save 1|2|3\n");
            return;
        }
        out_append("exexgui: workspace saved.\n");
        return;
    }
    if (strcmp(sub, "load") == 0) {
        uint64_t id;
        if (lsh_parse_u64(&args, &id) != 0 || exexgui_workspace_load((uint32_t)id) != 0) {
            out_append("Usage: exexgui load 1|2|3\n");
            return;
        }
        cmd_exexgui_status();
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(exexgui_selftest() == 0 ? "exexgui: selftest OK\n" : "exexgui: selftest failed\n");
        return;
    }
    out_append("Usage: exexgui on|off|status|focus|next|workspace 1|save 1|load 1|test\n");
}

static int lsh_parse_ip4_arg(const char** args, ip4_t* out)
{
    const char* p = *args;
    uint32_t seg = 0;
    uint32_t val = 0;
    int have = 0;
    while (*p == ' ' || *p == '\t') p++;
    for (;;) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            val = val * 10u + (uint32_t)(c - '0');
            if (val > 255u) return -1;
            have = 1;
            p++;
        } else if (c == '.' || c == '\0' || c == ' ' || c == '\t') {
            if (!have || seg >= 4u) return -1;
            out->b[seg++] = (uint8_t)val;
            val = 0;
            have = 0;
            if (c == '.') {
                p++;
                continue;
            }
            break;
        } else {
            return -1;
        }
    }
    if (seg != 4u) return -1;
    while (*p == ' ' || *p == '\t') p++;
    *args = p;
    return 0;
}

static int lsh_doc_data_from_arg(const char* file_arg, const uint8_t** data, uint32_t* size)
{
    char drv;
    char name[64];
    const FsFile* f;
    FsWritableFile* w;
    resolve_path(file_arg, &drv, name, sizeof(name));
    if (!name[0]) return -1;
    f = lsh_open_read(drv, name);
    w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    if (f) {
        *data = f->data;
        *size = f->size;
        return 0;
    }
    if (w) {
        *data = w->data;
        *size = w->size;
        return 0;
    }
    return -2;
}

static void copy_lsh_literal(char* out, uint32_t cap, const char* s)
{
    uint32_t i = 0;
    if (!out || cap == 0) return;
    while (s && s[i] && i + 1u < cap) {
        out[i] = s[i];
        i++;
    }
    out[i] = '\0';
}

static void cmd_lguilib_print_theme(const char* label, const lguilib_theme_t* t)
{
    out_append(label);
    out_append(": ");
    out_append(t->name);
    out_append(" widgets=");
    out_append_u32(t->widget_count);
    out_append(" err=");
    out_append(lguilib_error_name(t->last_error));
    out_append("\n");
    out_append("  title_bg=");
    out_append_hex32(t->title_bg);
    out_append(" title_fg=");
    out_append_hex32(t->title_fg);
    out_append(" panel=");
    out_append_hex32(t->panel_bg);
    out_append(" border=");
    out_append_hex32(t->border);
    out_append("\n");
    out_append("  tab=");
    out_append_hex32(t->tab_active);
    out_append("/");
    out_append_hex32(t->tab_idle);
    out_append("/");
    out_append_hex32(t->tab_hover);
    out_append(" accent=");
    out_append_hex32(t->tab_accent);
    out_append("\n");
    out_append("  button=");
    out_append_hex32(t->button_border);
    out_append("/");
    out_append_hex32(t->button_hover);
    out_append(" output=");
    out_append_hex32(t->output_frame);
    out_append(" shadow=");
    out_append_hex32(t->shadow);
    out_append("\n");
}

static void cmd_lguilib_status(void)
{
    lguilib_info_t info;
    lguilib_active(&info);
    cmd_lguilib_print_theme(info.valid ? "LGUILIB active" : "LGUILIB inactive", &info.theme);
}

static void cmd_lguilib_file(const char* action, const char* args)
{
    char file_arg[64];
    const uint8_t* data;
    uint32_t size;
    lguilib_theme_t parsed;
    int r;

    if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0) {
        copy_lsh_literal(file_arg, sizeof(file_arg), "default.lguilib");
    }
    if (lsh_doc_data_from_arg(file_arg, &data, &size) != 0) {
        out_append("lguilib: file not found: ");
        out_append(file_arg);
        out_append("\n");
        return;
    }
    r = lguilib_parse(data, size, &parsed);
    if (r != 0) {
        out_append("lguilib: invalid ");
        out_append(file_arg);
        out_append(" err=");
        out_append(lguilib_error_name(parsed.last_error));
        out_append("\n");
        return;
    }
    if (strcmp(action, "use") == 0 || strcmp(action, "load") == 0) {
        if (lguilib_load_active(data, size) != 0) {
            out_append("lguilib: load failed.\n");
            return;
        }
        out_append("lguilib: active theme loaded from ");
        out_append(file_arg);
        out_append("\n");
        cmd_lguilib_status();
        return;
    }
    cmd_lguilib_print_theme(file_arg, &parsed);
}

static void cmd_lguilib(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_lguilib_status();
        return;
    }
    if (strcmp(sub, "show") == 0 || strcmp(sub, "cat") == 0 ||
        strcmp(sub, "use") == 0 || strcmp(sub, "load") == 0) {
        cmd_lguilib_file(sub, args);
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(lguilib_selftest() == 0 ? "lguilib: selftest OK\n" : "lguilib: selftest failed\n");
        return;
    }
    out_append("Usage: lguilib status|show|use|test [file.lguilib]\n");
}

static void cmd_larsform(const char* args)
{
    char file_arg[64];
    const uint8_t* data;
    uint32_t size;
    if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0 ||
        lsh_doc_data_from_arg(file_arg, &data, &size) != 0) {
        out_append("Usage: larsform [drive:]file.lars\n");
        return;
    }
    int count = lard_doc_action_count((const char*)data, size);
    if (count <= 0) {
        out_append("larsform: no LARS actions.\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        lard_doc_action_t a;
        if (lard_doc_action_at((const char*)data, size, (uint32_t)i, &a) == 0) {
            out_append_u32((uint32_t)i);
            out_append(" ");
            out_append(a.kind);
            out_append(" ");
            out_append(a.label);
            if (a.command[0]) {
                out_append(" -> ");
                out_append(a.command);
            }
            out_append("\n");
        }
    }
}

static void cmd_larsact(const char* args)
{
    char file_arg[64];
    uint32_t index;
    const uint8_t* data;
    uint32_t size;
    lard_doc_action_t a;
    if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0 ||
        vcs_parse_u32(&args, &index) != 0 ||
        lsh_doc_data_from_arg(file_arg, &data, &size) != 0) {
        out_append("Usage: larsact [drive:]file.lars index\n");
        return;
    }
    if (lard_doc_action_at((const char*)data, size, index, &a) != 0) {
        out_append("larsact: action not found.\n");
        return;
    }
    if (!a.command[0]) {
        out_append("larsact: input actions do not run commands.\n");
        return;
    }
    out_append("larsact: ");
    out_append(a.label);
    out_append(" -> ");
    out_append(a.command);
    out_append("\n");
    lsh_exec(a.command);
}

static void cmd_lpack_show(const char* file_arg, const uint8_t* data, uint32_t size, int verbose)
{
    int count = lpack_file_count(data, size);
    if (count < 0) {
        out_append("lpack: not an LPACK 1 file.\n");
        return;
    }
    out_append("LPACK ");
    out_append(file_arg);
    out_append(": ");
    out_append_u32((uint32_t)count);
    out_append(count == 1 ? " file\n" : " files\n");
    if (!verbose) return;
    for (int i = 0; i < count; i++) {
        lpack_file_info_t info;
        if (lpack_file_at(data, size, (uint32_t)i, &info) == 0) {
            out_append_u32((uint32_t)i);
            out_append(" ");
            out_append(info.name);
            out_append(" ");
            out_append_u32(info.size);
            out_append(" bytes\n");
        }
    }
}

static void cmd_lpack_op(const char* op, const char* args)
{
    char file_arg[64];
    const uint8_t* data;
    uint32_t size;
    if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0 ||
        lsh_doc_data_from_arg(file_arg, &data, &size) != 0) {
        out_append("Usage: lpack info|list|verify|checksum|install [drive:]file.lpack\n");
        return;
    }
    if (strcmp(op, "info") == 0) {
        cmd_lpack_show(file_arg, data, size, 0);
        return;
    }
    if (strcmp(op, "list") == 0 || strcmp(op, "ls") == 0) {
        cmd_lpack_show(file_arg, data, size, 1);
        return;
    }
    if (strcmp(op, "verify") == 0 || strcmp(op, "check") == 0 || strcmp(op, "checksum") == 0) {
        lpack_verify_info_t info;
        int r = lpack_verify(data, size, &info);
        out_append("lpack verify ");
        out_append(file_arg);
        out_append(r == 0 ? ": OK\n" : ": CHECK\n");
        out_append("files=");
        out_append_u32(info.files);
        out_append(" installable=");
        out_append_u32(info.installable);
        out_append(" bytes=");
        out_append_u32(info.total_bytes);
        out_append(" hash=");
        out_append_hex32(info.hash);
        out_append(" warnings=");
        out_append_u32(info.warnings);
        out_append(" errors=");
        out_append_u32(info.errors);
        out_append("\n");
        return;
    }
    if (strcmp(op, "install") == 0 || strcmp(op, "add") == 0) {
        int installed = lpack_install(data, size);
        if (installed < 0) {
            out_append("lpack: install failed; invalid package.\n");
            return;
        }
        out_append("lpack: installed ");
        out_append_u32((uint32_t)installed);
        out_append(installed == 1 ? " file.\n" : " files.\n");
        if (installed == 0) out_append("lpack: no writable package targets matched this system.\n");
        return;
    }
    out_append("Usage: lpack info|list|verify|install [drive:]file.lpack\n");
}

static void cmd_lpack(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        lpack_undo_info_t undo;
        lpack_undo_info(&undo);
        out_append("Usage: lpack info|list|verify|checksum|install file.lpack | lpack undo last | lpack test\n");
        out_append("undo=");
        out_append(undo.ready ? "ready" : "empty");
        out_append(" files=");
        out_append_u32(undo.files);
        out_append(" bytes=");
        out_append_u32(undo.bytes);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "undo") == 0 || strcmp(sub, "rollback") == 0) {
        int r = lpack_undo_last();
        if (r < 0) out_append("lpack: no install snapshot to undo.\n");
        else {
            out_append("lpack: restored ");
            out_append_u32((uint32_t)r);
            out_append(r == 1 ? " file.\n" : " files.\n");
        }
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(lpack_selftest() == 0 ? "lpack: selftest OK\n" : "lpack: selftest failed\n");
        return;
    }
    cmd_lpack_op(sub, args);
}

static const char* oslink_type_name(uint8_t type)
{
    if (type == 1) return "hello";
    if (type == 2) return "ping";
    if (type == 3) return "pong";
    if (type == 4) return "text";
    if (type == 5) return "ack";
    if (type == 6) return "exec";
    if (type == 7) return "local";
    return "packet";
}

static void cmd_oslink_status(void)
{
    oslink_info_t info;
    oslink_info(&info);
    out_append("OSLink ");
    out_append(info.ready ? "ready" : "offline");
    out_append(" node=");
    out_append(info.node);
    out_append(" ip=");
    out_append_ip4(info.ip);
    out_append(" port=");
    out_append_u32(info.port);
    out_append("\n");
    out_append("sent=");
    out_append_u32(info.sent);
    out_append(" local=");
    out_append_u32(info.local_sent);
    out_append(" recv=");
    out_append_u32(info.received);
    out_append(" dropped=");
    out_append_u32(info.dropped);
    out_append(" inbox=");
    out_append_u32(info.inbox_count);
    out_append(" local-inbox=");
    out_append_u32(info.local_count);
    out_append(" peers=");
    out_append_u32(info.peer_count);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_oslink_peers(void)
{
    uint32_t count = oslink_peer_count();
    if (count == 0) {
        out_append("oslink: no peers yet.\n");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        oslink_peer_t p;
        if (oslink_peer_at(i, &p) == 0) {
            out_append_u32(i);
            out_append(" ");
            out_append_ip4(p.ip);
            out_append(" ");
            out_append(p.node);
            out_append(" seen=");
            out_append_u32(p.seen);
            out_append("\n");
        }
    }
}

static void cmd_oslink_bus(void)
{
    oslink_info_t info;
    oslink_info(&info);
    out_append("OSLink local bus: queued=");
    out_append_u32(info.local_count);
    out_append(" emitted=");
    out_append_u32(info.local_sent);
    out_append(" total-inbox=");
    out_append_u32(info.inbox_count);
    out_append("\nChannels are lightweight labels carried inside the OSLink inbox.\n");
}

static void cmd_oslink_recv(void)
{
    oslink_msg_t m;
    oslink_poll();
    if (oslink_recv(&m) == 0) {
        out_append("oslink: inbox empty.\n");
        return;
    }
    out_append(oslink_type_name(m.type));
    if (m.type == 7) {
        out_append(" channel=");
        out_append(m.channel[0] ? m.channel : "main");
    } else {
        out_append(" from ");
        out_append(m.src_node);
        out_append(" ");
        out_append_ip4(m.src_ip);
    }
    out_append(" seq=");
    out_append_u32(m.seq);
    out_append("\n");
    if (m.text[0]) {
        out_append(m.text);
        out_append("\n");
    }
}

static void cmd_oslink_emit(const char* args)
{
    char channel[OSLINK_CHANNEL_MAX + 1u];
    if (vcs_read_word(&args, channel, sizeof(channel)) != 0) {
        out_append("Usage: oslink emit channel text\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (oslink_emit_local(channel, args) == 0) {
        out_append("oslink: local ");
        out_append(channel);
        out_append(" emitted.\n");
    } else {
        out_append("oslink: emit failed.\n");
    }
}

static void cmd_oslink_send_like(const char* args, int kind)
{
    ip4_t dst;
    int r;
    if (lsh_parse_ip4_arg(&args, &dst) != 0) {
        out_append(kind == 1 ? "Usage: oslink hello ip\n" :
                   kind == 2 ? "Usage: oslink ping ip [text]\n" :
                   kind == 3 ? "Usage: oslink send ip text\n" :
                               "Usage: oslink exec ip command\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (kind == 1) r = oslink_send_hello(dst);
    else if (kind == 2) r = oslink_send_ping(dst, args);
    else if (kind == 3) r = oslink_send_text(dst, args);
    else r = oslink_send_exec(dst, args);
    if (r == 0) {
        out_append("oslink: sent to ");
        out_append_ip4(dst);
        out_append("\n");
    } else {
        out_append("oslink: send failed.\n");
    }
}

static void cmd_oslink(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        cmd_oslink_status();
        return;
    }
    if (strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_oslink_status();
        return;
    }
    if (strcmp(sub, "peers") == 0) {
        cmd_oslink_peers();
        return;
    }
    if (strcmp(sub, "bus") == 0 || strcmp(sub, "local") == 0) {
        cmd_oslink_bus();
        return;
    }
    if (strcmp(sub, "emit") == 0 || strcmp(sub, "pub") == 0) {
        cmd_oslink_emit(args);
        return;
    }
    if (strcmp(sub, "poll") == 0) {
        oslink_poll();
        out_append("oslink: polled.\n");
        return;
    }
    if (strcmp(sub, "recv") == 0 || strcmp(sub, "inbox") == 0) {
        cmd_oslink_recv();
        return;
    }
    if (strcmp(sub, "hello") == 0) {
        cmd_oslink_send_like(args, 1);
        return;
    }
    if (strcmp(sub, "ping") == 0) {
        cmd_oslink_send_like(args, 2);
        return;
    }
    if (strcmp(sub, "send") == 0 || strcmp(sub, "msg") == 0) {
        cmd_oslink_send_like(args, 3);
        return;
    }
    if (strcmp(sub, "exec") == 0 || strcmp(sub, "run") == 0) {
        cmd_oslink_send_like(args, 4);
        return;
    }
    if (strcmp(sub, "test") == 0) {
        out_append(oslink_selftest() == 0 ? "oslink: selftest OK\n" : "oslink: selftest failed\n");
        return;
    }
    out_append("Usage: oslink status|bus|emit|hello|ping|send|exec|recv|peers|poll|test\n");
}

static void cmd_bugeye_status(void)
{
    lardkit_bugeye_info_t info;
    lardkit_bugeye_info(&info);
    out_append("BugEye ");
    out_append(info.enabled ? "on" : "off");
    out_append(" scans=");
    out_append_u32(info.scans);
    out_append(" bugs=");
    out_append_u32(info.bug_count);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
    if (info.scans) screencheck_report(&info.screen);
}

static void cmd_bugeye(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_bugeye_status();
        return;
    }
    if (strcmp(sub, "on") == 0) {
        lardkit_bugeye_enable(1);
        (void)lardkit_bugeye_scan();
        cmd_bugeye_status();
        return;
    }
    if (strcmp(sub, "off") == 0) {
        lardkit_bugeye_enable(0);
        cmd_bugeye_status();
        return;
    }
    if (strcmp(sub, "scan") == 0 || strcmp(sub, "run") == 0) {
        int r = lardkit_bugeye_scan();
        out_append(r == 0 ? "bugeye: no visible layout bugs found.\n" : "bugeye: check report.\n");
        out_append("bugeye: wrote bugreport.lardd\n");
        cmd_bugeye_status();
        return;
    }
    if (strcmp(sub, "test") == 0) {
        out_append(lardkit_bugeye_scan() >= 0 ? "bugeye: selftest OK\n" : "bugeye: selftest failed\n");
        return;
    }
    out_append("Usage: bugeye on|off|status|scan|test\n");
}

static void cmd_bugreplay_list(void)
{
    uint32_t count = lardkit_bugreplay_count();
    out_append("BugReplay frames=");
    out_append_u32(count);
    out_append("\n");
    if (count == 0) {
        out_append("bugreplay: no frames. Run bugeye scan first.\n");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        lardkit_bugreplay_frame_t f;
        if (lardkit_bugreplay_at(i, &f) != 0) continue;
        out_append("#");
        out_append_u32(f.seq);
        out_append(" scan=");
        out_append_u32(f.scan);
        out_append(" ");
        out_append_u32(f.width);
        out_append("x");
        out_append_u32(f.height);
        out_append(" changed=");
        out_append_u32(f.changed_samples);
        out_append(" bad=");
        out_append_u32(f.bad_tiles);
        out_append(" err=");
        out_append_u32(f.last_error);
        out_append("\n");
    }
}

static void cmd_bugreplay(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) {
        cmd_bugreplay_list();
        return;
    }
    if (strcmp(sub, "last") == 0) {
        uint32_t count = lardkit_bugreplay_count();
        lardkit_bugreplay_frame_t f;
        if (count == 0 || lardkit_bugreplay_at(count - 1u, &f) != 0) {
            out_append("bugreplay: no frames.\n");
            return;
        }
        out_append("last seq=");
        out_append_u32(f.seq);
        out_append(" scan=");
        out_append_u32(f.scan);
        out_append(" bad=");
        out_append_u32(f.bad_tiles);
        out_append(" err=");
        out_append_u32(f.last_error);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "write") == 0 || strcmp(sub, "save") == 0) {
        out_append(lardkit_bugreplay_write() == 0 ? "bugreplay: wrote bugreplay.lardd\n" : "bugreplay: write failed\n");
        return;
    }
    if (strcmp(sub, "show") == 0 || strcmp(sub, "doc") == 0) {
        (void)lardkit_bugreplay_write();
        cmd_larddoc("bugreplay.lardd", "Usage: bugreplay show");
        return;
    }
    if (strcmp(sub, "draw") == 0) {
        out_append(lardkit_bugreplay_draw() == 0 ? "bugreplay: drew frame log panel.\n" : "bugreplay: draw failed.\n");
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        lardkit_bugreplay_clear();
        out_append("bugreplay: cleared.\n");
        return;
    }
    out_append("Usage: bugreplay status|last|show|draw|write|clear\n");
}

static void cmd_rollback_status(void)
{
    lardkit_rollback_info_t info;
    lardkit_rollback_info(&info);
    out_append("Rollback ");
    out_append(info.valid ? "ready" : "empty");
    out_append(" label=");
    out_append(info.label);
    out_append(" snaps=");
    out_append_u32(info.snapshots);
    out_append(" applied=");
    out_append_u32(info.applied);
    out_append("\n");
    if (info.valid) {
        out_append("exgui=");
        out_append_u32(info.exgui_enabled);
        out_append(" style=");
        out_append_u32(info.exgui_style);
        out_append(" layout=");
        out_append_u32(info.exgui_layout);
        out_append(" split=");
        out_append_u32(info.exexgui_enabled);
        out_append(" buddy=");
        out_append_u32(info.buddy_enabled);
        out_append(" http=");
        out_append(info.http_post ? "POST" : "GET");
        out_append(" boot=");
        out_append(info.boot_profile);
        out_append(" prio=");
        out_append_i32(info.task_default);
        out_append("\n");
    }
}

static void cmd_rollback(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_rollback_status();
        return;
    }
    if (strcmp(sub, "snap") == 0 || strcmp(sub, "snapshot") == 0 || strcmp(sub, "save") == 0) {
        char label[32];
        if (vcs_read_word(&args, label, sizeof(label)) != 0) label[0] = '\0';
        (void)lardkit_snapshot(label[0] ? label : "manual");
        out_append("rollback: snapshot saved.\n");
        cmd_rollback_status();
        return;
    }
    if (strcmp(sub, "apply") == 0 || strcmp(sub, "last") == 0 || strcmp(sub, "restore") == 0) {
        if (lardkit_rollback_apply() == 0) out_append("rollback: restored last snapshot.\n");
        else out_append("rollback: no snapshot.\n");
        cmd_rollback_status();
        return;
    }
    out_append("Usage: rollback status|snap [label]|apply\n");
}

static uint32_t trust_cap_from_name(const char* name)
{
    if (strcmp(name, "fs") == 0 || strcmp(name, "file") == 0) return LARDKIT_TRUST_FS;
    if (strcmp(name, "screen") == 0 || strcmp(name, "gui") == 0) return LARDKIT_TRUST_SCREEN;
    if (strcmp(name, "net") == 0 || strcmp(name, "network") == 0) return LARDKIT_TRUST_NET;
    if (strcmp(name, "oslink") == 0 || strcmp(name, "bus") == 0) return LARDKIT_TRUST_OSLINK;
    if (strcmp(name, "raw") == 0 || strcmp(name, "sum") == 0) return LARDKIT_TRUST_RAW;
    return 0;
}

static void trust_print_caps(uint32_t caps)
{
    int any = 0;
    if (caps & LARDKIT_TRUST_FS) { out_append("fs"); any = 1; }
    if (caps & LARDKIT_TRUST_SCREEN) { out_append(any ? ",screen" : "screen"); any = 1; }
    if (caps & LARDKIT_TRUST_NET) { out_append(any ? ",net" : "net"); any = 1; }
    if (caps & LARDKIT_TRUST_OSLINK) { out_append(any ? ",oslink" : "oslink"); any = 1; }
    if (caps & LARDKIT_TRUST_RAW) { out_append(any ? ",raw" : "raw"); any = 1; }
    if (!any) out_append("none");
}

static const char* trust_cap_name(uint32_t cap)
{
    if (cap == LARDKIT_TRUST_FS) return "fs";
    if (cap == LARDKIT_TRUST_SCREEN) return "screen";
    if (cap == LARDKIT_TRUST_NET) return "net";
    if (cap == LARDKIT_TRUST_OSLINK) return "oslink";
    if (cap == LARDKIT_TRUST_RAW) return "raw";
    return "unknown";
}

static void cmd_trust_list(void)
{
    uint32_t count = lardkit_trust_count();
    out_append("Trust policy\n");
    for (uint32_t i = 0; i < count; i++) {
        lardkit_trust_entry_t e;
        if (lardkit_trust_at(i, &e) == 0) {
            out_append("  ");
            out_append(e.subject);
            out_append(": ");
            trust_print_caps(e.caps);
            out_append("\n");
        }
    }
}

static void cmd_trust_history(const char* args)
{
    char sub[16];
    uint32_t count;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) == 0 &&
        (strcmp(sub, "clear") == 0 || strcmp(sub, "reset") == 0)) {
        lardkit_trust_history_clear();
        out_append("trust history: cleared.\n");
        return;
    }
    count = lardkit_trust_history_count();
    out_append("Trust history count=");
    out_append_u32(count);
    out_append("\n");
    if (count == 0) {
        out_append("trust history: no permission changes yet.\n");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        lardkit_trust_history_entry_t e;
        if (lardkit_trust_history_at(i, &e) != 0) continue;
        out_append("#");
        out_append_u32(e.seq);
        out_append(" ");
        out_append(e.allowed ? "allow " : "deny ");
        out_append(e.subject);
        out_append(" ");
        out_append(trust_cap_name(e.cap));
        out_append(" -> ");
        trust_print_caps(e.caps_after);
        out_append("\n");
    }
}

static void cmd_trust(const char* args)
{
    char sub[16];
    char subject[32];
    char capname[16];
    uint32_t cap;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "list") == 0 || strcmp(sub, "status") == 0) {
        cmd_trust_list();
        return;
    }
    if (strcmp(sub, "caps") == 0) {
        out_append("trust caps: fs screen net oslink raw\n");
        return;
    }
    if (strcmp(sub, "history") == 0 || strcmp(sub, "hist") == 0 || strcmp(sub, "audit") == 0) {
        cmd_trust_history(args);
        return;
    }
    if (strcmp(sub, "allow") == 0 || strcmp(sub, "deny") == 0) {
        if (vcs_read_word(&args, subject, sizeof(subject)) != 0 ||
            vcs_read_word(&args, capname, sizeof(capname)) != 0) {
            out_append("Usage: trust allow|deny subject fs|screen|net|oslink|raw\n");
            return;
        }
        cap = trust_cap_from_name(capname);
        if (!cap || lardkit_trust_set(subject, cap, strcmp(sub, "allow") == 0) != 0) {
            out_append("trust: unknown subject or cap.\n");
            return;
        }
        cmd_trust_list();
        return;
    }
    out_append("Usage: trust list|caps|history|allow|deny\n");
}

static void cmd_bootmap(const char* args)
{
    (void)args;
    bootprof_info_t bp;
    awake_info_t aw;
    bootprof_info(&bp);
    awake_info(&aw);
    out_append("BootMap profile=");
    out_append(bp.name);
    out_append(" awake=");
    out_append(aw.enabled ? aw.current : "off");
    out_append("\n");
    for (uint32_t i = 0; i < lardkit_bootmap_count(); i++) {
        out_append("  ");
        out_append_u32(i);
        out_append(" ");
        out_append(lardkit_bootmap_phase(i));
        if (aw.enabled && i == 8u + aw.phase) out_append("  <- background");
        out_append("\n");
    }
}

static void cmd_panicroom(const char* args)
{
    char sub[16];
    cpu_mode_info_t mode_info;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cpu_mode_info(&mode_info);
        out_append("PanicRoom ");
        out_append(lardkit_panicroom_active() ? "entered" : "standby");
        out_append(" entries=");
        out_append_u32(lardkit_panicroom_entries());
        out_append(" real16-textures=");
        out_append_u32(mode_info.panicroom_texture_count);
        out_append(" last=");
        out_append(mode_info.last_panicroom_texture_ok ? "ok" : "none");
        out_append("\nRuntime panic opens a real16-backed texture screen; use crashlog show, rollback apply, or panicroom capsule from LSH.\n");
        return;
    }
    if (strcmp(sub, "enter") == 0 || strcmp(sub, "on") == 0) {
        lardkit_panicroom_enter();
        out_append("panicroom: recovery shell armed.\n");
        return;
    }
    if (strcmp(sub, "exit") == 0 || strcmp(sub, "off") == 0) {
        lardkit_panicroom_exit();
        out_append("panicroom: standby.\n");
        return;
    }
    if (strcmp(sub, "capsule") == 0 || strcmp(sub, "report") == 0) {
        if (lardkit_panic_capsule_write() == 0) {
            out_append("panicroom: wrote paniccapsule.lardd\n");
            cmd_larddoc("paniccapsule.lardd", "Usage: panicroom capsule");
        } else {
            out_append("panicroom: capsule failed.\n");
        }
        return;
    }
    if (strcmp(sub, "texture") == 0 || strcmp(sub, "real") == 0 || strcmp(sub, "real16") == 0) {
        if (cpu_mode_panicroom_texture() == 0) out_append("panicroom: real16 default texture drawn.\n");
        else {
            cpu_mode_info(&mode_info);
            out_append("panicroom: real16 texture failed, err=");
            out_append_u32(mode_info.last_error);
            out_append("\n");
        }
        return;
    }
    out_append("Usage: panicroom status|enter|exit|capsule|texture\n");
}

static void cmd_paniccapsule(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "make") == 0 || strcmp(sub, "write") == 0 || strcmp(sub, "capture") == 0) {
        if (lardkit_panic_capsule_write() == 0) out_append("paniccapsule: wrote paniccapsule.lardd\n");
        else out_append("paniccapsule: write failed.\n");
        return;
    }
    if (strcmp(sub, "show") == 0 || strcmp(sub, "doc") == 0) {
        (void)lardkit_panic_capsule_write();
        cmd_larddoc("paniccapsule.lardd", "Usage: paniccapsule show");
        return;
    }
    out_append("Usage: paniccapsule make|show\n");
}

static void cmd_oldcheck_status(void)
{
    lardkit_oldcheck_info_t info;
    lardkit_oldcheck_info(&info);
    out_append("OldCheck files=");
    out_append_u32(info.count);
    out_append(" storage=");
    out_append(info.available ? "online" : "offline");
    out_append(" dirty=");
    out_append_u32(info.dirty);
    out_append(" gen=");
    out_append_u32(info.generation);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_oldcheck(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        (void)lardkit_oldcheck_run(0);
        cmd_oldcheck_status();
        return;
    }
    if (strcmp(sub, "draw") == 0 || strcmp(sub, "run") == 0 || strcmp(sub, "retro") == 0) {
        (void)lardkit_oldcheck_run(1);
        cmd_oldcheck_status();
        return;
    }
    if (strcmp(sub, "test") == 0) {
        out_append(lardkit_oldcheck_run(0) == 0 ? "oldcheck: selftest OK\n" : "oldcheck: selftest failed\n");
        return;
    }
    out_append("Usage: oldcheck status|draw|test\n");
}

static void cmd_lfsdoctor_status(void)
{
    lardkit_lfsdoctor_info_t info;
    lardkit_lfsdoctor_info(&info);
    out_append("LFSDoctor files=");
    out_append_u32(info.files);
    out_append(" storage=");
    out_append(info.storage_available ? "online" : "offline");
    out_append(" dirty=");
    out_append_u32(info.dirty);
    out_append(" gen=");
    out_append_u32(info.generation);
    out_append(" last=");
    out_append_i32(info.last_result);
    out_append(" repairs=");
    out_append_u32(info.repairs);
    out_append(" repair-last=");
    out_append_i32(info.last_repair);
    out_append("\n");
}

static void cmd_lfsdoctor(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        (void)lardkit_lfsdoctor_scan(0);
        cmd_lfsdoctor_status();
        return;
    }
    if (strcmp(sub, "scan") == 0 || strcmp(sub, "check") == 0) {
        (void)lardkit_lfsdoctor_scan(0);
        out_append("lfsdoctor: wrote lfsdoctor.lardd\n");
        cmd_lfsdoctor_status();
        return;
    }
    if (strcmp(sub, "repair") == 0 || strcmp(sub, "fix") == 0) {
        int r = lardkit_lfsdoctor_scan(1);
        out_append(r == 0 ? "lfsdoctor: repair path completed.\n" : "lfsdoctor: repair path reported an issue.\n");
        cmd_lfsdoctor_status();
        return;
    }
    if (strcmp(sub, "show") == 0 || strcmp(sub, "doc") == 0) {
        (void)lardkit_lfsdoctor_scan(0);
        cmd_larddoc("lfsdoctor.lardd", "Usage: lfsdoctor show");
        return;
    }
    out_append("Usage: lfsdoctor status|scan|repair|show\n");
}

static void cmd_trace(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        lardkit_trace_info_t info;
        lardkit_trace_info(&info);
        out_append("LardTrace ");
        out_append(info.enabled ? "on" : "off");
        out_append(" events=");
        out_append_u32(info.count);
        out_append(" next=");
        out_append_u32(info.next_seq);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "on") == 0) {
        lardkit_trace_enable(1);
        out_append("trace: on\n");
        return;
    }
    if (strcmp(sub, "off") == 0) {
        lardkit_trace_enable(0);
        out_append("trace: off\n");
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        lardkit_trace_clear();
        out_append("trace: cleared\n");
        return;
    }
    if (strcmp(sub, "show") == 0 || strcmp(sub, "log") == 0) {
        (void)lardkit_trace_write();
        cmd_larddoc("trace.lardd", "Usage: trace show");
        return;
    }
    if (strcmp(sub, "module") == 0 || strcmp(sub, "mod") == 0) {
        char module[32];
        uint32_t count = lardkit_trace_count();
        if (vcs_read_word(&args, module, sizeof(module)) != 0) {
            out_append("Usage: trace module gui|shell|oslink|taskprio\n");
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            lardkit_trace_entry_t e;
            if (lardkit_trace_at(i, &e) != 0 || strcmp(e.module, module) != 0) continue;
            out_append("#");
            out_append_u32(e.seq);
            out_append(" ");
            out_append(e.module);
            out_append(" ");
            out_append(e.text);
            out_append(" ");
            out_append_i32(e.value);
            out_append("\n");
        }
        return;
    }
    out_append("Usage: trace on|off|status|show|module name|clear\n");
}

static void cmd_netwatch(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        lardkit_netwatch_info_t info;
        lardkit_netwatch_info(&info);
        out_append("NetWatch ");
        out_append(info.enabled ? "on" : "off");
        out_append(" events=");
        out_append_u32(info.count);
        out_append(" sent=");
        out_append_u32(info.sent);
        out_append(" recv=");
        out_append_u32(info.received);
        out_append(" http=");
        out_append_u32(info.http);
        out_append(" oslink=");
        out_append_u32(info.oslink);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "on") == 0) {
        lardkit_netwatch_enable(1);
        out_append("netwatch: on\n");
        return;
    }
    if (strcmp(sub, "off") == 0) {
        lardkit_netwatch_enable(0);
        out_append("netwatch: off\n");
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        lardkit_netwatch_clear();
        out_append("netwatch: cleared\n");
        return;
    }
    if (strcmp(sub, "show") == 0 || strcmp(sub, "log") == 0) {
        (void)lardkit_netwatch_write();
        cmd_larddoc("netwatch.lardd", "Usage: netwatch show");
        return;
    }
    out_append("Usage: netwatch on|off|status|show|clear\n");
}

static void cmd_journal(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "show") == 0 || strcmp(sub, "status") == 0) {
        cmd_larddoc("journal.lardd", "Usage: journal show|clear|add text");
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        out_append(lardkit_journal_clear() == 0 ? "journal: cleared\n" : "journal: clear failed\n");
        return;
    }
    if (strcmp(sub, "add") == 0 || strcmp(sub, "write") == 0) {
        while (*args == ' ' || *args == '\t') args++;
        lardkit_journal_event("user", args && args[0] ? args : "manual event");
        out_append("journal: added\n");
        return;
    }
    out_append("Usage: journal show|clear|add text\n");
}

static void cmd_bootreplay(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "show") == 0 || strcmp(sub, "status") == 0) {
        (void)lardkit_bootreplay_write();
        cmd_larddoc("bootreplay.lardd", "Usage: bootreplay show");
        return;
    }
    if (strcmp(sub, "write") == 0 || strcmp(sub, "capture") == 0) {
        out_append(lardkit_bootreplay_write() == 0 ? "bootreplay: wrote bootreplay.lardd\n" : "bootreplay: write failed\n");
        return;
    }
    out_append("Usage: bootreplay show|write\n");
}

static void cmd_postbaseline(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "show") == 0 || strcmp(sub, "status") == 0) {
        cmd_larddoc("postbaseline.lardd", "Usage: postbaseline show");
        return;
    }
    out_append("Usage: postbaseline show\n");
}

static void devmap_draw(void)
{
    gui_syscall_fill_rect(32, 72, 430, 180, 0xFF081018u);
    gui_syscall_draw_text(44, 84, "DEVICE MAP", 0xFFFFFFFFu, 0xFF081018u);
    gui_syscall_fill_rect(44, 112, 84, 28, 0xFF4DE1C1u);
    gui_syscall_draw_text(50, 122, "PCI", 0xFF081018u, 0xFF4DE1C1u);
    gui_syscall_fill_rect(144, 112, 110, 28, 0xFFFFD84Au);
    gui_syscall_draw_text(150, 122, "STORAGE", 0xFF081018u, 0xFFFFD84Au);
    gui_syscall_fill_rect(270, 112, 150, 28, 0xFF72D6FFu);
    gui_syscall_draw_text(276, 122, "NETWORK", 0xFF081018u, 0xFF72D6FFu);
    gui_syscall_draw_text(44, 158, "rtl8139 / piix3 / LPST / OSLink", 0xFFFFFFFFu, 0xFF081018u);
}

static void cmd_devmap(const char* args)
{
    pci_addr_t a;
    uint32_t available = 0, dirty = 0, lba = 0, sectors = 0;
    int last = 0;
    const char* driver = NULL;
    oslink_info_t oi;
    if (args_word_is(args, "draw")) devmap_draw();
    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    oslink_info(&oi);
    out_append("Device Map\n");
    out_append("pci rtl8139=");
    out_append(pci_find_device(0x10ECu, 0x8139u, &a) == 0 ? "present" : "missing");
    out_append(" piix3ide=");
    out_append(pci_find_device(0x8086u, 0x7010u, &a) == 0 ? "present" : "missing");
    out_append("\nstorage=");
    out_append(available ? "online" : "offline");
    out_append(" driver=");
    out_append(driver ? driver : "none");
    out_append(" dirty=");
    out_append(dirty ? "yes" : "no");
    out_append(" last=");
    out_append_i32(last);
    out_append("\noslink=");
    out_append(oi.ready ? "ready" : "offline");
    out_append(" peers=");
    out_append_u32(oi.peer_count);
    out_append(" inbox=");
    out_append_u32(oi.inbox_count);
    out_append("\n");
}

static void cmd_cfgprof(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "list") == 0 || strcmp(sub, "status") == 0) {
        uint32_t count = lardkit_cfgprof_count();
        out_append("CFG profiles=");
        out_append_u32(count);
        out_append("\n");
        for (uint32_t i = 0; i < count; i++) {
            lardkit_cfg_profile_t p;
            if (lardkit_cfgprof_at(i, &p) != 0) continue;
            out_append("  ");
            out_append(p.name);
            out_append(" boot=");
            out_append(p.boot_profile);
            out_append(" prio=");
            out_append_i32(p.task_default);
            out_append("\n");
        }
        return;
    }
    if (strcmp(sub, "save") == 0) {
        char name[32];
        if (vcs_read_word(&args, name, sizeof(name)) != 0 || lardkit_cfgprof_save(name) != 0) {
            out_append("Usage: cfgprof save name\n");
            return;
        }
        out_append("cfgprof: saved\n");
        return;
    }
    if (strcmp(sub, "load") == 0) {
        char name[32];
        if (vcs_read_word(&args, name, sizeof(name)) != 0 || lardkit_cfgprof_load(name) != 0) {
            out_append("Usage: cfgprof load name\n");
            return;
        }
        out_append("cfgprof: loaded\n");
        return;
    }
    if (strcmp(sub, "show") == 0) {
        (void)lardkit_cfgprof_write();
        cmd_larddoc("cfgprof.lardd", "Usage: cfgprof show");
        return;
    }
    out_append("Usage: cfgprof list|save name|load name|show\n");
}

static void cmd_userlaw(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "show") == 0 || strcmp(sub, "status") == 0) {
        cmd_larddoc("userlaw.lardd", "Usage: userlaw show|reset|check");
        return;
    }
    if (strcmp(sub, "reset") == 0) {
        out_append(lardkit_userlaw_reset() == 0 ? "userlaw: reset\n" : "userlaw: reset failed\n");
        return;
    }
    if (strcmp(sub, "check") == 0) {
        out_append(lardkit_userlaw_check() == 0 ? "userlaw: valid\n" : "userlaw: invalid\n");
        return;
    }
    out_append("Usage: userlaw show|reset|check\n");
}

static int lunit_prefix(const char* s, const char* end, const char* pfx, const char** rest)
{
    while (s < end && *pfx && *s == *pfx) {
        s++;
        pfx++;
    }
    if (*pfx) return 0;
    if (rest) *rest = s;
    return 1;
}

static void cmd_lunit(const char* args)
{
    char sub[16];
    char file_arg[64];
    const uint8_t* data;
    uint32_t size;
    uint32_t pass = 0, fail = 0;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 || strcmp(sub, "run") != 0 ||
        vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0 ||
        lsh_doc_data_from_arg(file_arg, &data, &size) != 0) {
        out_append("Usage: lunit run tests.lunit\n");
        return;
    }
    const char* p = (const char*)data;
    const char* end = p + size;
    if (size < 8u || data[0] != 'L' || data[1] != 'U' || data[2] != 'N' || data[3] != 'I' || data[4] != 'T') {
        out_append("lunit: bad header\n");
        return;
    }
    while (p < end) {
        const char* ls = p;
        const char* le;
        const char* rest;
        while (p < end && *p != '\n' && *p != '\r') p++;
        le = p;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
        if (lunit_prefix(ls, le, "CHECK file ", &rest)) {
            char name[64];
            uint32_t i = 0;
            while (rest < le && i + 1u < sizeof(name)) name[i++] = *rest++;
            name[i] = '\0';
            if (fs_open(name)) pass++; else fail++;
        } else if (lunit_prefix(ls, le, "CHECK writable ", &rest)) {
            char name[64];
            uint32_t i = 0;
            while (rest < le && i + 1u < sizeof(name)) name[i++] = *rest++;
            name[i] = '\0';
            if (fs_open_writable(name)) pass++; else fail++;
        } else if (lunit_prefix(ls, le, "CHECK command ", &rest)) {
            char name[64];
            uint32_t i = 0;
            while (rest < le && i + 1u < sizeof(name)) name[i++] = *rest++;
            name[i] = '\0';
            if (magic_find_exact(name)) pass++; else fail++;
        }
    }
    out_append("lunit: ");
    out_append_u32(pass);
    out_append(" passed, ");
    out_append_u32(fail);
    out_append(" failed\n");
}

static void cmd_larsapp(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "open") == 0 || strcmp(sub, "show") == 0) {
        cmd_larddoc(args && args[0] ? args : "lardos.lars", "Usage: larsapp open file.lars");
        return;
    }
    if (strcmp(sub, "form") == 0 || strcmp(sub, "controls") == 0) {
        cmd_larsform(args);
        return;
    }
    if (strcmp(sub, "run") == 0 || strcmp(sub, "act") == 0) {
        cmd_larsact(args);
        return;
    }
    out_append("Usage: larsapp open file.lars | larsapp form file.lars | larsapp run file.lars index\n");
}

static void cmd_ltheme_status(void)
{
    lardkit_theme_info_t info;
    lardkit_theme_info(&info);
    out_append("LTheme active=");
    out_append(info.name);
    out_append(" fg=");
    out_append_hex32(info.fg);
    out_append(" bg=");
    out_append_hex32(info.bg);
    out_append(" accent=");
    out_append_hex32(info.accent);
    out_append(" style-hint=");
    out_append_u32(info.style_hint);
    out_append("\n");
}

static void cmd_ltheme(const char* args)
{
    char sub[16];
    char name[32];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_ltheme_status();
        return;
    }
    if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) {
        for (uint32_t i = 0; i < lardkit_theme_count(); i++) {
            lardkit_theme_info_t info;
            if (lardkit_theme_at(i, &info) == 0) {
                out_append("  ");
                out_append(info.name);
                out_append(" accent=");
                out_append_hex32(info.accent);
                out_append("\n");
            }
        }
        return;
    }
    if (strcmp(sub, "show") == 0) {
        const FsFile* f;
        lardkit_theme_info_t info;
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: ltheme show file.ltheme\n");
            return;
        }
        f = fs_open(name);
        if (!f || lardkit_theme_parse(f->data, f->size, &info) != 0) {
            out_append("ltheme: not a LTHEME file.\n");
            return;
        }
        out_append("LTHEME ");
        out_append(info.name);
        out_append(" fg=");
        out_append_hex32(info.fg);
        out_append(" bg=");
        out_append_hex32(info.bg);
        out_append(" accent=");
        out_append_hex32(info.accent);
        out_append(" style-hint=");
        out_append_u32(info.style_hint);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "preview") == 0) {
        const FsFile* f;
        lardkit_theme_info_t info;
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: ltheme preview file.ltheme\n");
            return;
        }
        f = fs_open(name);
        if (!f || lardkit_theme_parse(f->data, f->size, &info) != 0) {
            out_append("ltheme: not a LTHEME file.\n");
            return;
        }
        out_append(lardkit_theme_preview_draw(&info) == 0 ? "ltheme: preview drawn.\n" : "ltheme: preview failed.\n");
        return;
    }
    if (strcmp(sub, "use") == 0 || strcmp(sub, "set") == 0) {
        const FsFile* f;
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: ltheme use classic|contrast|night|amber|file.ltheme\n");
            return;
        }
        if (lardkit_theme_use(name) != 0) {
            f = fs_open(name);
            if (!f || lardkit_theme_use_data(f->data, f->size) != 0) {
                out_append("Usage: ltheme use classic|contrast|night|amber|file.ltheme\n");
                return;
            }
        }
        cmd_ltheme_status();
        return;
    }
    out_append("Usage: ltheme status|list|show file|preview file|use name\n");
}

static void cmd_awakemon(const char* args)
{
    (void)args;
    lardkit_awakemon_info_t info;
    lardkit_awakemon_info(&info);
    out_append("AwakeMonitor phase=");
    out_append_u32(info.current_phase);
    out_append("/");
    out_append_u32(info.phase_count);
    out_append(" percent=");
    out_append_u32(info.percent);
    out_append("% done=");
    out_append_u32(info.done);
    out_append(" current=");
    out_append(info.current);
    out_append("\n");
}

static void cmd_oschat(const char* args)
{
    char sub[16];
    char channel[OSLINK_CHANNEL_MAX + 1u];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        oslink_info_t info;
        oslink_info(&info);
        out_append("OSChat inbox=");
        out_append_u32(info.inbox_count);
        out_append(" local=");
        out_append_u32(info.local_count);
        out_append(" emitted=");
        out_append_u32(info.local_sent);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "send") == 0 || strcmp(sub, "say") == 0) {
        if (strcmp(sub, "send") == 0) {
            if (vcs_read_word(&args, channel, sizeof(channel)) != 0) {
                out_append("Usage: oschat send channel text\n");
                return;
            }
        } else {
            channel[0] = 'c'; channel[1] = 'h'; channel[2] = 'a'; channel[3] = 't'; channel[4] = '\0';
        }
        while (*args == ' ' || *args == '\t') args++;
        if (oslink_emit_local(channel, args) == 0) out_append("oschat: sent.\n");
        else out_append("oschat: empty message or inbox full.\n");
        return;
    }
    if (strcmp(sub, "read") == 0 || strcmp(sub, "recv") == 0) {
        oslink_msg_t m;
        if (oslink_recv(&m) == 0) {
            out_append("oschat: no messages.\n");
            return;
        }
        out_append(oslink_type_name(m.type));
        out_append(" channel=");
        out_append(m.channel[0] ? m.channel : "main");
        out_append(" ");
        out_append(m.text);
        out_append("\n");
        return;
    }
    out_append("Usage: oschat status|say text|send channel text|read\n");
}

static void cmd_larsview_status(void)
{
    lardkit_larsview_info_t info;
    lardkit_larsview_info(&info);
    out_append("LARSView path=");
    out_append(info.path);
    out_append(" previous=");
    out_append(info.previous_path[0] ? info.previous_path : "none");
    out_append(" opened=");
    out_append_u32(info.opened);
    out_append(" back=");
    out_append_u32(info.back_count);
    out_append(" size=");
    out_append_u32(info.size);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_larsview(const char* args)
{
    char sub[16];
    char path[64];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_larsview_status();
        return;
    }
    if (strcmp(sub, "home") == 0) {
        (void)lardkit_larsview_open("lardos.lars");
        cmd_larddoc("lardos.lars", "Usage: larsview open file.lars|file.lardd");
        return;
    }
    if (strcmp(sub, "reload") == 0 || strcmp(sub, "last") == 0) {
        lardkit_larsview_info_t info;
        lardkit_larsview_info(&info);
        if (lardkit_larsview_open(info.path[0] ? info.path : "lardos.lars") != 0) {
            out_append("larsview: reload failed.\n");
            return;
        }
        cmd_larddoc(info.path[0] ? info.path : "lardos.lars", "Usage: larsview reload");
        return;
    }
    if (strcmp(sub, "back") == 0) {
        lardkit_larsview_info_t info;
        if (lardkit_larsview_back() != 0) {
            out_append("larsview: no previous document.\n");
            return;
        }
        lardkit_larsview_info(&info);
        cmd_larddoc(info.path, "Usage: larsview back");
        return;
    }
    if (strcmp(sub, "actions") == 0 || strcmp(sub, "buttons") == 0) {
        lardkit_larsview_info_t info;
        if (vcs_read_word(&args, path, sizeof(path)) != 0) {
            lardkit_larsview_info(&info);
            cmd_larsform(info.path[0] ? info.path : "lardos.lars");
        } else {
            cmd_larsform(path);
        }
        return;
    }
    if (strcmp(sub, "open") == 0 || strcmp(sub, "view") == 0) {
        if (vcs_read_word(&args, path, sizeof(path)) != 0) {
            out_append("Usage: larsview open file.lars|file.lardd\n");
            return;
        }
        if (lardkit_larsview_open(path) != 0) {
            out_append("larsview: file not found or not native doc.\n");
            return;
        }
        cmd_larddoc(path, "Usage: larsview open file.lars|file.lardd");
        return;
    }
    out_append("Usage: larsview status|home|open file|reload|back|actions [file]\n");
}

static void cmd_larddnotes(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "show") == 0 || strcmp(sub, "view") == 0) {
        cmd_larddoc("notes.lardd", "Usage: notes show|add text|clear");
        return;
    }
    if (strcmp(sub, "add") == 0 || strcmp(sub, "append") == 0) {
        while (*args == ' ' || *args == '\t') args++;
        if (lardkit_notes_append(args) == 0) out_append("notes: added to notes.lardd and notes.txt\n");
        else out_append("notes: add failed.\n");
        return;
    }
    if (strcmp(sub, "clear") == 0 || strcmp(sub, "reset") == 0) {
        if (lardkit_notes_reset() == 0) out_append("notes: reset notes.lardd and notes.txt\n");
        else out_append("notes: reset failed.\n");
        return;
    }
    out_append("Usage: notes show|add text|clear\n");
}

static void cmd_task_list(void)
{
    taskprio_info_t info;
    taskprio_info(&info);
    out_append("Tasks queued=");
    out_append_u32(info.queued);
    out_append(" runnable=");
    out_append_u32(info.runnable);
    out_append(" paused=");
    out_append_u32(info.paused);
    out_append(" default-prio=");
    out_append_i32(info.default_priority);
    out_append(" completed=");
    out_append_u32(info.completed);
    out_append(" lev10=");
    out_append_u32(info.os_urgent);
    out_append("\n");
    if (info.queued == 0) {
        out_append("task: no queued tasks.\n");
        return;
    }
    for (uint32_t i = 0; i < info.queued; i++) {
        taskprio_task_t t;
        if (taskprio_at(i, &t) == 0) {
            out_append("#");
            out_append_u32(t.id);
            out_append(" prio=");
            out_append_i32(t.priority);
            out_append(" wait=");
            out_append_u32(t.wait_ticks);
            out_append(t.paused ? " pause " : " run   ");
            out_append(" ");
            out_append(t.name);
            out_append(" :: ");
            out_append(t.command);
            out_append("\n");
        }
    }
}

static void cmd_task_history(const char* args)
{
    char sub[16];
    uint32_t count;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) == 0 &&
        (strcmp(sub, "clear") == 0 || strcmp(sub, "reset") == 0)) {
        taskprio_history_clear();
        out_append("priority history: cleared.\n");
        return;
    }
    count = taskprio_history_count();
    out_append("Priority lev.10 history count=");
    out_append_u32(count);
    out_append("\n");
    if (count == 0) {
        out_append("priority history: no lev.10 grants yet.\n");
        return;
    }
    out_append("SEQ ACTOR ACTION TASK OLD->NEW\n");
    for (uint32_t i = 0; i < count; i++) {
        taskprio_history_entry_t e;
        if (taskprio_history_at(i, &e) != 0) continue;
        out_append_u32(e.seq);
        out_append(" ");
        out_append(e.actor);
        out_append(" ");
        out_append(e.action);
        out_append(" #");
        out_append_u32(e.id);
        out_append(" ");
        out_append(e.name);
        out_append(" ");
        out_append_i32(e.old_priority);
        out_append("->");
        out_append_i32(e.new_priority);
        out_append("\n");
    }
}

static void tasktop_bar(int32_t priority)
{
    if (priority == TASKPRIO_OS_LEVEL) {
        out_append("[LEV10!!!]");
        return;
    }
    out_append("[");
    for (int32_t i = 0; i <= TASKPRIO_MAX; i++) {
        out_append_char(i <= priority ? '#' : '.');
    }
    out_append("]");
}

static void cmd_tasktop(const char* args)
{
    (void)args;
    taskprio_info_t info;
    taskprio_info(&info);
    out_append("TASKTOP  queued=");
    out_append_u32(info.queued);
    out_append(" runnable=");
    out_append_u32(info.runnable);
    out_append(" paused=");
    out_append_u32(info.paused);
    out_append(" done=");
    out_append_u32(info.completed);
    out_append(" lev10=");
    out_append_u32(info.os_urgent);
    out_append(" default=");
    out_append_i32(info.default_priority);
    out_append("\n");
    out_append("ID  ST  PRIO BAR        WAIT CMD\n");
    for (uint32_t i = 0; i < info.queued; i++) {
        taskprio_task_t t;
        if (taskprio_at(i, &t) == 0) {
            out_append_u32(t.id);
            out_append("  ");
            out_append(t.paused ? "PAU " : "RUN ");
            out_append_i32(t.priority);
            out_append("    ");
            tasktop_bar(t.priority);
            out_append(" ");
            out_append_u32(t.wait_ticks);
            out_append("    ");
            out_append(t.command);
            out_append("\n");
        }
    }
    if (info.queued == 0) out_append("No queued tasks. Use: task run 7 echo hello\n");
}

static int task_parse_priority(const char** args, int32_t* out)
{
    uint32_t v;
    if (vcs_parse_u32(args, &v) != 0 || v > (uint32_t)TASKPRIO_MAX) return -1;
    *out = (int32_t)v;
    return 0;
}

static void cmd_task_run_like(const char* args)
{
    int32_t prio;
    uint32_t id;
    if (task_parse_priority(&args, &prio) != 0) {
        out_append("Usage: task run priority command (0..10; lev.10 urgent)\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (!*args) {
        out_append("Usage: task run priority command (0..10; lev.10 urgent)\n");
        return;
    }
    if (taskprio_enqueue(NULL, args, prio, &id) == 0) {
        out_append("task: queued #");
        out_append_u32(id);
        out_append(" prio=");
        out_append_i32(prio);
        out_append("\n");
    } else {
        out_append("task: queue full or empty command.\n");
    }
}

static void cmd_task(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        cmd_task_list();
        return;
    }
    if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0 || strcmp(sub, "status") == 0) {
        cmd_task_list();
        return;
    }
    if (strcmp(sub, "history") == 0 || strcmp(sub, "hist") == 0 || strcmp(sub, "audit") == 0) {
        cmd_task_history(args);
        return;
    }
    if (strcmp(sub, "default") == 0) {
        int32_t prio;
        if (task_parse_priority(&args, &prio) != 0) {
            out_append("task: default-prio=");
            out_append_i32(taskprio_default_priority());
            out_append("\nUsage: task default priority (0..10; lev.10 urgent)\n");
            return;
        }
        taskprio_set_default(prio);
        out_append("task: default priority ");
        out_append_i32(prio);
        out_append("\n");
        return;
    }
    if (strcmp(sub, "set") == 0 || strcmp(sub, "prio") == 0) {
        uint32_t id;
        int32_t prio;
        if (vcs_parse_u32(&args, &id) != 0 || task_parse_priority(&args, &prio) != 0) {
            out_append("Usage: task set id priority (0..10; lev.10 urgent)\n");
            return;
        }
        if (taskprio_set_priority(id, prio) == 0) {
            out_append("task: #");
            out_append_u32(id);
            out_append(" priority ");
            out_append_i32(prio);
            out_append("\n");
        } else {
            out_append("task: id not found.\n");
        }
        return;
    }
    if (strcmp(sub, "boost") == 0 || strcmp(sub, "urgent") == 0 || strcmp(sub, "lev10") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task boost|urgent|lev10 id\n");
            return;
        }
        if (taskprio_grant_urgent_priority(id) == 0) out_append("task: lev.10 granted.\n");
        else out_append("task: id not found.\n");
        return;
    }
    if (strcmp(sub, "up") == 0 || strcmp(sub, "+") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task up id\n");
            return;
        }
        if (taskprio_adjust_priority(id, 1) == 0) out_append("task: priority up.\n");
        else out_append("task: id not found.\n");
        return;
    }
    if (strcmp(sub, "down") == 0 || strcmp(sub, "-") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task down id\n");
            return;
        }
        if (taskprio_adjust_priority(id, -1) == 0) out_append("task: priority down.\n");
        else out_append("task: id not found.\n");
        return;
    }
    if (strcmp(sub, "pause") == 0 || strcmp(sub, "hold") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task pause id\n");
            return;
        }
        if (taskprio_pause(id, 1) == 0) out_append("task: paused.\n");
        else out_append("task: id not found.\n");
        return;
    }
    if (strcmp(sub, "resume") == 0 || strcmp(sub, "cont") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task resume id\n");
            return;
        }
        if (taskprio_pause(id, 0) == 0) out_append("task: resumed.\n");
        else out_append("task: id not found.\n");
        return;
    }
    if (strcmp(sub, "drop") == 0 || strcmp(sub, "kill") == 0 || strcmp(sub, "rm") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task drop id\n");
            return;
        }
        if (taskprio_remove(id) == 0) out_append("task: dropped.\n");
        else out_append("task: id not found.\n");
        return;
    }
    if (strcmp(sub, "run") == 0 || strcmp(sub, "queue") == 0) {
        cmd_task_run_like(args);
        return;
    }
    if (strcmp(sub, "test") == 0) {
        out_append(taskprio_selftest() == 0 ? "task: selftest OK\n" : "task: selftest failed\n");
        return;
    }
    out_append("Usage: task list|set|default|run|history|up|down|pause|resume|boost|urgent|drop|test\n");
    out_append("Note: lev.10 is urgent and user-grantable; wait-time aging still cannot create it.\n");
}

static void cmd_nice(const char* args)
{
    cmd_task_run_like(args);
}

static void cmd_prio(const char* args)
{
    const char* p = args ? args : "";
    char word[16];
    uint32_t id;
    int32_t prio;
    if (vcs_read_word(&p, word, sizeof(word)) == 0 &&
        (strcmp(word, "history") == 0 || strcmp(word, "hist") == 0 || strcmp(word, "audit") == 0)) {
        cmd_task_history(p);
        return;
    }
    if (vcs_parse_u32(&args, &id) != 0 || task_parse_priority(&args, &prio) != 0) {
        out_append("Usage: prio id priority | prio history (0..10; lev.10 urgent)\n");
        return;
    }
    if (taskprio_set_priority(id, prio) == 0) out_append("prio: updated.\n");
    else out_append("prio: id not found.\n");
}

static void cmd_awake_status(void)
{
    bootprof_info_t bp;
    awake_info_t info;
    bootprof_info(&bp);
    awake_info(&info);
    out_append("Awakening mode: ");
    out_append(bp.awakening_mode ? "profile-on" : "profile-off");
    out_append(", loader=");
    out_append(info.enabled ? (info.done ? "complete" : "background") : "off");
    out_append("\nphase=");
    out_append_u32(info.phase);
    out_append("/");
    out_append_u32(info.total);
    out_append(" current=");
    out_append(info.current);
    out_append(" runs=");
    out_append_u32(info.background_runs);
    out_append(" err=");
    out_append_u32(info.last_error);
    out_append("\n");
}

static void cmd_awake(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_awake_status();
        return;
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "enable") == 0 || strcmp(sub, "start") == 0) {
        int r = bootprof_set("awakening");
        if (r == 0) {
            out_append("awake: on for next boot. Run sync to persist. Current boot keeps its startup path.\n");
        } else {
            out_append("awake: failed to store awakening profile.\n");
        }
        return;
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "disable") == 0 || strcmp(sub, "stop") == 0) {
        int r = bootprof_set("normal");
        awake_enable(0, 0);
        if (r == 0) {
            out_append("awake: off. Run sync to persist. Next boot is normal; current background loader stopped.\n");
        } else {
            out_append("awake: failed to store normal profile.\n");
        }
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(awake_selftest() == 0 ? "awake: selftest OK\n" : "awake: selftest failed\n");
        return;
    }
    out_append("Usage: awake on|off|status|test\n");
}

static void cmd_buddy_status(void)
{
    lassist_info_t info;
    lassist_info(&info);
    out_append("Lard Buddy: ");
    out_append(info.enabled ? "on" : "off");
    out_append(", mood=");
    out_append(lassist_mood_name(info.mood));
    out_append(", jokes=");
    out_append_u32(info.jokes);
    out_append(", tick=");
    out_append_u32(info.tick);
    out_append("\n\"");
    out_append(info.message);
    out_append("\"\n");
}

static void cmd_buddy(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cmd_buddy_status();
        return;
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "enable") == 0 || strcmp(sub, "start") == 0) {
        lassist_enable(1);
        out_append("buddy: on. I will hover politely and pretend this was my idea.\n");
        return;
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "disable") == 0 || strcmp(sub, "stop") == 0) {
        lassist_enable(0);
        out_append("buddy: off. I will be emotionally available off-screen.\n");
        return;
    }
    if (strcmp(sub, "joke") == 0 || strcmp(sub, "fun") == 0) {
        lassist_joke();
        cmd_buddy_status();
        return;
    }
    if (strcmp(sub, "next") == 0 || strcmp(sub, "tip") == 0) {
        lassist_next(0);
        cmd_buddy_status();
        return;
    }
    if (strcmp(sub, "mood") == 0 || strcmp(sub, "personality") == 0) {
        char mood[16];
        if (vcs_read_word(&args, mood, sizeof(mood)) != 0 || lassist_set_mood(mood) != 0) {
            out_append("Usage: buddy mood calm|funny|strict|silent\n");
            return;
        }
        lassist_enable(strcmp(mood, "silent") == 0 ? lassist_enabled() : 1);
        cmd_buddy_status();
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(lassist_selftest() == 0 ? "buddy: selftest OK\n" : "buddy: selftest failed\n");
        return;
    }
    out_append("Usage: buddy on|off|status|joke|next|mood|test\n");
}

static void cmd_bootprof_status(void)
{
    bootprof_info_t info;
    bootprof_info(&info);
    out_append("Boot profile: ");
    out_append(info.name);
    out_append("\n");
    out_append("network=");
    out_append(info.network ? "on" : "off");
    out_append(" force_post=");
    out_append(info.force_post ? "on" : "off");
    out_append(" safe=");
    out_append(info.safe_mode ? "on" : "off");
    out_append(" dev=");
    out_append(info.dev_mode ? "on" : "off");
    out_append(" awake=");
    out_append(info.awakening_mode ? "on" : "off");
    out_append("\nProfiles: normal safe netoff dev awakening\n");
    out_append("Stored in bootprof.txt. Use sync to persist it.\n");
}

static void cmd_bootprof(const char* args)
{
    char sub[16];
    char name[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0 ||
        strcmp(sub, "list") == 0) {
        cmd_bootprof_status();
        return;
    }
    if (strcmp(sub, "set") == 0 || strcmp(sub, "use") == 0) {
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: bootprof set normal|safe|netoff|dev|awakening\n");
            return;
        }
        int r = bootprof_set(name);
        if (r == 0) {
            out_append("bootprof: set ");
            out_append(name);
            out_append(". Run sync to persist.\n");
        } else {
            out_append("bootprof: unknown profile.\n");
        }
        return;
    }
    if (strcmp(sub, "test") == 0) {
        out_append(bootprof_selftest() == 0 ? "bootprof: selftest OK\n" : "bootprof: selftest failed\n");
        return;
    }
    out_append("Usage: bootprof status|set|test\n");
}

static void cmd_crashlog(const char* args)
{
    char sub[16];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "show") == 0 || strcmp(sub, "status") == 0) {
        const char* text = crashlog_text();
        out_append(text && text[0] ? text : "crashlog: empty\n");
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        if (crashlog_clear() == 0) out_append("crashlog: cleared.\n");
        else out_append("crashlog: unavailable.\n");
        return;
    }
    if (strcmp(sub, "test") == 0) {
        crashlog_record("test", "manual diagnostic event");
        out_append("crashlog: test event recorded.\n");
        return;
    }
    if (strcmp(sub, "selftest") == 0) {
        out_append(crashlog_selftest() == 0 ? "crashlog: selftest OK\n" : "crashlog: selftest failed\n");
        return;
    }
    out_append("Usage: crashlog show|clear|test|selftest\n");
}

static int lsh_require_sum(const char* cmd)
{
    if (s_in_sum_mode) return 1;
    out_append(cmd);
    out_append(": SUM mode required. Type 'sum' first.\n");
    return 0;
}

static void cmd_peek(const char* args)
{
    uint64_t addr;
    uint64_t len = 64;
    volatile const uint8_t* p;
    if (!lsh_require_sum("peek")) return;
    if (lsh_parse_u64(&args, &addr) != 0) {
        out_append("Usage: peek addr [len]\n");
        return;
    }
    if (*args && lsh_parse_u64(&args, &len) != 0) {
        out_append("Usage: peek addr [len]\n");
        return;
    }
    if (len == 0) len = 1;
    if (len > 256) {
        out_append("peek: capped to 256 bytes for console output.\n");
        len = 256;
    }
    p = (volatile const uint8_t*)(uintptr_t)addr;
    for (uint64_t i = 0; i < len; i++) {
        if ((i & 15u) == 0) {
            if (i) out_append("\n");
            out_append_hex64(addr + i);
            out_append(": ");
        }
        out_append_hex8(p[i]);
        out_append_char(' ');
    }
    out_append("\n");
}

static void cmd_poke(const char* args)
{
    uint64_t addr;
    uint64_t val;
    uint64_t width = 8;
    if (!lsh_require_sum("poke")) return;
    if (lsh_parse_u64(&args, &addr) != 0 || lsh_parse_u64(&args, &val) != 0) {
        out_append("Usage: poke addr value [8|16|32]\n");
        return;
    }
    if (*args && lsh_parse_u64(&args, &width) != 0) {
        out_append("Usage: poke addr value [8|16|32]\n");
        return;
    }
    if (width == 8) {
        *(volatile uint8_t*)(uintptr_t)addr = (uint8_t)val;
    } else if (width == 16) {
        *(volatile uint16_t*)(uintptr_t)addr = (uint16_t)val;
    } else if (width == 32) {
        *(volatile uint32_t*)(uintptr_t)addr = (uint32_t)val;
    } else {
        out_append("poke: width must be 8, 16, or 32.\n");
        return;
    }
    out_append("poke: wrote ");
    out_append_u32((uint32_t)width);
    out_append("-bit ");
    out_append_hex64(val);
    out_append(" -> ");
    out_append_hex64(addr);
    out_append("\n");
}

static int lsh_read_data_arg(const char* arg, const uint8_t** data, uint32_t* size,
                             char* name, uint32_t name_cap)
{
    char drv;
    const FsFile* f;
    FsWritableFile* w;
    resolve_path(arg, &drv, name, name_cap);
    if (!name[0]) return -1;
    f = lsh_open_read(drv, name);
    if (f) {
        *data = f->data;
        *size = f->size;
        return 0;
    }
    w = fs_open_writable(name);
    if (w) {
        *data = w->data;
        *size = w->size;
        return 0;
    }
    return -2;
}

#define LSH_GLYPH_MAX_BMP_PIXELS (128u * 128u)
static uint32_t s_glyph_load_pixels[LSH_GLYPH_MAX_BMP_PIXELS];

static int glyph_hex_digit(char c, uint32_t* out)
{
    if (c >= '0' && c <= '9') { *out = (uint32_t)(c - '0'); return 1; }
    if (c >= 'A' && c <= 'F') { *out = 10u + (uint32_t)(c - 'A'); return 1; }
    if (c >= 'a' && c <= 'f') { *out = 10u + (uint32_t)(c - 'a'); return 1; }
    return 0;
}

static int glyph_parse_cp(const char* s, uint32_t* out)
{
    uint32_t v = 0;
    int base = 10;
    int saw = 0;
    int has_alpha = 0;
    const char* p = s;
    if (!s || !out) return -1;
    if ((p[0] == 'U' || p[0] == 'u') && p[1] == '+') {
        base = 16;
        p += 2;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else {
        for (uint32_t i = 0; p[i]; i++) {
            if ((p[i] >= 'A' && p[i] <= 'F') || (p[i] >= 'a' && p[i] <= 'f')) has_alpha = 1;
        }
        if (has_alpha) base = 16;
    }
    while (*p) {
        uint32_t d = 0;
        if (base == 16) {
            if (!glyph_hex_digit(*p, &d)) return -2;
        } else {
            if (*p < '0' || *p > '9') return -2;
            d = (uint32_t)(*p - '0');
        }
        if (d >= (uint32_t)base) return -2;
        v = v * (uint32_t)base + d;
        saw = 1;
        p++;
    }
    if (!saw || v < IMG_GLYPH_PUA_START || v > IMG_GLYPH_PUA_END) return -3;
    *out = v;
    return 0;
}

static int glyph_parse_color(const char* s, uint32_t* out)
{
    uint32_t v = 0;
    uint32_t n = 0;
    const char* p = s;
    if (!s || !out) return -1;
    if (*p == '#') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (*p) {
        uint32_t d = 0;
        if (!glyph_hex_digit(*p, &d)) return -2;
        v = (v << 4) | d;
        n++;
        p++;
    }
    if (n == 6u) v |= 0xFF000000u;
    else if (n != 8u) return -3;
    *out = v;
    return 0;
}

static void glyph_out_cp(uint32_t cp)
{
    static const char hex[] = "0123456789ABCDEF";
    out_append("U+");
    for (int i = 3; i >= 0; i--) {
        out_append_char(hex[(cp >> (uint32_t)(i * 4)) & 0xFu]);
    }
}

static void glyph_out_info(const img_glyph_info_t* info)
{
    if (!info) return;
    glyph_out_cp(info->cp);
    out_append(" ");
    out_append(info->name);
    out_append(" src=");
    out_append_u32(info->source_w);
    out_append("x");
    out_append_u32(info->source_h);
    out_append(" avg=");
    out_append_hex32(info->avg_argb);
    out_append(" rev=");
    out_append_u32(info->revision);
    out_append(" live=");
    out_append(info->live ? "on" : "off");
    out_append(" clicks=");
    out_append_u32(info->click_count);
    out_append("\n");
}

static void glyph_usage(void)
{
    out_append("Usage: glyph demo|list|load U+E000 file.bmp [name]|auto file.bmp [name]|show U+E000|move U+E000 U+E010|copy U+E000 U+E011|rename U+E000 name|pixel U+E000 x y RRGGBB|clear U+E000|live U+E000 on|click U+E000|insert U+E000 notes.txt|write\n");
}

static int glyph_load_bmp_to_slot(uint32_t cp, const char* file_arg, const char* label)
{
    const uint8_t* data = NULL;
    uint32_t size = 0;
    char name[64];
    bmp_result_t probe;
    bmp_result_t br;
    int r;

    if (lsh_read_data_arg(file_arg, &data, &size, name, sizeof(name)) != 0) {
        out_append("glyph: BMP file not found.\n");
        return -1;
    }
    probe.pixels = NULL;
    probe.w = 0;
    probe.h = 0;
    probe.has_alpha = 0;
    r = bmp_decode(data, size, &probe);
    if (r != 0) {
        out_append("glyph: only uncompressed 24/32-bit BMP files are supported.\n");
        return -2;
    }
    if (probe.w == 0 || probe.h == 0 || probe.w * probe.h > LSH_GLYPH_MAX_BMP_PIXELS) {
        out_append("glyph: BMP is too large for the in-kernel importer (max 128x128).\n");
        return -3;
    }
    br.pixels = s_glyph_load_pixels;
    br.w = 0;
    br.h = 0;
    br.has_alpha = 0;
    r = bmp_decode(data, size, &br);
    if (r != 0) {
        out_append("glyph: BMP decode failed.\n");
        return -4;
    }
    if (img_glyph_assign_named(cp, s_glyph_load_pixels, (uint16_t)br.w, (uint16_t)br.h,
                               (label && label[0]) ? label : name) != 0) {
        out_append("glyph: assign failed.\n");
        return -5;
    }
    img_glyph_write_lardd();
    out_append("glyph: assigned ");
    glyph_out_cp(cp);
    out_append(" from ");
    out_append(name);
    out_append(" (");
    out_append_u32(br.w);
    out_append("x");
    out_append_u32(br.h);
    out_append(")\n");
    return 0;
}

static void cmd_glyph(const char* args)
{
    char sub[16];
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        glyph_usage();
        return;
    }

    if (strcmp(sub, "demo") == 0 || strcmp(sub, "seed") == 0) {
        img_glyph_assign_pattern(0xE000u, "face");
        img_glyph_assign_pattern(0xE001u, "window");
        img_glyph_assign_pattern(0xE002u, "spark");
        img_glyph_assign_pattern(0xE003u, "badge");
        img_glyph_write_lardd();
        out_append("glyph: demo picture characters assigned at U+E000..U+E003.\n");
        return;
    }

    if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) {
        uint32_t count = img_glyph_count();
        out_append("Image glyphs ");
        out_append_u32(count);
        out_append("/");
        out_append_u32(IMG_GLYPH_PUA_END - IMG_GLYPH_PUA_START + 1u);
        out_append("\n");
        if (!count) {
            out_append("  none; try glyph demo or glyph auto sample.bmp sample\n");
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            img_glyph_info_t info;
            if (img_glyph_info_at(i, &info) == 0) {
                out_append("  ");
                glyph_out_info(&info);
            }
        }
        return;
    }

    if (strcmp(sub, "load") == 0 || strcmp(sub, "set") == 0 || strcmp(sub, "assign") == 0) {
        char cp_arg[16];
        char file_arg[64];
        char label[IMG_GLYPH_NAME_MAX];
        uint32_t cp;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 ||
            vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0) {
            out_append("Usage: glyph load U+E000 file.bmp [name]\n");
            return;
        }
        if (glyph_parse_cp(cp_arg, &cp) != 0) {
            out_append("glyph: codepoint must be U+E000..U+E0FF.\n");
            return;
        }
        if (vcs_read_word(&args, label, sizeof(label)) != 0) label[0] = '\0';
        glyph_load_bmp_to_slot(cp, file_arg, label);
        return;
    }

    if (strcmp(sub, "auto") == 0 || strcmp(sub, "import") == 0) {
        char file_arg[64];
        char label[IMG_GLYPH_NAME_MAX];
        uint32_t cp;
        if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0) {
            out_append("Usage: glyph auto file.bmp [name]\n");
            return;
        }
        if (img_glyph_next_free(&cp) != 0) {
            out_append("glyph: no free PUA slots.\n");
            return;
        }
        if (vcs_read_word(&args, label, sizeof(label)) != 0) label[0] = '\0';
        glyph_load_bmp_to_slot(cp, file_arg, label);
        return;
    }

    if (strcmp(sub, "show") == 0 || strcmp(sub, "info") == 0) {
        char cp_arg[16];
        uint32_t cp;
        img_glyph_info_t info;
        char bytes[5];
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 || glyph_parse_cp(cp_arg, &cp) != 0) {
            out_append("Usage: glyph show U+E000\n");
            return;
        }
        if (img_glyph_info(cp, &info) != 0) {
            out_append("glyph: slot is empty.\n");
            return;
        }
        glyph_out_info(&info);
        if (img_glyph_utf8(cp, bytes) == 3) {
            out_append("char=");
            out_append(bytes);
            out_append(" bytes=");
            out_append_hex8((uint8_t)bytes[0]);
            out_append(" ");
            out_append_hex8((uint8_t)bytes[1]);
            out_append(" ");
            out_append_hex8((uint8_t)bytes[2]);
            out_append("\n");
        }
        return;
    }

    if (strcmp(sub, "move") == 0 || strcmp(sub, "mv") == 0 || strcmp(sub, "recode") == 0) {
        char from_arg[16];
        char to_arg[16];
        uint32_t from_cp;
        uint32_t to_cp;
        gui_cursor_info_t cur;
        if (vcs_read_word(&args, from_arg, sizeof(from_arg)) != 0 ||
            vcs_read_word(&args, to_arg, sizeof(to_arg)) != 0 ||
            glyph_parse_cp(from_arg, &from_cp) != 0 ||
            glyph_parse_cp(to_arg, &to_cp) != 0) {
            out_append("Usage: glyph move U+E000 U+E010\n");
            return;
        }
        if (img_glyph_move(from_cp, to_cp) != 0) {
            out_append("glyph: source slot is empty or codepoint is invalid.\n");
            return;
        }
        gui_cursor_info(&cur);
        if (cur.enabled && cur.cp == from_cp) {
            gui_cursor_set_unicode(to_cp);
            out_append("glyph: cursor assignment followed the moved Unicode slot.\n");
        }
        img_glyph_write_lardd();
        out_append("glyph: moved ");
        glyph_out_cp(from_cp);
        out_append(" -> ");
        glyph_out_cp(to_cp);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "copy") == 0 || strcmp(sub, "cp") == 0 || strcmp(sub, "clone") == 0) {
        char from_arg[16];
        char to_arg[16];
        uint32_t from_cp;
        uint32_t to_cp;
        if (vcs_read_word(&args, from_arg, sizeof(from_arg)) != 0 ||
            vcs_read_word(&args, to_arg, sizeof(to_arg)) != 0 ||
            glyph_parse_cp(from_arg, &from_cp) != 0 ||
            glyph_parse_cp(to_arg, &to_cp) != 0) {
            out_append("Usage: glyph copy U+E000 U+E011\n");
            return;
        }
        if (img_glyph_copy(from_cp, to_cp) != 0) {
            out_append("glyph: source slot is empty or codepoint is invalid.\n");
            return;
        }
        img_glyph_write_lardd();
        out_append("glyph: copied ");
        glyph_out_cp(from_cp);
        out_append(" -> ");
        glyph_out_cp(to_cp);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "rename") == 0 || strcmp(sub, "name") == 0) {
        char cp_arg[16];
        char label[IMG_GLYPH_NAME_MAX];
        uint32_t cp;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 ||
            vcs_read_word(&args, label, sizeof(label)) != 0 ||
            glyph_parse_cp(cp_arg, &cp) != 0) {
            out_append("Usage: glyph rename U+E000 name\n");
            return;
        }
        if (img_glyph_rename(cp, label) != 0) {
            out_append("glyph: slot is empty.\n");
            return;
        }
        img_glyph_write_lardd();
        out_append("glyph: renamed ");
        glyph_out_cp(cp);
        out_append(" to ");
        out_append(label);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "pixel") == 0 || strcmp(sub, "px") == 0 || strcmp(sub, "edit") == 0) {
        char cp_arg[16];
        char color_arg[16];
        uint32_t cp;
        uint32_t x;
        uint32_t y;
        uint32_t color;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 ||
            glyph_parse_cp(cp_arg, &cp) != 0 ||
            vcs_parse_u32(&args, &x) != 0 ||
            vcs_parse_u32(&args, &y) != 0 ||
            vcs_read_word(&args, color_arg, sizeof(color_arg)) != 0 ||
            glyph_parse_color(color_arg, &color) != 0 ||
            x >= IMG_GLYPH_SIZE || y >= IMG_GLYPH_SIZE) {
            out_append("Usage: glyph pixel U+E000 x y RRGGBB  (x/y 0..7, color RRGGBB or AARRGGBB)\n");
            return;
        }
        if (img_glyph_set_pixel(cp, (uint16_t)x, (uint16_t)y, color) != 0) {
            out_append("glyph: slot is empty.\n");
            return;
        }
        img_glyph_write_lardd();
        out_append("glyph: pixel ");
        glyph_out_cp(cp);
        out_append(" [");
        out_append_u32(x);
        out_append(",");
        out_append_u32(y);
        out_append("]=");
        out_append_hex32(color);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "live") == 0 || strcmp(sub, "rt") == 0 || strcmp(sub, "realtime") == 0) {
        char cp_arg[16];
        char on_arg[16];
        uint32_t cp;
        int on;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 ||
            vcs_read_word(&args, on_arg, sizeof(on_arg)) != 0 ||
            glyph_parse_cp(cp_arg, &cp) != 0) {
            out_append("Usage: glyph live U+E000 on|off\n");
            return;
        }
        if (strcmp(on_arg, "on") == 0 || strcmp(on_arg, "1") == 0) on = 1;
        else if (strcmp(on_arg, "off") == 0 || strcmp(on_arg, "0") == 0) on = 0;
        else {
            out_append("Usage: glyph live U+E000 on|off\n");
            return;
        }
        if (img_glyph_set_live(cp, on) != 0) {
            out_append("glyph: slot is empty.\n");
            return;
        }
        img_glyph_write_lardd();
        out_append("glyph: live render ");
        out_append(on ? "on " : "off ");
        glyph_out_cp(cp);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "click") == 0 || strcmp(sub, "tap") == 0) {
        char cp_arg[16];
        uint32_t cp;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 || glyph_parse_cp(cp_arg, &cp) != 0) {
            out_append("Usage: glyph click U+E000\n");
            return;
        }
        if (img_glyph_click(cp, 0u) != 0) {
            out_append("glyph: slot is empty.\n");
            return;
        }
        img_glyph_write_lardd();
        out_append("glyph: clicked ");
        glyph_out_cp(cp);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "clear") == 0 || strcmp(sub, "rm") == 0) {
        char cp_arg[16];
        uint32_t cp;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0 || glyph_parse_cp(cp_arg, &cp) != 0) {
            out_append("Usage: glyph clear U+E000\n");
            return;
        }
        img_glyph_clear(cp);
        img_glyph_write_lardd();
        out_append("glyph: cleared ");
        glyph_out_cp(cp);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "insert") == 0 || strcmp(sub, "note") == 0) {
        char cp_arg[16];
        char file_arg[64];
        char name[64];
        char drv;
        uint32_t cp;
        char bytes[5];
        const uint8_t newline = '\n';
        FsWritableFile* w;
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0) {
            out_append("Usage: glyph insert U+E000 [notes.txt]\n");
            return;
        }
        if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0) {
            file_arg[0] = 'n'; file_arg[1] = 'o'; file_arg[2] = 't'; file_arg[3] = 'e';
            file_arg[4] = 's'; file_arg[5] = '.'; file_arg[6] = 't'; file_arg[7] = 'x';
            file_arg[8] = 't'; file_arg[9] = '\0';
        }
        if (glyph_parse_cp(cp_arg, &cp) != 0 || img_glyph_utf8(cp, bytes) != 3) {
            out_append("glyph: codepoint must be U+E000..U+E0FF.\n");
            return;
        }
        {
            img_glyph_info_t info;
            if (img_glyph_info(cp, &info) != 0) {
                out_append("glyph: slot is empty; load or demo it before insert.\n");
                return;
            }
        }
        resolve_path(file_arg, &drv, name, sizeof(name));
        (void)drv;
        w = fs_open_writable(name);
        if (!w) {
            out_append("glyph: target must be a writable RAM file.\n");
            return;
        }
        if (w->size + 4u > w->cap) {
            out_append("glyph: target file is full.\n");
            return;
        }
        fs_append(w, (const uint8_t*)bytes, 3);
        fs_append(w, &newline, 1);
        out_append("glyph: inserted ");
        glyph_out_cp(cp);
        out_append(" into ");
        out_append(name);
        out_append("\n");
        return;
    }

    if (strcmp(sub, "write") == 0 || strcmp(sub, "map") == 0 || strcmp(sub, "export") == 0) {
        if (img_glyph_write_lardd() == 0) out_append("glyph: wrote glyphmap.lardd\n");
        else out_append("glyph: glyphmap.lardd is missing.\n");
        return;
    }

    glyph_usage();
}

static void cursor_status(void)
{
    gui_cursor_info_t cur;
    img_glyph_info_t info;
    gui_cursor_info(&cur);
    out_append("Cursor ");
    out_append(cur.enabled ? "unicode " : "default ");
    if (cur.enabled) {
        glyph_out_cp(cur.cp);
        out_append(cur.assigned ? " assigned" : " empty");
        if (img_glyph_info(cur.cp, &info) == 0) {
            out_append(" name=");
            out_append(info.name);
            out_append(" live=");
            out_append(info.live ? "on" : "off");
        }
    }
    out_append(" renders=");
    out_append_u32(cur.render_count);
    out_append(" fallbacks=");
    out_append_u32(cur.fallback_count);
    out_append(" err=");
    out_append_u32(cur.last_error);
    out_append("\n");
}

static void cursor_usage(void)
{
    out_append("Usage: cursor [status]|set U+E000|U+E000|off\n");
}

static void cmd_cursor(const char* args)
{
    char sub[16];
    char cp_arg[16];
    uint32_t cp;
    img_glyph_info_t info;
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cursor_status();
        return;
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "default") == 0 || strcmp(sub, "block") == 0) {
        gui_cursor_disable();
        out_append("cursor: default block cursor\n");
        return;
    }
    if (strcmp(sub, "set") == 0 || strcmp(sub, "unicode") == 0 || strcmp(sub, "glyph") == 0) {
        if (vcs_read_word(&args, cp_arg, sizeof(cp_arg)) != 0) {
            cursor_usage();
            return;
        }
    } else {
        uint32_t i = 0;
        while (sub[i] && i + 1u < sizeof(cp_arg)) {
            cp_arg[i] = sub[i];
            i++;
        }
        cp_arg[i] = '\0';
    }
    if (glyph_parse_cp(cp_arg, &cp) != 0 || gui_cursor_set_unicode(cp) != 0) {
        out_append("cursor: codepoint must be U+E000..U+E0FF.\n");
        return;
    }
    out_append("cursor: unicode cursor set to ");
    glyph_out_cp(cp);
    if (img_glyph_info(cp, &info) == 0) {
        out_append(" (");
        out_append(info.name);
        out_append(")\n");
    } else {
        out_append(" (empty slot; run glyph demo/load to give it a picture)\n");
    }
}

static void cmd_copy(const char* args)
{
    char src_arg[64];
    char dst_arg[64];
    char src_name[64];
    char dst_name[64];
    char dst_drive;
    const uint8_t* data;
    uint32_t size;
    FsWritableFile* dst;
    if (vcs_read_word(&args, src_arg, sizeof(src_arg)) != 0 ||
        vcs_read_word(&args, dst_arg, sizeof(dst_arg)) != 0) {
        out_append("Usage: copy src dst\n");
        return;
    }
    if (lsh_read_data_arg(src_arg, &data, &size, src_name, sizeof(src_name)) != 0) {
        out_append("copy: source not found.\n");
        return;
    }
    resolve_path(dst_arg, &dst_drive, dst_name, sizeof(dst_name));
    (void)dst_drive;
    dst = fs_open_writable(dst_name);
    if (!dst) {
        out_append("copy: destination must be a writable RAM file.\n");
        return;
    }
    if (size > dst->cap) {
        out_append("copy: destination too small.\n");
        return;
    }
    fs_write(dst, 0, data, size);
    out_append("Copied ");
    out_append(src_name);
    out_append(" -> ");
    out_append(dst_name);
    out_append(" (");
    out_append_u32(size);
    out_append(" bytes)\n");
}

static void cmd_write_like(const char* args, int append)
{
    char file_arg[64];
    char name[64];
    char drv;
    FsWritableFile* w;
    uint32_t len = 0;
    uint32_t wrote;
    const uint8_t newline = '\n';

    if (vcs_read_word(&args, file_arg, sizeof(file_arg)) != 0) {
        out_append(append ? "Usage: append file text\n" : "Usage: write file text\n");
        return;
    }
    resolve_path(file_arg, &drv, name, sizeof(name));
    (void)drv;
    w = fs_open_writable(name);
    if (!w) {
        out_append(append ? "append: target must be a writable RAM file.\n" :
                            "write: target must be a writable RAM file.\n");
        return;
    }
    while (args[len]) len++;
    if (append) {
        uint32_t need = len + (len ? 1u : 0u);
        if (w->size + need > w->cap) {
            out_append("append: not enough space.\n");
            return;
        }
        wrote = fs_append(w, (const uint8_t*)args, len);
        if (len) wrote += fs_append(w, &newline, 1);
        out_append("Appended ");
    } else {
        uint32_t need = len + (len ? 1u : 0u);
        if (need > w->cap) {
            out_append("write: text too large.\n");
            return;
        }
        wrote = fs_write(w, 0, (const uint8_t*)args, len);
        if (len) wrote += fs_write(w, len, &newline, 1);
        out_append("Wrote ");
    }
    out_append_u32(wrote);
    out_append(" bytes to ");
    out_append(name);
    out_append("\n");
}

static void cmd_write(const char* args)
{
    cmd_write_like(args, 0);
}

static void cmd_append(const char* args)
{
    cmd_write_like(args, 1);
}

static void cmd_set(const char* args)
{
    while (*args == ' ' || *args == '\t') args++;
    if (!*args) {
        for (uint32_t i = 0; i < s_nenv; i++) {
            out_append(s_env_buf[i][0]);
            out_append("=");
            out_append(s_env_buf[i][1]);
            out_append("\n");
        }
        return;
    }
    uint32_t ei = 0;
    char name[LSH_MAX_VAR_LEN];
    while (*args && *args != '=' && ei + 1 < LSH_MAX_VAR_LEN) name[ei++] = *args++;
    name[ei] = '\0';
    if (*args != '=') {
        const char* val = env_get(name);
        if (val) {
            out_append(val);
            out_append("\n");
        }
        return;
    }
    args++;
    if (env_set(name, args) != 0) out_append("set: too many variables\n");
}

static void cmd_echo(const char* args)
{
    while (*args == ' ' || *args == '\t') args++;
    out_append(args);
    out_append("\n");
}

#define LARDX_ARGV_MAX 16

static void report_lardx_result(int r)
{
    if (r == 0) return;
    if (r == -1) out_append("run: file not found or too small.\n");
    else if (r == -2) out_append("run: not a LARDX file.\n");
    else if (r == -3) out_append("run: unsupported LARDX version.\n");
    else if (r == -4) out_append("run: not a user executable (use mkardx --user).\n");
    else if (r == -5) out_append("run: invalid segment count.\n");
    else if (r == -6) out_append("run: truncated file.\n");
    else if (r == -7) out_append("run: segment overflow.\n");
    else if (r == -40) out_append("run: no active Lard container.\n");
    else {
        out_append("run: failed ");
        out_append_i32(r);
        out_append("\n");
    }
}

static void cmd_run(const char* args)
{
    char buf[LSH_MAX_LINE];
    uint32_t bi = 0;
    while (args[bi] && bi < LSH_MAX_LINE - 1) {
        buf[bi] = args[bi];
        bi++;
    }
    buf[bi] = '\0';

    char drv;
    char path[64];
    resolve_path(buf, &drv, path, sizeof(path));
    if (!path[0]) {
        out_append("Usage: run [drive:]file.bosx [arg1 [arg2 ...]]\n");
        return;
    }

    char* rest = buf;
    while (*rest == ' ' || *rest == '\t') rest++;
    while (*rest && *rest != ' ' && *rest != '\t') rest++;
    while (*rest == ' ' || *rest == '\t') rest++;

    const char* argv[LARDX_ARGV_MAX + 1];
    int argc = 1;
    argv[0] = path;

    while (*rest && argc <= LARDX_ARGV_MAX) {
        argv[argc++] = rest;
        while (*rest && *rest != ' ' && *rest != '\t') rest++;
        if (*rest) *rest++ = '\0';
        while (*rest == ' ' || *rest == '\t') rest++;
    }

    int r = s_sandbox_mode ? lardx_run_sandbox(path, argc, argv) :
            (lcontainer_has_active() ? lcontainer_run(NULL, path, argc, argv) : lardx_run(path, argc, argv));
    report_lardx_result(r);
}

static void cmd_lcnt_list(void)
{
    uint32_t count = lcontainer_count();
    if (count == 0) {
        out_append("No Lard containers.\n");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        const char* name;
        uint32_t cap_bits;
        uint32_t runs;
        int active;
        char caps_text[80];
        if (lcontainer_get(i, &name, &cap_bits, &runs, &active) != 0) continue;
        lcontainer_caps_text(cap_bits, caps_text, sizeof(caps_text));
        out_append(active ? "* " : "  ");
        out_append(name);
        out_append(" profile=");
        out_append(lcontainer_profile_name(cap_bits));
        out_append(" caps=");
        out_append(caps_text);
        out_append(" runs=");
        out_append_u32(runs);
        out_append("\n");
    }
}

static void cmd_lcnt_run(const char* args)
{
    char name[LCONTAINER_NAME_MAX];
    char buf[LSH_MAX_LINE];
    char drv;
    char path[64];
    uint32_t bi = 0;
    if (vcs_read_word(&args, name, sizeof(name)) != 0) {
        out_append("Usage: lcnt run name [drive:]file.bosx [args]\n");
        return;
    }
    while (args[bi] && bi < LSH_MAX_LINE - 1) {
        buf[bi] = args[bi];
        bi++;
    }
    buf[bi] = '\0';
    resolve_path(buf, &drv, path, sizeof(path));
    (void)drv;
    if (!path[0]) {
        out_append("Usage: lcnt run name [drive:]file.bosx [args]\n");
        return;
    }

    char* rest = buf;
    while (*rest == ' ' || *rest == '\t') rest++;
    while (*rest && *rest != ' ' && *rest != '\t') rest++;
    while (*rest == ' ' || *rest == '\t') rest++;

    const char* argv[LARDX_ARGV_MAX + 1];
    int argc = 1;
    argv[0] = path;
    while (*rest && argc <= LARDX_ARGV_MAX) {
        argv[argc++] = rest;
        while (*rest && *rest != ' ' && *rest != '\t') rest++;
        if (*rest) *rest++ = '\0';
        while (*rest == ' ' || *rest == '\t') rest++;
    }
    report_lardx_result(lcontainer_run(name, path, argc, argv));
}

static void cmd_lcnt(const char* args)
{
    char sub[16];
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0) {
        out_append("Usage: lcnt list|create|rm|use|exit|run|info\n");
        return;
    }
    if (strcmp(sub, "list") == 0) {
        cmd_lcnt_list();
        return;
    }
    if (strcmp(sub, "info") == 0) {
        out_append("Lard containers isolate user programs with syscall caps.\n");
        out_append("Profiles: sealed, fs, gui, dev, ipc, open.\n");
        cmd_lcnt_list();
        return;
    }
    if (strcmp(sub, "create") == 0) {
        char name[LCONTAINER_NAME_MAX];
        char profile[16];
        uint32_t caps;
        int r;
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: lcnt create name [sealed|fs|gui|dev|ipc|open]\n");
            return;
        }
        if (vcs_read_word(&args, profile, sizeof(profile)) != 0) {
            profile[0] = 's'; profile[1] = 'e'; profile[2] = 'a'; profile[3] = 'l';
            profile[4] = 'e'; profile[5] = 'd'; profile[6] = '\0';
        }
        caps = lcontainer_profile_caps(profile);
        r = lcontainer_create(name, caps);
        if (r == 0) {
            out_append("Created Lard container ");
            out_append(name);
            out_append(" profile=");
            out_append(lcontainer_profile_name(caps));
            out_append("\n");
        } else if (r == -2) out_append("lcnt: container already exists.\n");
        else if (r == -3) out_append("lcnt: container table full.\n");
        else out_append("lcnt: bad container name.\n");
        return;
    }
    if (strcmp(sub, "rm") == 0) {
        char name[LCONTAINER_NAME_MAX];
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: lcnt rm name\n");
            return;
        }
        if (lcontainer_remove(name) == 0) out_append("Removed Lard container.\n");
        else out_append("lcnt: container not found.\n");
        return;
    }
    if (strcmp(sub, "use") == 0) {
        char name[LCONTAINER_NAME_MAX];
        if (vcs_read_word(&args, name, sizeof(name)) != 0) {
            out_append("Usage: lcnt use name\n");
            return;
        }
        if (lcontainer_use(name) == 0) {
            out_append("Entered Lard container ");
            out_append(name);
            out_append(". run now uses its caps.\n");
        } else out_append("lcnt: container not found.\n");
        return;
    }
    if (strcmp(sub, "exit") == 0) {
        lcontainer_exit();
        out_append("Left Lard container.\n");
        return;
    }
    if (strcmp(sub, "run") == 0) {
        cmd_lcnt_run(args);
        return;
    }
    out_append("lcnt: unknown subcommand.\n");
}

static void cmd_more(const char* args)
{
    (void)args;
    const char* input = lsh_stdin();
    if (!input || !input[0]) {
        out_append("more: no input (use with pipe, e.g. type file | more)\n");
        return;
    }
    const char* p = input;
    uint32_t lines = 0;
    while (*p && lines < 100) {
        while (*p && *p != '\n') { out_append_char(*p); p++; }
        if (*p == '\n') { out_append_char('\n'); p++; lines++; }
    }
    if (*p) out_append("-- more --\n");
}

#define BOSL_MAGIC 0x4C534F42u  /* "BOSL" LE */

static void cmd_bosl(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: bosl [drive:]file.bosli\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    const uint8_t* data = NULL;
    uint32_t size = 0;
    if (f) {
        data = f->data;
        size = f->size;
    } else if (drive_to_fs(drv) == 1) {
        FsWritableFile* w = fs_open_writable(name);
        if (w) { data = w->data; size = w->size; }
    }
    if (!data || size < 8) {
        out_append("File not found or too small.\n");
        return;
    }
    uint32_t mag = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    if (mag != BOSL_MAGIC) {
        out_append("Not a BOSL bytecode file.\n");
        return;
    }
    int r = bosl_vm_run_jit_io(data, size, lsh_putc, NULL);
    if (r != 0) out_append("BOSL execution failed.\n");
}

static void cmd_lil(const char* args)
{
    char drv;
    char name[64];
    static char src[8192];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: lil [drive:]file.lil\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    const uint8_t* data = f ? f->data : (w ? w->data : NULL);
    uint32_t size = f ? f->size : (w ? w->size : 0);
    if (!data || size == 0) {
        out_append("LIL file not found.\n");
        return;
    }
    if (size >= sizeof(src)) {
        out_append("LIL source too large.\n");
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        src[i] = (char)data[i];
    }
    src[size] = 0;
    int r = lil_run(src, lsh_putc, NULL);
    if (r != 0) {
        out_append("LIL execution failed: ");
        out_append_i32(r);
        out_append("\n");
    }
}

#define DVM_MAGIC 0x004D5644u  /* "DVM\0" LE */

static void cmd_lafvm(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: lafvm [drive:]file.dvm\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    const uint8_t* data = f ? f->data : (w ? w->data : NULL);
    uint32_t size = f ? f->size : (w ? w->size : 0);
    if (!data || size == 0) {
        out_append("File not found.\n");
        return;
    }
    int r;
    if (size >= 4 && (data[0] == 'D' && data[1] == 'V' && data[2] == 'M')) {
        r = lafillo_vm_run_io(data, size, lsh_putc, NULL);
    } else {
        static char src[4096];
        uint32_t n = size < sizeof(src) - 1 ? size : sizeof(src) - 1;
        for (uint32_t i = 0; i < n; i++) src[i] = (char)data[i];
        src[n] = '\0';
        r = lafillo_vm_asm_eval(src, lsh_putc, NULL);
    }
    if (r != 0) out_append("Lafillo VM execution failed.\n");
}

#define OVM_MAGIC 0x004D564Fu  /* "OVM\0" LE */

static void cmd_osvm(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: osvm [drive:]file.ovm\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    const uint8_t* data = f ? f->data : (w ? w->data : NULL);
    uint32_t size = f ? f->size : (w ? w->size : 0);
    if (!data || size == 0) {
        out_append("File not found.\n");
        return;
    }
    int r;
    if (size >= 4 && data[0] == 'O' && data[1] == 'V' && data[2] == 'M') {
        r = os_vm_run_io(data, size, lsh_putc, NULL);
    } else {
        static char src[2048];
        uint32_t n = size < sizeof(src) - 1 ? size : sizeof(src) - 1;
        for (uint32_t i = 0; i < n; i++) src[i] = (char)data[i];
        src[n] = '\0';
        r = os_vm_asm_eval(src, lsh_putc, NULL);
    }
    if (r != 0) out_append("OSVM execution failed.\n");
}

static void lsh_putc(char c, void* user)
{
    (void)user;
    out_append_char(c);
}

/* Parse hex (0xNN) or decimal number. Sets *endp to char after parsed number. */
static int parse_port_or_val(const char* p, uint32_t* out, const char** endp)
{
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        uint32_t v = 0;
        for (; *p; p++) {
            if (*p >= '0' && *p <= '9') v = (v << 4) + (*p - '0');
            else if (*p >= 'a' && *p <= 'f') v = (v << 4) + (*p - 'a' + 10);
            else if (*p >= 'A' && *p <= 'F') v = (v << 4) + (*p - 'A' + 10);
            else break;
        }
        *out = v;
        *endp = p;
        return 0;
    }
    uint32_t v = 0;
    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = v;
    *endp = p;
    return 0;
}

static void cmd_asm_(const char* args)
{
    if (!s_in_sum_mode) {
        out_append("asm_: SUM mode required. Type 'sum' first.\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    /* inb port | outb port val | inw port | outw port val | inl port | outl port val */
    if ((args[0] == 'i' || args[0] == 'I') && (args[1] == 'n' || args[1] == 'N') &&
        (args[2] == 'b' || args[2] == 'B') && (args[3] == ' ' || args[3] == '\t' || args[3] == '\0')) {
        uint32_t port;
        const char* ep;
        if (parse_port_or_val(args + 3, &port, &ep) != 0 || port > 0xFFFF) {
            out_append("asm_: inb <port> (0-65535)\n");
            return;
        }
        uint8_t v = inb((uint16_t)port);
        char buf[16];
        uint32_t i = 0;
        if (v >= 100) { buf[i++] = (char)('0' + v / 100); v %= 100; }
        if (v >= 10 || i) { buf[i++] = (char)('0' + v / 10); v %= 10; }
        buf[i++] = (char)('0' + v);
        buf[i] = '\0';
        out_append(buf);
        out_append("\n");
    } else if ((args[0] == 'o' || args[0] == 'O') && (args[1] == 'u' || args[1] == 'U') &&
               (args[2] == 't' || args[2] == 'T') && (args[3] == 'b' || args[3] == 'B') &&
               (args[4] == ' ' || args[4] == '\t')) {
        uint32_t port, val;
        const char* rest = args + 4;
        const char* ep;
        if (parse_port_or_val(rest, &port, &ep) != 0 || port > 0xFFFF) {
            out_append("asm_: outb <port> <val>\n");
            return;
        }
        rest = ep;
        if (parse_port_or_val(rest, &val, &ep) != 0 || val > 0xFF) {
            out_append("asm_: outb <port> <val>\n");
            return;
        }
        outb((uint16_t)port, (uint8_t)(val & 0xFF));
    } else if ((args[0] == 'i' || args[0] == 'I') && (args[1] == 'n' || args[1] == 'N') &&
               (args[2] == 'w' || args[2] == 'W') && (args[3] == ' ' || args[3] == '\t' || args[3] == '\0')) {
        uint32_t port;
        const char* ep;
        if (parse_port_or_val(args + 3, &port, &ep) != 0 || port > 0xFFFF) {
            out_append("asm_: inw <port>\n");
            return;
        }
        uint16_t v = inw((uint16_t)port);
        char buf[16];
        uint32_t i = 0;
        if (v >= 10000) { buf[i++] = (char)('0' + v / 10000); v %= 10000; }
        if (v >= 1000) { buf[i++] = (char)('0' + v / 1000); v %= 1000; }
        if (v >= 100) { buf[i++] = (char)('0' + v / 100); v %= 100; }
        if (v >= 10 || i) { buf[i++] = (char)('0' + v / 10); v %= 10; }
        buf[i++] = (char)('0' + v);
        buf[i] = '\0';
        out_append(buf);
        out_append("\n");
    } else if ((args[0] == 'o' || args[0] == 'O') && (args[1] == 'u' || args[1] == 'U') &&
               (args[2] == 't' || args[2] == 'T') && (args[3] == 'w' || args[3] == 'W') &&
               (args[4] == ' ' || args[4] == '\t')) {
        uint32_t port, val;
        const char* rest = args + 4;
        const char* ep;
        if (parse_port_or_val(rest, &port, &ep) != 0 || port > 0xFFFF) {
            out_append("asm_: outw <port> <val>\n");
            return;
        }
        rest = ep;
        if (parse_port_or_val(rest, &val, &ep) != 0 || val > 0xFFFF) {
            out_append("asm_: outw <port> <val>\n");
            return;
        }
        outw((uint16_t)port, (uint16_t)(val & 0xFFFF));
    } else if ((args[0] == 'i' || args[0] == 'I') && (args[1] == 'n' || args[1] == 'N') &&
               (args[2] == 'l' || args[2] == 'L') && (args[3] == ' ' || args[3] == '\t' || args[3] == '\0')) {
        uint32_t port;
        const char* ep;
        if (parse_port_or_val(args + 3, &port, &ep) != 0 || port > 0xFFFF) {
            out_append("asm_: inl <port>\n");
            return;
        }
        uint32_t v = inl((uint16_t)port);
        char buf[16];
        uint32_t i = 0;
        uint32_t m = v;
        if (m >= 1000000000) { buf[i++] = (char)('0' + m / 1000000000); m %= 1000000000; }
        if (m >= 100000000) { buf[i++] = (char)('0' + m / 100000000); m %= 100000000; }
        if (m >= 10000000) { buf[i++] = (char)('0' + m / 10000000); m %= 10000000; }
        if (m >= 1000000) { buf[i++] = (char)('0' + m / 1000000); m %= 1000000; }
        if (m >= 100000) { buf[i++] = (char)('0' + m / 100000); m %= 100000; }
        if (m >= 10000) { buf[i++] = (char)('0' + m / 10000); m %= 10000; }
        if (m >= 1000) { buf[i++] = (char)('0' + m / 1000); m %= 1000; }
        if (m >= 100) { buf[i++] = (char)('0' + m / 100); m %= 100; }
        if (m >= 10 || i) { buf[i++] = (char)('0' + m / 10); m %= 10; }
        buf[i++] = (char)('0' + m);
        buf[i] = '\0';
        out_append(buf);
        out_append("\n");
    } else if ((args[0] == 'o' || args[0] == 'O') && (args[1] == 'u' || args[1] == 'U') &&
               (args[2] == 't' || args[2] == 'T') && (args[3] == 'l' || args[3] == 'L') &&
               (args[4] == ' ' || args[4] == '\t')) {
        uint32_t port, val;
        const char* rest = args + 4;
        const char* ep;
        if (parse_port_or_val(rest, &port, &ep) != 0 || port > 0xFFFF) {
            out_append("asm_: outl <port> <val>\n");
            return;
        }
        rest = ep;
        if (parse_port_or_val(rest, &val, &ep) != 0) {
            out_append("asm_: outl <port> <val>\n");
            return;
        }
        outl((uint16_t)port, val);
    } else {
        out_append("asm_: inb|outb|inw|outw|inl|outl <port> [val]\n");
    }
}

static void cmd_sum(const char* args)
{
    (void)args;
    s_in_sum_mode = 1;
    out_append("SUM (ring 0) enabled. asm_ for hardware I/O. exitsum to leave.\n");
}

static void cmd_exitsum(const char* args)
{
    (void)args;
    s_in_sum_mode = 0;
    out_append("Exited SUM.\n");
}

static void cmd_sandbox(const char* args)
{
    (void)args;
    s_sandbox_mode = 1;
    out_append("Sandbox ON. run executes with restricted syscalls. exitsandbox to leave.\n");
}

static void cmd_exitsandbox(const char* args)
{
    (void)args;
    s_sandbox_mode = 0;
    out_append("Sandbox OFF.\n");
}

static void cmd_larsh(const char* args)
{
    char drv;
    char name[64];
    resolve_path(args, &drv, name, sizeof(name));
    if (!name[0]) {
        out_append("Usage: larsh [drive:]file.larsh\n");
        return;
    }
    const FsFile* f = lsh_open_read(drv, name);
    FsWritableFile* w = (drive_to_fs(drv) == 1) ? fs_open_writable(name) : NULL;
    if (!f && !w) {
        out_append("larsh: file not found.\n");
        return;
    }
    gui_larsh_play(name);
    out_append("Playing LARSH. Switch to Gallery tab to view.\n");
}

static int cfgsh_is_status_word(const char* value)
{
    return strcmp(value, "status") == 0 || strcmp(value, "info") == 0 || strcmp(value, "?") == 0;
}

static int cfgsh_bool_value(const char* value, int* out)
{
    if (strcmp(value, "on") == 0 || strcmp(value, "enable") == 0 ||
        strcmp(value, "enabled") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out = 1;
        return 0;
    }
    if (strcmp(value, "off") == 0 || strcmp(value, "disable") == 0 ||
        strcmp(value, "disabled") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static const char* cfgsh_style_value(const char* value)
{
    if (strcmp(value, "1") == 0) return "win";
    if (strcmp(value, "2") == 0) return "linux";
    if (strcmp(value, "3") == 0) return "mac";
    if (strcmp(value, "win") == 0 || strcmp(value, "windows") == 0) return "win";
    if (strcmp(value, "linux") == 0 || strcmp(value, "gnome") == 0 || strcmp(value, "kde") == 0) return "linux";
    if (strcmp(value, "mac") == 0 || strcmp(value, "macos") == 0) return "mac";
    return NULL;
}

static const char* cfgsh_layout_value(const char* value)
{
    if (strcmp(value, "1") == 0) return "float";
    if (strcmp(value, "2") == 0) return "tile";
    if (strcmp(value, "3") == 0) return "stack";
    if (strcmp(value, "float") == 0 || strcmp(value, "floating") == 0) return "float";
    if (strcmp(value, "tile") == 0 || strcmp(value, "tiling") == 0) return "tile";
    if (strcmp(value, "stack") == 0 || strcmp(value, "stacked") == 0) return "stack";
    return NULL;
}

static const char* cfgsh_focus_value(const char* value)
{
    if (strcmp(value, "1") == 0) return "gui";
    if (strcmp(value, "2") == 0) return "term";
    if (strcmp(value, "3") == 0) return "info";
    if (strcmp(value, "gui") == 0 || strcmp(value, "de") == 0 || strcmp(value, "wm") == 0) return "gui";
    if (strcmp(value, "term") == 0 || strcmp(value, "terminal") == 0 || strcmp(value, "shell") == 0) return "term";
    if (strcmp(value, "info") == 0 || strcmp(value, "status") == 0) return "info";
    return NULL;
}

static const char* cfgsh_boot_value(const char* value)
{
    if (strcmp(value, "1") == 0) return "normal";
    if (strcmp(value, "2") == 0) return "safe";
    if (strcmp(value, "3") == 0) return "netoff";
    if (strcmp(value, "4") == 0) return "dev";
    if (strcmp(value, "5") == 0) return "awakening";
    if (strcmp(value, "awake") == 0) return "awakening";
    if (strcmp(value, "normal") == 0 || strcmp(value, "safe") == 0 ||
        strcmp(value, "netoff") == 0 || strcmp(value, "dev") == 0 ||
        strcmp(value, "awakening") == 0) return value;
    return NULL;
}

static void cfgsh_help(void)
{
    out_append("CFGSH settings shell\n");
    out_append("  cfgsh              enter settings shell (CFG# prompt)\n");
    out_append("  exitcfg            leave settings shell\n");
    out_append("  setting value      e.g. awake on, style 2, layout 3, boot 4\n");
    out_append("Settings:\n");
    out_append("  awake on|off       next boot fast-surface mode\n");
    out_append("  exgui on|off       extended desktop layer\n");
    out_append("  style 1|2|3        win|linux|mac\n");
    out_append("  layout 1|2|3       float|tile|stack\n");
    out_append("  split on|off       EXEXGUI sketch split\n");
    out_append("  buddy on|off|mood  roaming easygoing assistant\n");
    out_append("  bugeye on|off      visual bug monitor\n");
    out_append("  ltheme name        classic|contrast|night|amber\n");
    out_append("  rollback snap|apply settings snapshot restore\n");
    out_append("  pane 1|2|3         gui|term|info focus\n");
    out_append("  sram on|off        screen scratch RAM\n");
    out_append("  http 1|2           GET|POST mode\n");
    out_append("  boot 1..5          normal|safe|netoff|dev|awakening\n");
    out_append("  priority 0..10     default background task priority\n");
    out_append("  sandbox on|off     default LARDX sandbox run mode\n");
    out_append("  sum on|off         ring-0 full-control mode\n");
    out_append("  sync               persist writable settings/files\n");
}

static void cfgsh_status(void)
{
    bootprof_info_t bp;
    awake_info_t aw;
    exgui_info_t eg;
    exexgui_info_t xx;
    gui_screenram_info_t sr;
    taskprio_info_t tp;
    lassist_info_t buddy;
    lardkit_bugeye_info_t be;
    lardkit_theme_info_t th;
    lardkit_rollback_info_t rb;
    bootprof_info(&bp);
    awake_info(&aw);
    exgui_info(&eg);
    exexgui_info(&xx);
    gui_screenram_info(&sr);
    taskprio_info(&tp);
    lassist_info(&buddy);
    lardkit_bugeye_info(&be);
    lardkit_theme_info(&th);
    lardkit_rollback_info(&rb);
    out_append("CFGSH status\n");
    out_append("  boot=");
    out_append(bp.name);
    out_append(" awake=");
    out_append(bp.awakening_mode ? "on" : "off");
    out_append(" loader=");
    out_append(aw.enabled ? (aw.done ? "done" : "background") : "off");
    out_append("\n  exgui=");
    out_append(eg.enabled ? "on" : "off");
    out_append(" style=");
    out_append(exgui_style_name(eg.style));
    out_append(" layout=");
    out_append(exgui_layout_name(eg.layout));
    out_append(" split=");
    out_append(xx.enabled ? "on" : "off");
    out_append(" pane=");
    out_append(exexgui_focus_name(xx.focus));
    out_append(" buddy=");
    out_append(buddy.enabled ? "on" : "off");
    out_append(" bugeye=");
    out_append(be.enabled ? "on" : "off");
    out_append(" theme=");
    out_append(th.name);
    out_append("\n  sram=");
    out_append(sr.enabled ? "on" : "off");
    out_append(" http=");
    out_append(gui_http_post_mode() ? "POST" : "GET");
    out_append(" priority=");
    out_append_i32(tp.default_priority);
    out_append("\n  sandbox=");
    out_append(s_sandbox_mode ? "on" : "off");
    out_append(" sum=");
    out_append(s_in_sum_mode ? "on" : "off");
    out_append(" cfgsh=");
    out_append(s_cfgsh_mode ? "on" : "off");
    out_append(" rollback=");
    out_append(rb.valid ? rb.label : "empty");
    out_append("\n");
}

static void cfgsh_set_boot(const char* profile)
{
    if (bootprof_set(profile) == 0) {
        out_append("cfgsh: boot=");
        out_append(profile);
        out_append(" (run sync to persist)\n");
    } else {
        out_append("cfgsh: boot profile failed.\n");
    }
}

static int cfgsh_apply(const char* setting, const char* args)
{
    char value[32];
    const char* rest = args ? args : "";
    int have_value = vcs_read_word(&rest, value, sizeof(value)) == 0;
    int on = 0;
    const char* mapped;

    if (!setting || !setting[0]) return 0;
    if (have_value && !cfgsh_is_status_word(value) &&
        strcmp(setting, "rollback") != 0 && strcmp(setting, "undo") != 0) {
        (void)lardkit_snapshot(setting);
    }

    if (strcmp(setting, "awake") == 0 || strcmp(setting, "awakening") == 0 || strcmp(setting, "fastboot") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_awake_status(); return 1; }
        if (cfgsh_bool_value(value, &on) == 0) cmd_awake(on ? "on" : "off");
        else out_append("Usage: awake on|off\n");
        return 1;
    }
    if (strcmp(setting, "exgui") == 0 || strcmp(setting, "desktop") == 0 || strcmp(setting, "de") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_exgui_status(); return 1; }
        if (cfgsh_bool_value(value, &on) == 0) {
            exgui_enable(on);
            out_append(on ? "cfgsh: exgui on\n" : "cfgsh: exgui off\n");
            return 1;
        }
        mapped = cfgsh_style_value(value);
        if (mapped && exgui_set_style(mapped) == 0) { cmd_exgui_status(); return 1; }
        mapped = cfgsh_layout_value(value);
        if (mapped && exgui_set_layout(mapped) == 0) { cmd_exgui_status(); return 1; }
        out_append("Usage: exgui on|off or style/layout value\n");
        return 1;
    }
    if (strcmp(setting, "style") == 0 || strcmp(setting, "theme") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_exgui_status(); return 1; }
        mapped = cfgsh_style_value(value);
        if (mapped && exgui_set_style(mapped) == 0) cmd_exgui_status();
        else out_append("Usage: style 1|2|3 (win|linux|mac)\n");
        return 1;
    }
    if (strcmp(setting, "layout") == 0 || strcmp(setting, "wm") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_exgui_status(); return 1; }
        mapped = cfgsh_layout_value(value);
        if (mapped && exgui_set_layout(mapped) == 0) cmd_exgui_status();
        else out_append("Usage: layout 1|2|3 (float|tile|stack)\n");
        return 1;
    }
    if (strcmp(setting, "split") == 0 || strcmp(setting, "exexgui") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_exexgui_status(); return 1; }
        if (cfgsh_bool_value(value, &on) == 0) {
            exexgui_enable(on);
            out_append(on ? "cfgsh: split on\n" : "cfgsh: split off\n");
        } else {
            out_append("Usage: split on|off\n");
        }
        return 1;
    }
    if (strcmp(setting, "buddy") == 0 || strcmp(setting, "assistant") == 0 ||
        strcmp(setting, "helper") == 0 || strcmp(setting, "lardbuddy") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_buddy_status(); return 1; }
        if (cfgsh_bool_value(value, &on) == 0) {
            lassist_enable(on);
            out_append(on ? "cfgsh: buddy on\n" : "cfgsh: buddy off\n");
        } else if (strcmp(value, "joke") == 0) {
            lassist_joke();
            cmd_buddy_status();
        } else if (strcmp(value, "next") == 0 || strcmp(value, "tip") == 0) {
            lassist_next(0);
            cmd_buddy_status();
        } else if (lassist_set_mood(value) == 0) {
            lassist_enable(1);
            cmd_buddy_status();
        } else {
            out_append("Usage: buddy on|off|joke|next|calm|funny|strict|silent\n");
        }
        return 1;
    }
    if (strcmp(setting, "bugeye") == 0 || strcmp(setting, "bug") == 0 || strcmp(setting, "visualbug") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_bugeye_status(); return 1; }
        if (cfgsh_bool_value(value, &on) == 0) {
            lardkit_bugeye_enable(on);
            if (on) (void)lardkit_bugeye_scan();
            cmd_bugeye_status();
        } else if (strcmp(value, "scan") == 0) {
            (void)lardkit_bugeye_scan();
            cmd_bugeye_status();
        } else {
            out_append("Usage: bugeye on|off|scan\n");
        }
        return 1;
    }
    if (strcmp(setting, "ltheme") == 0 || strcmp(setting, "theme2") == 0 || strcmp(setting, "colors") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_ltheme_status(); return 1; }
        if (lardkit_theme_use(value) == 0) cmd_ltheme_status();
        else out_append("Usage: ltheme classic|contrast|night|amber\n");
        return 1;
    }
    if (strcmp(setting, "rollback") == 0 || strcmp(setting, "undo") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_rollback_status(); return 1; }
        if (strcmp(value, "snap") == 0 || strcmp(value, "save") == 0) {
            (void)lardkit_snapshot("cfgsh");
            out_append("cfgsh: rollback snapshot saved\n");
        } else if (strcmp(value, "apply") == 0 || strcmp(value, "last") == 0 || strcmp(value, "restore") == 0) {
            out_append(lardkit_rollback_apply() == 0 ? "cfgsh: rollback applied\n" : "cfgsh: no rollback snapshot\n");
        } else {
            out_append("Usage: rollback snap|apply\n");
        }
        return 1;
    }
    if (strcmp(setting, "pane") == 0 || strcmp(setting, "focus") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_exexgui_status(); return 1; }
        mapped = cfgsh_focus_value(value);
        if (mapped && exexgui_set_focus(mapped) == 0) cmd_exexgui_status();
        else out_append("Usage: pane 1|2|3 (gui|term|info)\n");
        return 1;
    }
    if (strcmp(setting, "sram") == 0 || strcmp(setting, "screenram") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_sram_status(); return 1; }
        if (cfgsh_bool_value(value, &on) == 0) {
            if (on) (void)gui_screenram_enable(1);
            else gui_screenram_enable(0);
            cmd_sram_status();
        } else if (strcmp(value, "clear") == 0) {
            gui_screenram_clear();
            out_append("cfgsh: sram cleared\n");
        } else {
            out_append("Usage: sram on|off|clear\n");
        }
        return 1;
    }
    if (strcmp(setting, "http") == 0 || strcmp(setting, "method") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) {
            out_append("cfgsh: http=");
            out_append(gui_http_post_mode() ? "POST\n" : "GET\n");
            return 1;
        }
        if (strcmp(value, "get") == 0 || strcmp(value, "1") == 0 || strcmp(value, "off") == 0) {
            gui_http_set_post_mode(0);
            out_append("cfgsh: http=GET\n");
        } else if (strcmp(value, "post") == 0 || strcmp(value, "2") == 0 || strcmp(value, "on") == 0) {
            gui_http_set_post_mode(1);
            out_append("cfgsh: http=POST\n");
        } else {
            out_append("Usage: http get|post or 1|2\n");
        }
        return 1;
    }
    if (strcmp(setting, "boot") == 0 || strcmp(setting, "bootprof") == 0 || strcmp(setting, "profile") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) { cmd_bootprof_status(); return 1; }
        mapped = cfgsh_boot_value(value);
        if (mapped) cfgsh_set_boot(mapped);
        else out_append("Usage: boot 1..5 (normal|safe|netoff|dev|awakening)\n");
        return 1;
    }
    if (strcmp(setting, "priority") == 0 || strcmp(setting, "prio") == 0 ||
        strcmp(setting, "taskprio") == 0 || strcmp(setting, "defaultprio") == 0) {
        uint32_t pr;
        const char* p = args ? args : "";
        if (!have_value || cfgsh_is_status_word(value)) {
            out_append("cfgsh: priority=");
            out_append_i32(taskprio_default_priority());
            out_append("\n");
            return 1;
        }
        if (vcs_parse_u32(&p, &pr) == 0 && pr <= (uint32_t)TASKPRIO_MAX) {
            taskprio_set_default((int32_t)pr);
            out_append("cfgsh: priority=");
            out_append_u32(pr);
            out_append("\n");
        } else {
            out_append("Usage: priority 0..10\n");
        }
        return 1;
    }
    if (strcmp(setting, "sandbox") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) {
            out_append("cfgsh: sandbox=");
            out_append(s_sandbox_mode ? "on\n" : "off\n");
            return 1;
        }
        if (cfgsh_bool_value(value, &on) == 0) {
            s_sandbox_mode = on ? 1 : 0;
            out_append(on ? "cfgsh: sandbox on\n" : "cfgsh: sandbox off\n");
        } else {
            out_append("Usage: sandbox on|off\n");
        }
        return 1;
    }
    if (strcmp(setting, "sum") == 0 || strcmp(setting, "ring0") == 0) {
        if (!have_value || cfgsh_is_status_word(value)) {
            out_append("cfgsh: sum=");
            out_append(s_in_sum_mode ? "on\n" : "off\n");
            return 1;
        }
        if (cfgsh_bool_value(value, &on) == 0) {
            if (on) cmd_sum("");
            else cmd_exitsum("");
        } else {
            out_append("Usage: sum on|off\n");
        }
        return 1;
    }
    if (strcmp(setting, "sync") == 0 || strcmp(setting, "save") == 0 || strcmp(setting, "persist") == 0) {
        cmd_fssave("");
        return 1;
    }
    return 0;
}

static void cmd_cfgsh(const char* args)
{
    char sub[32];
    if (!args) args = "";
    if (vcs_read_word(&args, sub, sizeof(sub)) != 0 ||
        strcmp(sub, "on") == 0 || strcmp(sub, "enter") == 0 || strcmp(sub, "shell") == 0) {
        s_cfgsh_mode = 1;
        out_append("CFGSH ON. Use setting value; exitcfg to leave.\n");
        cfgsh_help();
        return;
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "exit") == 0 || strcmp(sub, "quit") == 0) {
        s_cfgsh_mode = 0;
        out_append("CFGSH OFF.\n");
        return;
    }
    if (strcmp(sub, "help") == 0 || strcmp(sub, "list") == 0 || strcmp(sub, "?") == 0) {
        cfgsh_help();
        return;
    }
    if (strcmp(sub, "status") == 0 || strcmp(sub, "info") == 0) {
        cfgsh_status();
        return;
    }
    if (strcmp(sub, "test") == 0 || strcmp(sub, "selftest") == 0) {
        out_append(lsh_cfgsh_selftest() == 0 ? "cfgsh: selftest OK\n" : "cfgsh: selftest failed\n");
        return;
    }
    if (!cfgsh_apply(sub, args)) {
        out_append("cfgsh: unknown setting. Try cfgsh help.\n");
    }
}

int lsh_cfgsh_selftest(void)
{
    int on = -1;
    const char* s;
    if (cfgsh_bool_value("on", &on) != 0 || on != 1) return -1;
    if (cfgsh_bool_value("0", &on) != 0 || on != 0) return -2;
    s = cfgsh_style_value("2");
    if (!s || strcmp(s, "linux") != 0) return -3;
    s = cfgsh_layout_value("3");
    if (!s || strcmp(s, "stack") != 0) return -4;
    s = cfgsh_focus_value("1");
    if (!s || strcmp(s, "gui") != 0) return -5;
    s = cfgsh_boot_value("5");
    if (!s || strcmp(s, "awakening") != 0) return -6;
    if (cfgsh_style_value("bad") != NULL) return -7;
    return 0;
}

static int run_lsh_cmd(const char* name, const char* argv)
{
    (void)argv;
    char path[80];
    uint32_t i = 0;
    while (name[i] && i < 76) {
        path[i] = name[i];
        i++;
    }
    path[i] = '\0';
    if (i < 4 || path[i-4] != '.' || path[i-3] != 'l' || path[i-2] != 's' || path[i-1] != 'h') {
        if (i + 4 < 80) {
            path[i++] = '.';
            path[i++] = 'l';
            path[i++] = 's';
            path[i++] = 'h';
            path[i] = '\0';
        }
    }

    const FsFile* f = fs_open(path);
    if (!f || f->size < 10) return 0;

    const uint8_t* d = f->data;
    uint32_t mag = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (mag != LSH_MAGIC) return 0;
    if (d[4] != 1) return 0;
    uint8_t nlen = d[5];
    if (nlen >= 32 || 6u + (uint32_t)nlen + 2u >= f->size) return 0;
    uint8_t typ = d[6 + nlen];
    const uint8_t* payload = d + 6 + nlen + 1;
    uint32_t pay_len = f->size - (6 + nlen + 1);
    if (typ == 0) {
        int r = bosl_vm_run_jit_io(payload, pay_len, lsh_putc, NULL);
        if (r != 0) out_append("Command failed.\n");
        return 1;
    }
    return 0;
}

static void parse_and_run(const char* cmd, const char* args)
{
    if (!cmd || !cmd[0]) return;
    lardkit_trace_event("shell", cmd, 0);
    if (strcmp(cmd, "oslink") == 0 || strcmp(cmd, "oschat") == 0) lardkit_trace_event("oslink", cmd, 0);
    if (strcmp(cmd, "task") == 0 || strcmp(cmd, "tasks") == 0 || strcmp(cmd, "tasktop") == 0 ||
        strcmp(cmd, "prio") == 0 || strcmp(cmd, "priority") == 0) lardkit_trace_event("taskprio", cmd, 0);
    if (strcmp(cmd, "exgui") == 0 || strcmp(cmd, "exexgui") == 0 || strcmp(cmd, "ltheme") == 0 ||
        strcmp(cmd, "glyph") == 0 || strcmp(cmd, "glyphs") == 0 || strcmp(cmd, "uglyph") == 0 ||
        strcmp(cmd, "picglyph") == 0) lardkit_trace_event("gui", cmd, 0);

    if (s_cfgsh_mode) {
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exitcfg") == 0) {
            s_cfgsh_mode = 0;
            out_append("CFGSH OFF.\n");
            return;
        }
        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0 || strcmp(cmd, "list") == 0) {
            cfgsh_help();
            return;
        }
        if (strcmp(cmd, "status") == 0 || strcmp(cmd, "info") == 0) {
            cfgsh_status();
            return;
        }
        if (strcmp(cmd, "sync") == 0) {
            cmd_fssave(args);
            return;
        }
        if (strcmp(cmd, "cfgsh") == 0 || strcmp(cmd, "cfg") == 0 || strcmp(cmd, "settings") == 0) {
            cmd_cfgsh(args);
            return;
        }
        if (cfgsh_apply(cmd, args)) return;
        out_append("cfgsh: unknown setting. Try help or exitcfg.\n");
        return;
    }

    if (strcmp(cmd, "magic") == 0) {
        if (s_magic_depth > 0) {
            out_append("magic: nested magic ignored.\n");
            return;
        }
        cmd_magic(args);
        return;
    }
    if (strcmp(cmd, "help") == 0 || (cmd[0] == '?' && cmd[1] == '\0')) { cmd_help(args); return; }
    if (strcmp(cmd, "control") == 0) { cmd_control(args); return; }
    if (strcmp(cmd, "status") == 0) { cmd_status(args); return; }
    if (strcmp(cmd, "time") == 0 || strcmp(cmd, "lardtime") == 0 || strcmp(cmd, "ltime") == 0) { cmd_lardtime_mode(args, "now"); return; }
    if (strcmp(cmd, "date") == 0) { cmd_lardtime_mode(args, "solar"); return; }
    if (strcmp(cmd, "lunar") == 0) { cmd_lardtime_mode(args, "lunar"); return; }
    if (strcmp(cmd, "dangun") == 0) { cmd_lardtime_mode(args, "dangun"); return; }
    if (strcmp(cmd, "cfgsh") == 0 || strcmp(cmd, "cfg") == 0 || strcmp(cmd, "settings") == 0) { cmd_cfgsh(args); return; }
    if (strcmp(cmd, "exitcfg") == 0) { s_cfgsh_mode = 0; out_append("CFGSH OFF.\n"); return; }
    if (strcmp(cmd, "buddy") == 0 || strcmp(cmd, "assistant") == 0 || strcmp(cmd, "lardbuddy") == 0) { cmd_buddy(args); return; }
    if (strcmp(cmd, "mode") == 0) { cmd_mode(args); return; }
    if (strcmp(cmd, "trace") == 0 || strcmp(cmd, "lardtrace") == 0) { cmd_trace(args); return; }
    if (strcmp(cmd, "netwatch") == 0) { cmd_netwatch(args); return; }
    if (strcmp(cmd, "journal") == 0) { cmd_journal(args); return; }
    if (strcmp(cmd, "oslink") == 0) { cmd_oslink(args); return; }
    if (strcmp(cmd, "oschat") == 0) { cmd_oschat(args); return; }
    if (strcmp(cmd, "exgui") == 0) { cmd_exgui(args); return; }
    if (strcmp(cmd, "exexgui") == 0) { cmd_exexgui(args); return; }
    if (strcmp(cmd, "lguilib") == 0) { cmd_lguilib(args); return; }
    if (strcmp(cmd, "ltheme") == 0) { cmd_ltheme(args); return; }
    if (strcmp(cmd, "glyph") == 0 || strcmp(cmd, "glyphs") == 0 || strcmp(cmd, "uglyph") == 0 || strcmp(cmd, "picglyph") == 0) { cmd_glyph(args); return; }
    if (strcmp(cmd, "cursor") == 0 || strcmp(cmd, "ucursor") == 0) { cmd_cursor(args); return; }
    if (strcmp(cmd, "awake") == 0 || strcmp(cmd, "awakening") == 0) { cmd_awake(args); return; }
    if (strcmp(cmd, "awakemon") == 0) { cmd_awakemon(args); return; }
    if (strcmp(cmd, "task") == 0 || strcmp(cmd, "tasks") == 0) { cmd_task(args); return; }
    if (strcmp(cmd, "tasktop") == 0) { cmd_tasktop(args); return; }
    if (strcmp(cmd, "bootprof") == 0) { cmd_bootprof(args); return; }
    if (strcmp(cmd, "bootmap") == 0) { cmd_bootmap(args); return; }
    if (strcmp(cmd, "bootreplay") == 0) { cmd_bootreplay(args); return; }
    if (strcmp(cmd, "postbaseline") == 0 || strcmp(cmd, "postbase") == 0) { cmd_postbaseline(args); return; }
    if (strcmp(cmd, "devmap") == 0) { cmd_devmap(args); return; }
    if (strcmp(cmd, "crashlog") == 0) { cmd_crashlog(args); return; }
    if (strcmp(cmd, "panicroom") == 0 || strcmp(cmd, "panic") == 0) { cmd_panicroom(args); return; }
    if (strcmp(cmd, "paniccapsule") == 0) { cmd_paniccapsule(args); return; }
    if (strcmp(cmd, "rollback") == 0) { cmd_rollback(args); return; }
    if (strcmp(cmd, "trust") == 0) { cmd_trust(args); return; }
    if (strcmp(cmd, "bugeye") == 0) { cmd_bugeye(args); return; }
    if (strcmp(cmd, "bugreplay") == 0) { cmd_bugreplay(args); return; }
    if (strcmp(cmd, "oldcheck") == 0) { cmd_oldcheck(args); return; }
    if (strcmp(cmd, "lfsdoctor") == 0) { cmd_lfsdoctor(args); return; }
    if (strcmp(cmd, "cfgprof") == 0) { cmd_cfgprof(args); return; }
    if (strcmp(cmd, "userlaw") == 0) { cmd_userlaw(args); return; }
    if (strcmp(cmd, "nice") == 0) { cmd_nice(args); return; }
    if (strcmp(cmd, "prio") == 0 || strcmp(cmd, "priority") == 0) { cmd_prio(args); return; }
    if (strcmp(cmd, "release") == 0 || strcmp(cmd, "releases") == 0) { cmd_release(args); return; }
    if (strcmp(cmd, "peek") == 0) { cmd_peek(args); return; }
    if (strcmp(cmd, "poke") == 0) { cmd_poke(args); return; }
    if (strcmp(cmd, "lars") == 0) { cmd_larddoc(args, "Usage: lars [drive:]file.lars"); return; }
    if (strcmp(cmd, "lardd") == 0) { cmd_larddoc(args, "Usage: lardd [drive:]file.lardd"); return; }
    if (strcmp(cmd, "doc") == 0) { cmd_larddoc(args, "Usage: doc [drive:]file.lars|file.lardd"); return; }
    if (strcmp(cmd, "larsview") == 0) { cmd_larsview(args); return; }
    if (strcmp(cmd, "larsapp") == 0) { cmd_larsapp(args); return; }
    if (strcmp(cmd, "larddnotes") == 0 || strcmp(cmd, "notes") == 0) { cmd_larddnotes(args); return; }
    if (strcmp(cmd, "larsform") == 0) { cmd_larsform(args); return; }
    if (strcmp(cmd, "larsact") == 0) { cmd_larsact(args); return; }
    if (strcmp(cmd, "lunit") == 0) { cmd_lunit(args); return; }
    if (strcmp(cmd, "lpack") == 0) { cmd_lpack(args); return; }
    if (strcmp(cmd, "lpackls") == 0) { cmd_lpack_op("list", args); return; }
    if (strcmp(cmd, "lpackinstall") == 0) { cmd_lpack_op("install", args); return; }
    if (strcmp(cmd, "lpackverify") == 0) { cmd_lpack_op("verify", args); return; }
    if (strcmp(cmd, "lpackchecksum") == 0) { cmd_lpack_op("checksum", args); return; }
    if (strcmp(cmd, "lpackundo") == 0) { cmd_lpack("undo"); return; }
    if (strcmp(cmd, "copy") == 0 || strcmp(cmd, "cp") == 0) { cmd_copy(args); return; }
    if (strcmp(cmd, "write") == 0) { cmd_write(args); return; }
    if (strcmp(cmd, "append") == 0) { cmd_append(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'e' && cmd[2] == 't' && cmd[3] == '\0') { cmd_set(args); return; }
    if (cmd[0] == 'm' && cmd[1] == 'o' && cmd[2] == 'r' && cmd[3] == 'e' && cmd[4] == '\0') { cmd_more(args); return; }
    if (cmd[0] == 'd' && cmd[1] == 'i' && cmd[2] == 'r' && cmd[3] == '\0') { cmd_dir(args); return; }
    if (cmd[0] == 't' && cmd[1] == 'y' && cmd[2] == 'p' && cmd[3] == 'e' && cmd[4] == '\0') { cmd_type(args); return; }
    if (strncmp(cmd, "lafillo", 7) == 0 && (cmd[7] == '\0' || cmd[7] == ' ' || cmd[7] == '\t')) { cmd_lafillo(args); return; }
    if (cmd[0] == 'l' && cmd[1] == 'a' && cmd[2] == 'r' && cmd[3] == 'l' && cmd[4] == 's' && cmd[5] == '\0') { cmd_larls(args); return; }
    if (cmd[0] == 'l' && cmd[1] == 'a' && cmd[2] == 'r' && cmd[3] == 'x' && cmd[4] == '\0') { cmd_larx(args); return; }
    if (cmd[0] == 'd' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 'v' && cmd[4] == 'e' && cmd[5] == 'r' && cmd[6] == 's' && cmd[7] == '\0') { cmd_drivers(args); return; }
    if (cmd[0] == 'f' && cmd[1] == 's' && cmd[2] == 's' && cmd[3] == 't' && cmd[4] == 'a' && cmd[5] == 't' && cmd[6] == '\0') { cmd_fsstat(args); return; }
    if (cmd[0] == 'f' && cmd[1] == 's' && cmd[2] == 's' && cmd[3] == 'a' && cmd[4] == 'v' && cmd[5] == 'e' && cmd[6] == '\0') { cmd_fssave(args); return; }
    if (cmd[0] == 'f' && cmd[1] == 's' && cmd[2] == 'l' && cmd[3] == 'o' && cmd[4] == 'a' && cmd[5] == 'd' && cmd[6] == '\0') { cmd_fsload(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'y' && cmd[2] == 'n' && cmd[3] == 'c' && cmd[4] == '\0') { cmd_fssave(args); return; }
    if (cmd[0] == 'p' && cmd[1] == 'o' && cmd[2] == 's' && cmd[3] == 't' && cmd[4] == '\0') {
        const char* rest = args;
        char sub[16];
        if (vcs_read_word(&rest, sub, sizeof(sub)) == 0 &&
            (strcmp(sub, "baseline") == 0 || strcmp(sub, "base") == 0)) {
            cmd_postbaseline(rest);
            return;
        }
        cmd_selftest(args);
        return;
    }
    if (cmd[0] == 's' && cmd[1] == 'r' && cmd[2] == 'a' && cmd[3] == 'm' && cmd[4] == '\0') { cmd_sram(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'c' && cmd[2] == 'r' && cmd[3] == 'e' && cmd[4] == 'e' && cmd[5] == 'n' && cmd[6] == 'r' && cmd[7] == 'a' && cmd[8] == 'm' && cmd[9] == '\0') { cmd_sram(args); return; }
    if (strcmp(cmd, "screencheck") == 0 || strcmp(cmd, "scrcheck") == 0) { cmd_screencheck(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'f' && cmd[4] == 't' && cmd[5] == 'e' && cmd[6] == 's' && cmd[7] == 't' && cmd[8] == '\0') { cmd_selftest(args); return; }
    if (cmd[0] == 'l' && cmd[1] == 'c' && cmd[2] == 'n' && cmd[3] == 't' && cmd[4] == '\0') { cmd_lcnt(args); return; }
    if (cmd[0] == 'c' && cmd[1] == 'o' && cmd[2] == 'n' && cmd[3] == 't' && cmd[4] == 'a' && cmd[5] == 'i' && cmd[6] == 'n' && cmd[7] == 'e' && cmd[8] == 'r' && cmd[9] == '\0') { cmd_lcnt(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == '\0') { cmd_vcs(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == 'i' && cmd[4] == 'n' && cmd[5] == 'i' && cmd[6] == 't' && cmd[7] == '\0') { cmd_vcsinit(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == 's' && cmd[4] == 't' && cmd[5] == 'a' && cmd[6] == 't' && cmd[7] == 'u' && cmd[8] == 's' && cmd[9] == '\0') { cmd_vcsstatus(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == 'a' && cmd[4] == 'd' && cmd[5] == 'd' && cmd[6] == '\0') { cmd_vcsadd(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == 'c' && cmd[4] == 'o' && cmd[5] == 'm' && cmd[6] == 'm' && cmd[7] == 'i' && cmd[8] == 't' && cmd[9] == '\0') { cmd_vcscommit(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == 'l' && cmd[4] == 'o' && cmd[5] == 'g' && cmd[6] == '\0') { cmd_vcslog(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'c' && cmd[2] == 's' && cmd[3] == 's' && cmd[4] == 'h' && cmd[5] == 'o' && cmd[6] == 'w' && cmd[7] == '\0') { cmd_vcsshow(args); return; }
    if (cmd[0] == 'v' && cmd[1] == 'e' && cmd[2] == 'r' && cmd[3] == '\0') { cmd_ver(args); return; }
    if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == '\0') { cmd_echo(args); return; }
    if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 's' && cmd[3] == '\0') { lsh_clear_output(); return; }
    if (cmd[0] == 's' && cmd[1] == 'u' && cmd[2] == 'm' && cmd[3] == '\0') { cmd_sum(args); return; }
    if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 's' && cmd[5] == 'u' && cmd[6] == 'm' && cmd[7] == '\0') { cmd_exitsum(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'a' && cmd[2] == 'n' && cmd[3] == 'd' && cmd[4] == 'b' && cmd[5] == 'o' && cmd[6] == 'x' && cmd[7] == '\0') { cmd_sandbox(args); return; }
    if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 's' && cmd[5] == 'a' && cmd[6] == 'n' && cmd[7] == 'd' && cmd[8] == 'b' && cmd[9] == 'o' && cmd[10] == 'x' && cmd[11] == '\0') { cmd_exitsandbox(args); return; }
    if (cmd[0] == 'a' && cmd[1] == 's' && cmd[2] == 'm' && cmd[3] == '_' && cmd[4] == '\0') { cmd_asm_(args); return; }
    if (cmd[0] == 'l' && cmd[1] == 'a' && cmd[2] == 'r' && cmd[3] == 's' && cmd[4] == 'h' && cmd[5] == '\0') { cmd_larsh(args); return; }
    if (cmd[0] == 'b' && cmd[1] == 'o' && cmd[2] == 's' && cmd[3] == 'l' && cmd[4] == '\0') { cmd_bosl(args); return; }
    if (cmd[0] == 'l' && cmd[1] == 'i' && cmd[2] == 'l' && cmd[3] == '\0') { cmd_lil(args); return; }
    if (cmd[0] == 'l' && cmd[1] == 'a' && cmd[2] == 'f' && cmd[3] == 'v' && cmd[4] == 'm' && cmd[5] == '\0') { cmd_lafvm(args); return; }
    if (cmd[0] == 'o' && cmd[1] == 's' && cmd[2] == 'v' && cmd[3] == 'm' && cmd[4] == '\0') { cmd_osvm(args); return; }
    if (cmd[0] == 'r' && cmd[1] == 'u' && cmd[2] == 'n' && cmd[3] == '\0') { cmd_run(args); return; }
    if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == '\0') {
        char drv;
        char path[64];
        resolve_path(args, &drv, path, sizeof(path));
        if (path[0]) { out_append("cd: no dirs (flat FS)\n"); }
        else { s_drive = drv; out_append("Drive "); out_append_char(drv); out_append(":\n"); }
        return;
    }
    if (cmd[0] == 'X' && cmd[1] == ':' && cmd[2] == '\0') { s_drive = 'X'; return; }
    if (cmd[0] == 'Y' && cmd[1] == ':' && cmd[2] == '\0') { s_drive = 'Y'; return; }
    if (cmd[0] == 'Z' && cmd[1] == ':' && cmd[2] == '\0') { s_drive = 'Z'; return; }
    if (cmd[0] >= 'A' && cmd[0] <= 'Z' && cmd[1] == ':' && cmd[2] == '\0') { s_drive = cmd[0]; return; }
    if (cmd[0] >= 'a' && cmd[0] <= 'z' && cmd[1] == ':' && cmd[2] == '\0') { s_drive = (char)(cmd[0] - 32); return; }

    if (run_lsh_cmd(cmd, args)) return;
    out_append("Unknown command: ");
    out_append(cmd);
    out_append("\n");
}

void lsh_init(void)
{
    s_drive = 'X';
    s_out_len = 0;
    s_output[0] = '\0';
    s_nenv = 0;
    s_in_sum_mode = 0;
    s_sandbox_mode = 0;
    s_cfgsh_mode = 0;
    taskprio_init();
    lcontainer_init();
    lvcs_init();
    lardkit_init();
    out_append("Lard Shell ready. Type help for commands.\n");
}

static void parse_and_run(const char* cmd, const char* args);

static void run_one_segment(const char* seg)
{
    char cmd[64];
    char args[192];
    uint32_t i = 0;
    while (seg[i] == ' ' || seg[i] == '\t') i++;
    uint32_t ci = 0;
    while (seg[i] && seg[i] != ' ' && seg[i] != '\t' && ci < 63) cmd[ci++] = seg[i++];
    cmd[ci] = '\0';
    uint32_t ai = 0;
    while (seg[i] && ai < 191) args[ai++] = seg[i++];
    args[ai] = '\0';
    parse_and_run(cmd, args);
}

static void lsh_exec_impl(const char* buf, int show_prompt);

void lsh_exec(const char* line)
{
    if (!line) return;
    char buf[LSH_MAX_LINE];
    uint32_t i = 0;
    while (line[i] && i < LSH_MAX_LINE - 1) { buf[i] = line[i]; i++; }
    buf[i] = '\0';

    /* Strip trailing & */
    int background = 0;
    while (i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) i--;
    if (i > 0 && buf[i-1] == '&') {
        buf[i-1] = '\0';
        i--;
        while (i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) { buf[--i] = '\0'; }
        background = 1;
    }

    expand_env(buf);

    if (background) {
        uint32_t task_id = 0;
        int enqueue_ok = taskprio_enqueue(NULL, buf, taskprio_default_priority(), &task_id) == 0;
        if (s_cfgsh_mode) {
            out_append("CFG# ");
        } else if (s_in_sum_mode) {
            out_append("SUM# ");
        } else if (s_sandbox_mode) {
            out_append("[sandbox] ");
            out_append_char(s_drive);
            out_append(":\\> ");
        } else if (lcontainer_has_active()) {
            out_append("[lcnt:");
            out_append(lcontainer_active_name());
            out_append("] ");
            out_append_char(s_drive);
            out_append(":\\> ");
        } else {
            out_append_char(s_drive);
            out_append(":\\> ");
        }
        out_append(line);
        out_append("\n");
        if (enqueue_ok) {
            out_append("(background task #");
            out_append_u32(task_id);
            out_append(" prio=");
            out_append_i32(taskprio_default_priority());
            out_append(")\n");
        } else {
            out_append("Background task queue full.\n");
        }
        return;
    }

    if (s_cfgsh_mode) {
        out_append("CFG# ");
    } else if (s_in_sum_mode) {
        out_append("SUM# ");
    } else if (s_sandbox_mode) {
        out_append("[sandbox] ");
        out_append_char(s_drive);
        out_append(":\\> ");
    } else if (lcontainer_has_active()) {
        out_append("[lcnt:");
        out_append(lcontainer_active_name());
        out_append("] ");
        out_append_char(s_drive);
        out_append(":\\> ");
    } else {
        out_append_char(s_drive);
        out_append(":\\> ");
    }
    out_append(line);
    out_append("\n");

    lsh_exec_impl(buf, 0);
}

static void lsh_exec_impl(const char* buf, int show_prompt)
{
    uint32_t i;
    (void)show_prompt;
    uint32_t pipe_at = 0;
    int has_pipe = 0;
    for (i = 0; buf[i]; i++) {
        if (buf[i] == '|') { pipe_at = i; has_pipe = 1; break; }
    }

    if (!has_pipe) {
        s_redirect_to_pipe = 0;
        s_pipe_has_input = 0;
        run_one_segment(buf);
        return;
    }

    /* Split on | (max 2 segments) */
    char seg0[LSH_MAX_LINE];
    char seg1[LSH_MAX_LINE];
    uint32_t j;
    for (j = 0; j < pipe_at && j < LSH_MAX_LINE - 1; j++) seg0[j] = buf[j];
    seg0[j] = '\0';
    j = pipe_at + 1;
    while (buf[j] == ' ' || buf[j] == '\t') j++;
    i = 0;
    while (buf[j] && i < LSH_MAX_LINE - 1) seg1[i++] = buf[j++];
    seg1[i] = '\0';

    s_redirect_to_pipe = 1;
    s_pipe_len = 0;
    s_pipe_has_input = 0;
    run_one_segment(seg0);

    s_redirect_to_pipe = 0;
    s_pipe_has_input = 1;
    run_one_segment(seg1);
    s_pipe_has_input = 0;
}

int lsh_poll_background(void)
{
    taskprio_task_t task;
    if (taskprio_dequeue_next(&task) != 1) return 0;
    lsh_exec_impl(task.command, 0);
    return 1;
}

const char* lsh_get_output(void)
{
    return s_output;
}

void lsh_clear_output(void)
{
    s_out_len = 0;
    s_output[0] = '\0';
}

char lsh_get_drive(void)
{
    return s_drive;
}

void lsh_set_drive(char letter)
{
    if (letter >= 'a' && letter <= 'z') letter = (char)(letter - 32);
    if ((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z')) s_drive = letter;
}

const char* lsh_stdin(void)
{
    return s_pipe_has_input ? s_pipe_buf : 0;
}

int lsh_in_sum_mode(void)
{
    return s_in_sum_mode;
}
