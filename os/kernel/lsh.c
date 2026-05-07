/*
 * LSH - Lard Shell
 * Drive X=default, Y=floppy, Z=RAM, A~W=extra.
 */
#include "lsh.h"
#include "fs.h"
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
#include "gui.h"
#include "post.h"
#include "cpumode.h"
#include "oslink.h"
#include "taskprio.h"
#include "version.h"
#include "io.h"
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

static void out_append_i32(int32_t v)
{
    if (v < 0) {
        out_append_char('-');
        out_append_u32((uint32_t)(-v));
    } else {
        out_append_u32((uint32_t)v);
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
    { "help", 1 }, { "control", 1 }, { "status", 1 }, { "release", 1 }, { "releases", 1 },
    { "ver", 1 }, { "post", 1 }, { "selftest", 1 }, { "mode", 1 }, { "oslink", 1 }, { "task", 1 }, { "tasks", 1 }, { "tasktop", 1 }, { "nice", 1 }, { "prio", 1 }, { "cls", 1 },
    { "dir", 1 }, { "type", 1 }, { "more", 1 }, { "lars", 1 }, { "lardd", 1 }, { "doc", 1 },
    { "copy", 1 }, { "cp", 1 }, { "write", 1 }, { "append", 1 }, { "set", 1 }, { "echo", 1 }, { "cd", 1 },
    { "lafillo", 1 }, { "larls", 1 }, { "larx", 1 }, { "larsh", 1 },
    { "bosl", 1 }, { "lil", 1 }, { "lafvm", 1 }, { "osvm", 1 }, { "run", 1 },
    { "lcnt", 1 }, { "container", 1 },
    { "vcs", 1 }, { "vcsinit", 1 }, { "vcsstatus", 1 }, { "vcsadd", 1 }, { "vcscommit", 1 },
    { "vcslog", 1 }, { "vcsshow", 1 },
    { "drivers", 1 }, { "fsstat", 1 }, { "fsload", 1 }, { "fssave", 1 }, { "sync", 1 },
    { "sram", 1 }, { "screenram", 1 }, { "sandbox", 1 }, { "exitsandbox", 1 },
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
        out_append("Usage: magic command [args]\n");
        return;
    }
    const magic_cmd_entry_t* exact = magic_find_exact(cmd);
    const magic_cmd_entry_t* pick = exact ? exact : magic_predict(cmd);
    if (!pick) {
        out_append("magic: no confident command match for ");
        out_append(cmd);
        out_append("\n");
        return;
    }
    if (!pick->magic_safe) {
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

static int s_dir_skip_ram;

static void dir_cb(const char* nm, uint32_t sz, void* u)
{
    (void)u;
    if (s_dir_skip_ram) {
        if (nm[0] == 'n' && nm[1] == 'o' && nm[2] == 't' && nm[3] == 'e' &&
            nm[4] == 's' && nm[5] == '.' && nm[6] == 't' && nm[7] == 'x' && nm[8] == 't' && nm[9] == '\0')
            return;
    }
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

static void dir_ram_name(const char* name)
{
    FsWritableFile* w = fs_open_writable(name);
    if (!w) return;
    out_append("  ");
    out_append(w->name);
    out_append(" ");
    out_append_u32(w->size);
    out_append("\n");
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
        s_dir_skip_ram = 1;
        fs_list(dir_cb, NULL);
        s_dir_skip_ram = 0;
    } else {
        dir_ram_name("notes.txt");
        dir_ram_name("lafillo_saved.txt");
        dir_ram_name("lar_extract.txt");
        dir_ram_name("vcs_restore.txt");
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
    (void)args;
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

static void cmd_help(const char* args)
{
    (void)args;
    out_append("Lard Shell commands\n");
    out_append("  help control status release ver post selftest magic mode oslink task cls\n");
    out_append("  dir [drive:]  type file  more  lars file  lardd file\n");
    out_append("  write file text  append file text  copy src dst\n");
    out_append("  set NAME=value  echo text  cd drive:  X: Y: Z:\n");
    out_append("  lafillo file  larls archive  larx archive member  larsh file\n");
    out_append("  bosl file  lil file  lafvm file  osvm file  run file.bosx [args]\n");
    out_append("  lcnt list|create|rm|use|exit|run|info\n");
    out_append("  vcs init|status|add|commit|log|show\n");
    out_append("  drivers fsstat fsload fssave sync sram sandbox exitsandbox\n");
    out_append("  tasktop  task list|set|up|down|pause|resume|drop  nice prio cmd\n");
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
    out_append("  Each feature addition gets a version bump and releases.lardd entry.\n");
    out_append("\n");
    out_append("Start points:\n");
    out_append("  status              inspect version, drivers, storage, containers\n");
    out_append("  magic statsu        predict and execute the intended safe command\n");
    out_append("  mode probe          real16 <-> long64 controlled roundtrip\n");
    out_append("  oslink status       inspect OS-to-OS message link\n");
    out_append("  task list           inspect and reprioritize queued tasks\n");
    out_append("  sram on             use a quiet screen corner as scratch RAM\n");
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
    (void)args;

    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    fs_persist_detail(&bank, &generation, &bank_sectors);
    drivers = drfl_list(NULL, NULL);

    out_append("LardOS ");
    out_append(LARDOS_VERSION);
    out_append(" (");
    out_append(lardos_version_channel());
    out_append(")\n");
    out_append("Drive: ");
    out_append_char(s_drive);
    out_append(":\\\n");
    out_append("Mode: ");
    if (s_in_sum_mode) {
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
    out_append(", default-prio=");
    out_append_i32(tasks.default_priority);
    out_append(", completed=");
    out_append_u32(tasks.completed);
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
        out_append("Usage: mode [status|probe|real]\n");
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

static const char* oslink_type_name(uint8_t type)
{
    if (type == 1) return "hello";
    if (type == 2) return "ping";
    if (type == 3) return "pong";
    if (type == 4) return "text";
    if (type == 5) return "ack";
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
    out_append(" recv=");
    out_append_u32(info.received);
    out_append(" dropped=");
    out_append_u32(info.dropped);
    out_append(" inbox=");
    out_append_u32(info.inbox_count);
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

static void cmd_oslink_recv(void)
{
    oslink_msg_t m;
    oslink_poll();
    if (oslink_recv(&m) == 0) {
        out_append("oslink: inbox empty.\n");
        return;
    }
    out_append(oslink_type_name(m.type));
    out_append(" from ");
    out_append(m.src_node);
    out_append(" ");
    out_append_ip4(m.src_ip);
    out_append(" seq=");
    out_append_u32(m.seq);
    out_append("\n");
    if (m.text[0]) {
        out_append(m.text);
        out_append("\n");
    }
}

static void cmd_oslink_send_like(const char* args, int kind)
{
    ip4_t dst;
    int r;
    if (lsh_parse_ip4_arg(&args, &dst) != 0) {
        out_append(kind == 1 ? "Usage: oslink hello ip\n" :
                   kind == 2 ? "Usage: oslink ping ip [text]\n" :
                               "Usage: oslink send ip text\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (kind == 1) r = oslink_send_hello(dst);
    else if (kind == 2) r = oslink_send_ping(dst, args);
    else r = oslink_send_text(dst, args);
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
    if (strcmp(sub, "test") == 0) {
        out_append(oslink_selftest() == 0 ? "oslink: selftest OK\n" : "oslink: selftest failed\n");
        return;
    }
    out_append("Usage: oslink status|hello|ping|send|recv|peers|poll|test\n");
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

static void tasktop_bar(int32_t priority)
{
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
        out_append("Usage: task run priority command\n");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (!*args) {
        out_append("Usage: task run priority command\n");
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
    if (strcmp(sub, "default") == 0) {
        int32_t prio;
        if (task_parse_priority(&args, &prio) != 0) {
            out_append("task: default-prio=");
            out_append_i32(taskprio_default_priority());
            out_append("\nUsage: task default priority\n");
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
            out_append("Usage: task set id priority\n");
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
    if (strcmp(sub, "boost") == 0) {
        uint32_t id;
        if (vcs_parse_u32(&args, &id) != 0) {
            out_append("Usage: task boost id\n");
            return;
        }
        if (taskprio_set_priority(id, TASKPRIO_MAX) == 0) out_append("task: boosted.\n");
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
    out_append("Usage: task list|set|default|run|up|down|pause|resume|boost|drop|test\n");
}

static void cmd_nice(const char* args)
{
    cmd_task_run_like(args);
}

static void cmd_prio(const char* args)
{
    uint32_t id;
    int32_t prio;
    if (vcs_parse_u32(&args, &id) != 0 || task_parse_priority(&args, &prio) != 0) {
        out_append("Usage: prio id priority\n");
        return;
    }
    if (taskprio_set_priority(id, prio) == 0) out_append("prio: updated.\n");
    else out_append("prio: id not found.\n");
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
    if (strcmp(cmd, "mode") == 0) { cmd_mode(args); return; }
    if (strcmp(cmd, "oslink") == 0) { cmd_oslink(args); return; }
    if (strcmp(cmd, "task") == 0 || strcmp(cmd, "tasks") == 0) { cmd_task(args); return; }
    if (strcmp(cmd, "tasktop") == 0) { cmd_tasktop(args); return; }
    if (strcmp(cmd, "nice") == 0) { cmd_nice(args); return; }
    if (strcmp(cmd, "prio") == 0) { cmd_prio(args); return; }
    if (strcmp(cmd, "release") == 0 || strcmp(cmd, "releases") == 0) { cmd_release(args); return; }
    if (strcmp(cmd, "peek") == 0) { cmd_peek(args); return; }
    if (strcmp(cmd, "poke") == 0) { cmd_poke(args); return; }
    if (strcmp(cmd, "lars") == 0) { cmd_larddoc(args, "Usage: lars [drive:]file.lars"); return; }
    if (strcmp(cmd, "lardd") == 0) { cmd_larddoc(args, "Usage: lardd [drive:]file.lardd"); return; }
    if (strcmp(cmd, "doc") == 0) { cmd_larddoc(args, "Usage: doc [drive:]file.lars|file.lardd"); return; }
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
    if (cmd[0] == 'p' && cmd[1] == 'o' && cmd[2] == 's' && cmd[3] == 't' && cmd[4] == '\0') { cmd_selftest(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'r' && cmd[2] == 'a' && cmd[3] == 'm' && cmd[4] == '\0') { cmd_sram(args); return; }
    if (cmd[0] == 's' && cmd[1] == 'c' && cmd[2] == 'r' && cmd[3] == 'e' && cmd[4] == 'e' && cmd[5] == 'n' && cmd[6] == 'r' && cmd[7] == 'a' && cmd[8] == 'm' && cmd[9] == '\0') { cmd_sram(args); return; }
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
    taskprio_init();
    lcontainer_init();
    lvcs_init();
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
        if (s_in_sum_mode) {
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

    if (s_in_sum_mode) {
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
