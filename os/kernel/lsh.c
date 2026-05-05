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
#include "gui.h"
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

/* Background: queue of command lines */
static char s_bg_queue[LSH_MAX_BG][LSH_MAX_LINE];
static uint32_t s_bg_count;

/* SUM (Super User Mode): ring 0, full permissions, asm_ for hardware I/O */
static int s_in_sum_mode;

/* Sandbox: run LARDX with restricted syscalls (no file/LDLL/network) */
static int s_sandbox_mode;

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

static FsWritableFile* lsh_open_write(char drive, const char* name)
{
    if (drive_to_fs(drive) != 1) return NULL;
    return fs_open_writable(name);
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
        FsWritableFile* w = fs_open_writable("notes.txt");
        if (w) {
            out_append("  notes.txt ");
            char tmp[16];
            uint32_t sz = w->size, t = 0;
            if (sz == 0) tmp[t++] = '0';
            else while (sz) { tmp[t++] = (char)('0' + (sz % 10)); sz /= 10; }
            while (t--) out_append_char(tmp[t]);
            out_append("\n");
        }
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

static void cmd_ver(const char* args)
{
    (void)args;
    out_append("LardOS LSH 1.0\n");
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

static void cmd_run(const char* args)
{
    char buf[LSH_MAX_LINE];
    uint32_t bi = 0;
    while (args[bi] && bi < LSH_MAX_LINE - 1) buf[bi] = args[bi++];
    buf[bi] = '\0';

    char drv;
    char path[64];
    resolve_path(buf, &drv, path, sizeof(path));
    if (!path[0]) {
        out_append("Usage: run [drive:]file.bosx [arg1 [arg2 ...]]\n");
        return;
    }

    char* rest = buf;
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

    int r = s_sandbox_mode ? lardx_run_sandbox(path, argc, argv) : lardx_run(path, argc, argv);
    if (r == -1) out_append("run: file not found or too small.\n");
    else if (r == -2) out_append("run: not a LARDX file.\n");
    else if (r == -3) out_append("run: unsupported LARDX version.\n");
    else if (r == -4) out_append("run: not a user executable (use mkardx --user).\n");
    else if (r == -5) out_append("run: invalid segment count.\n");
    else if (r == -6) out_append("run: truncated file.\n");
    else if (r == -7) out_append("run: segment overflow.\n");
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
    while (name[i] && i < 76) path[i++] = name[i];
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
    if (nlen >= 32 || 6 + nlen + 2 >= f->size) return 0;
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

    if (cmd[0] == 's' && cmd[1] == 'e' && cmd[2] == 't' && cmd[3] == '\0') { cmd_set(args); return; }
    if (cmd[0] == 'm' && cmd[1] == 'o' && cmd[2] == 'r' && cmd[3] == 'e' && cmd[4] == '\0') { cmd_more(args); return; }
    if (cmd[0] == 'd' && cmd[1] == 'i' && cmd[2] == 'r' && cmd[3] == '\0') { cmd_dir(args); return; }
    if (cmd[0] == 't' && cmd[1] == 'y' && cmd[2] == 'p' && cmd[3] == 'e' && cmd[4] == '\0') { cmd_type(args); return; }
    if (strncmp(cmd, "lafillo", 7) == 0 && (cmd[7] == '\0' || cmd[7] == ' ' || cmd[7] == '\t')) { cmd_lafillo(args); return; }
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

    if (background && s_bg_count < LSH_MAX_BG) {
        uint32_t j = 0;
        while (buf[j] && j < LSH_MAX_LINE - 1) {
            s_bg_queue[s_bg_count][j] = buf[j];
            j++;
        }
        s_bg_queue[s_bg_count][j] = '\0';
        s_bg_count++;
        if (s_in_sum_mode) {
            out_append("SUM# ");
        } else if (s_sandbox_mode) {
            out_append("[sandbox] ");
            out_append_char(s_drive);
            out_append(":\\> ");
        } else {
            out_append_char(s_drive);
            out_append(":\\> ");
        }
        out_append(line);
        out_append("\n");
        out_append("(background)\n");
        return;
    }
    if (background) {
        if (s_in_sum_mode) {
            out_append("SUM# ");
        } else if (s_sandbox_mode) {
            out_append("[sandbox] ");
            out_append_char(s_drive);
            out_append(":\\> ");
        } else {
            out_append_char(s_drive);
            out_append(":\\> ");
        }
        out_append(line);
        out_append("\n");
        out_append("Background queue full.\n");
        return;
    }

    if (s_in_sum_mode) {
        out_append("SUM# ");
    } else if (s_sandbox_mode) {
        out_append("[sandbox] ");
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
    if (s_bg_count == 0) return 0;
    char line[LSH_MAX_LINE];
    uint32_t i = 0;
    while (s_bg_queue[0][i] && i < LSH_MAX_LINE - 1) { line[i] = s_bg_queue[0][i]; i++; }
    line[i] = '\0';
    s_bg_count--;
    for (uint32_t q = 0; q < s_bg_count; q++) {
        i = 0;
        while (s_bg_queue[q + 1][i]) {
            s_bg_queue[q][i] = s_bg_queue[q + 1][i];
            i++;
        }
        s_bg_queue[q][i] = '\0';
    }
    lsh_exec_impl(line, 0);
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
