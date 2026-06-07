#include "lha.h"

#include "fs.h"
#include "string.h"
#include "vmmon.h"

#include <stddef.h>

typedef struct {
    int used;
    lha_info_t info;
    char program[LHA_PROGRAM_MAX];
} lha_slot_t;

static lha_slot_t s_lha_slots[LHA_VM_MAX];
static char s_lha_error[80] = "ok";

static void lha_set_error(const char* msg)
{
    uint32_t i = 0;
    if (!msg) msg = "unknown";
    while (msg[i] && i + 1u < sizeof(s_lha_error)) {
        s_lha_error[i] = msg[i];
        i++;
    }
    s_lha_error[i] = '\0';
}

const char* lha_last_error(void)
{
    return s_lha_error;
}

static char lha_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static int lha_ci_char_eq(char a, char b)
{
    return lha_lower(a) == lha_lower(b);
}

static int lha_token_eq(const char* s, uint32_t len, const char* tok)
{
    uint32_t i = 0;
    while (tok[i]) {
        if (i >= len || !lha_ci_char_eq(s[i], tok[i])) return 0;
        i++;
    }
    return i == len;
}

static int lha_prefix(const char* s, uint32_t len, const char* prefix,
                      const char** rest, uint32_t* rest_len)
{
    uint32_t i = 0;
    while (prefix[i]) {
        if (i >= len || !lha_ci_char_eq(s[i], prefix[i])) return 0;
        i++;
    }
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    *rest = s + i;
    *rest_len = len - i;
    return 1;
}

static void lha_copy_name(char* out, uint32_t cap, const char* src, uint32_t len)
{
    uint32_t i = 0;
    while (i + 1u < cap && i < len && src[i] != '\r' && src[i] != '\n') {
        char c = src[i];
        if (c == ' ' || c == '\t') c = '-';
        out[i++] = c;
    }
    out[i] = '\0';
    if (i == 0) {
        out[0] = 'l';
        out[1] = 'h';
        out[2] = 'a';
        out[3] = '-';
        out[4] = 'v';
        out[5] = 'm';
        out[6] = '\0';
    }
}

static uint32_t lha_copy_cstr(char* dst, uint32_t cap, const char* src)
{
    uint32_t n = 0;
    if (cap == 0) return 0;
    while (src && src[n] && n + 1u < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
    return n;
}

static uint32_t lha_source_steps(const char* src)
{
    uint32_t steps = 0;
    int saw = 0;
    const char* p = src;
    while (p && *p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p == '\0') break;
        if (*p != '#' && *p != ';') {
            saw = 1;
        }
        while (*p && *p != '\n') p++;
        if (saw) {
            steps++;
            saw = 0;
        }
        if (*p == '\n') p++;
    }
    return steps ? steps : 1u;
}

static int lha_append_code_line(char* code, uint32_t cap, uint32_t* pos,
                                const char* src, uint32_t len)
{
    if (*pos + len + 1u >= cap) return -1;
    for (uint32_t i = 0; i < len; i++) {
        code[(*pos)++] = lha_lower(src[i]);
    }
    code[(*pos)++] = '\n';
    code[*pos] = '\0';
    return 0;
}

static int lha_extract_program(const char* fallback_name, const uint8_t* text, uint32_t len,
                               char* out_name, uint32_t name_cap,
                               char* out_code, uint32_t code_cap)
{
    uint32_t pos = 0;
    uint32_t code_pos = 0;
    int saw_header = 0;
    int in_code = 0;
    int saw_code = 0;
    int engine_ok = 1;
    lha_copy_name(out_name, name_cap, fallback_name ? fallback_name : "lha-vm",
                  fallback_name ? (uint32_t)strlen(fallback_name) : 6u);
    out_code[0] = '\0';

    while (pos < len) {
        uint32_t line_start = pos;
        uint32_t line_end;
        const char* line;
        uint32_t line_len;
        const char* rest;
        uint32_t rest_len;

        while (pos < len && text[pos] != '\n') pos++;
        line_end = pos;
        if (pos < len && text[pos] == '\n') pos++;
        while (line_end > line_start && (text[line_end - 1u] == '\r' ||
                                         text[line_end - 1u] == ' ' ||
                                         text[line_end - 1u] == '\t')) {
            line_end--;
        }
        while (line_start < line_end && (text[line_start] == ' ' || text[line_start] == '\t')) {
            line_start++;
        }
        line = (const char*)text + line_start;
        line_len = line_end - line_start;
        if (line_len == 0 || line[0] == '#') continue;

        if (!in_code) {
            if (lha_token_eq(line, line_len, "LHA 1")) {
                saw_header = 1;
            } else if (lha_prefix(line, line_len, "NAME", &rest, &rest_len)) {
                lha_copy_name(out_name, name_cap, rest, rest_len);
            } else if (lha_prefix(line, line_len, "ENGINE", &rest, &rest_len)) {
                engine_ok = lha_token_eq(rest, rest_len, "osvm");
            } else if (lha_token_eq(line, line_len, "CODE")) {
                in_code = 1;
                saw_code = 1;
            }
            continue;
        }

        if (lha_token_eq(line, line_len, "END")) {
            in_code = 0;
            break;
        }
        if (lha_append_code_line(out_code, code_cap, &code_pos, line, line_len) != 0) {
            lha_set_error("LHA source too large");
            return -5;
        }
    }

    if (!saw_header) {
        lha_set_error("missing LHA 1 header");
        return -2;
    }
    if (!engine_ok) {
        lha_set_error("unsupported LHA engine");
        return -3;
    }
    if (!saw_code || code_pos == 0) {
        lha_set_error("missing LHA CODE block");
        return -4;
    }
    lha_set_error("ok");
    return 0;
}

static int lha_slot_by_name(const char* name)
{
    for (uint32_t i = 0; i < LHA_VM_MAX; i++) {
        if (s_lha_slots[i].used && strcmp(s_lha_slots[i].info.name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int lha_free_slot(void)
{
    for (uint32_t i = 0; i < LHA_VM_MAX; i++) {
        if (!s_lha_slots[i].used) return (int)i;
    }
    return -1;
}

static void lha_write_report(const char* name, int rc, uint32_t source_size)
{
    FsWritableFile* w = fs_open_writable("lha.lardd");
    char buf[256];
    int n;
    if (!w) return;
    n = snprintf(buf, sizeof(buf),
                 "LARDD 1\n"
                 "TITLE LardOS Hypervisor API\n"
                 "TEXT LHA wraps LardOS VM runtimes with file-defined, user-visible VM creation.\n"
                 "SECTION Last Run\n"
                 "ITEM name %s\n"
                 "ITEM engine osvm\n"
                 "ITEM source-bytes %u\n"
                 "ITEM rc %d\n"
                 "END\n",
                 name ? name : "none", source_size, rc);
    if (n < 0) return;
    if ((uint32_t)n > sizeof(buf)) n = (int)sizeof(buf);
    (void)fs_write(w, 0, (const uint8_t*)buf, (uint32_t)n);
}

void lha_clear(void)
{
    for (uint32_t i = 0; i < LHA_VM_MAX; i++) {
        s_lha_slots[i].used = 0;
        s_lha_slots[i].program[0] = '\0';
        memset(&s_lha_slots[i].info, 0, sizeof(s_lha_slots[i].info));
    }
    lha_set_error("ok");
}

uint32_t lha_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < LHA_VM_MAX; i++) {
        if (s_lha_slots[i].used) n++;
    }
    return n;
}

int lha_info(uint32_t slot, lha_info_t* out)
{
    if (slot >= LHA_VM_MAX || !out || !s_lha_slots[slot].used) return -1;
    *out = s_lha_slots[slot].info;
    return 0;
}

int lha_create(const char* name, const char* osvm_source, uint32_t* out_slot)
{
    int slot;
    uint32_t size;
    if (!name || !name[0] || !osvm_source || !osvm_source[0]) {
        lha_set_error("missing LHA name or source");
        return -1;
    }
    size = (uint32_t)strlen(osvm_source);
    if (size >= LHA_PROGRAM_MAX) {
        lha_set_error("LHA program exceeds slot size");
        return -2;
    }
    slot = lha_slot_by_name(name);
    if (slot < 0) slot = lha_free_slot();
    if (slot < 0) {
        lha_set_error("no free LHA VM slot");
        return -3;
    }

    s_lha_slots[slot].used = 1;
    lha_copy_cstr(s_lha_slots[slot].info.name, sizeof(s_lha_slots[slot].info.name), name);
    s_lha_slots[slot].info.slot = (uint32_t)slot;
    s_lha_slots[slot].info.used = 1;
    s_lha_slots[slot].info.source_size = size;
    s_lha_slots[slot].info.last_rc = 0;
    lha_copy_cstr(s_lha_slots[slot].program, sizeof(s_lha_slots[slot].program), osvm_source);
    if (out_slot) *out_slot = (uint32_t)slot;
    lha_write_report(name, 0, size);
    lha_set_error("ok");
    return 0;
}

int lha_create_from_text(const char* fallback_name, const uint8_t* text, uint32_t len,
                         uint32_t* out_slot)
{
    char name[LHA_NAME_MAX];
    char code[LHA_PROGRAM_MAX];
    int r;
    if (!text || len == 0) {
        lha_set_error("empty LHA file");
        return -1;
    }
    r = lha_extract_program(fallback_name, text, len, name, sizeof(name), code, sizeof(code));
    if (r != 0) return r;
    return lha_create(name, code, out_slot);
}

int lha_run_source(const char* name, const char* osvm_source, os_vm_putc_fn putc, void* user)
{
    uint32_t steps = lha_source_steps(osvm_source);
    uint32_t size = osvm_source ? (uint32_t)strlen(osvm_source) : 0u;
    int rc = os_vm_asm_eval(osvm_source, putc, user);
    vmmon_record(VMMON_LHA, steps, rc);
    lha_write_report(name ? name : "source", rc, size);
    if (rc != 0) lha_set_error("LHA VM execution failed");
    else lha_set_error("ok");
    return rc;
}

int lha_run(uint32_t slot, os_vm_putc_fn putc, void* user)
{
    lha_slot_t* s;
    int rc;
    if (slot >= LHA_VM_MAX || !s_lha_slots[slot].used) {
        lha_set_error("bad LHA VM slot");
        return -1;
    }
    s = &s_lha_slots[slot];
    rc = lha_run_source(s->info.name, s->program, putc, user);
    s->info.runs++;
    s->info.last_rc = rc;
    if (rc != 0) s->info.failures++;
    return rc;
}

int lha_run_file(const char* path, os_vm_putc_fn putc, void* user)
{
    const FsFile* f = fs_open(path ? path : "lha_demo.lhvm");
    uint32_t slot = 0;
    int r;
    if (!f) {
        lha_set_error("LHA file not found");
        return -1;
    }
    r = lha_create_from_text(f->name, f->data, f->size, &slot);
    if (r != 0) return r;
    return lha_run(slot, putc, user);
}

int lha_demo(os_vm_putc_fn putc, void* user)
{
    return lha_run_file("lha_demo.lhvm", putc, user);
}

typedef struct {
    char buf[16];
    uint32_t len;
} lha_cap_t;

static void lha_cap_putc(char c, void* user)
{
    lha_cap_t* cap = (lha_cap_t*)user;
    if (!cap) return;
    if (cap->len + 1u < sizeof(cap->buf)) {
        cap->buf[cap->len++] = c;
        cap->buf[cap->len] = '\0';
    }
}

int lha_selftest(void)
{
    static const uint8_t sample[] =
        "LHA 1\n"
        "NAME lha-selftest\n"
        "ENGINE osvm\n"
        "MEMORY 64\n"
        "CODE\n"
        "push 40\n"
        "push 2\n"
        "add\n"
        "print\n"
        "halt\n"
        "END\n";
    char name[LHA_NAME_MAX];
    char code[LHA_PROGRAM_MAX];
    lha_cap_t cap;
    int r;

    if (!fs_open("lha_demo.lhvm")) return -1;
    if (lha_extract_program("selftest.lhvm", sample, sizeof(sample) - 1u,
                            name, sizeof(name), code, sizeof(code)) != 0) {
        return -2;
    }
    cap.len = 0;
    cap.buf[0] = '\0';
    r = lha_run_source(name, code, lha_cap_putc, &cap);
    if (r != 0) return -3;
    if (strcmp(cap.buf, "42\n") != 0) return -4;
    return 0;
}
