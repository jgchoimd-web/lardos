#include "kmo.h"

#include "fs.h"
#include "kmodtalk.h"
#include "lardkit.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

static kmo_module_t s_kmos[KMO_MAX_MODULES];
static uint32_t s_kmo_count;

static uint32_t slen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && lower_char(a[i]) == lower_char(b[i])) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int has_suffix_ci(const char* name, const char* suffix)
{
    uint32_t nl = slen(name);
    uint32_t sl = slen(suffix);
    if (!name || !suffix || nl < sl) return 0;
    for (uint32_t i = 0; i < sl; i++) {
        if (lower_char(name[nl - sl + i]) != lower_char(suffix[i])) return 0;
    }
    return 1;
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

static void append_body_line(char* dst, uint32_t cap, const char* src)
{
    if (!dst || cap == 0 || !src) return;
    sappend(dst, cap, src);
    sappend(dst, cap, "\n");
}

static const char* skip_ws(const char* s)
{
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static void strip_tail(char* s)
{
    uint32_t n = slen(s);
    while (n > 0 && (s[n - 1u] == ' ' || s[n - 1u] == '\t' ||
                     s[n - 1u] == '\r' || s[n - 1u] == '\n')) {
        s[--n] = '\0';
    }
}

static const char* value_after_key(const char* line, const char* key)
{
    uint32_t i = 0;
    if (!line || !key) return NULL;
    while (key[i]) {
        if (line[i] != key[i]) return NULL;
        i++;
    }
    if (line[i] != ' ' && line[i] != '\t' && line[i] != '=') return NULL;
    while (line[i] == ' ' || line[i] == '\t' || line[i] == '=') i++;
    return line + i;
}

static void defaults_for(kmo_module_t* m, const char* file)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->used = 1;
    m->writable = fs_open_writable(file) ? 1 : 0;
    scopy(m->file, sizeof(m->file), file ? file : "module.kmo");
    scopy(m->id, sizeof(m->id), file ? file : "module");
    scopy(m->name, sizeof(m->name), "KMO Module");
    scopy(m->target, sizeof(m->target), "boot");
    scopy(m->help, sizeof(m->help), "File-defined kernel module route");
    scopy(m->default_msg, sizeof(m->default_msg), "status");
}

static int parse_line(kmo_module_t* m, const char* src)
{
    char line[224];
    const char* v;
    uint32_t i = 0;
    if (!m || !src) return -1;
    while (src[i] && i + 1u < sizeof(line)) {
        line[i] = src[i];
        i++;
    }
    line[i] = '\0';
    strip_tail(line);
    src = skip_ws(line);
    if (!src[0] || src[0] == '#') return 0;
    if (strncmp(src, "KMO", 3) == 0) return 0;
    if ((v = value_after_key(src, "ID")) != NULL) { scopy(m->id, sizeof(m->id), v); return 0; }
    if ((v = value_after_key(src, "NAME")) != NULL) { scopy(m->name, sizeof(m->name), v); return 0; }
    if ((v = value_after_key(src, "TARGET")) != NULL) { scopy(m->target, sizeof(m->target), v); return 0; }
    if ((v = value_after_key(src, "MODULE")) != NULL) { scopy(m->target, sizeof(m->target), v); return 0; }
    if ((v = value_after_key(src, "HELP")) != NULL) { scopy(m->help, sizeof(m->help), v); return 0; }
    if ((v = value_after_key(src, "DEFAULT")) != NULL) { scopy(m->default_msg, sizeof(m->default_msg), v); return 0; }
    if ((v = value_after_key(src, "MESSAGE")) != NULL) { scopy(m->default_msg, sizeof(m->default_msg), v); return 0; }
    if ((v = value_after_key(src, "CMD")) != NULL) { scopy(m->default_msg, sizeof(m->default_msg), v); return 0; }
    if ((v = value_after_key(src, "TEXT")) != NULL) { append_body_line(m->body, sizeof(m->body), v); return 0; }
    if ((v = value_after_key(src, "BODY")) != NULL) { append_body_line(m->body, sizeof(m->body), v); return 0; }
    return 0;
}

static int parse_kmo(kmo_module_t* m, const char* file, const char* data, uint32_t size)
{
    char line[224];
    uint32_t lp = 0;
    int saw_magic = 0;
    defaults_for(m, file);
    if (!data || size == 0) return -1;
    for (uint32_t i = 0; i < size; i++) {
        char c = data[i];
        if (c == '\n' || lp + 1u >= sizeof(line)) {
            line[lp] = '\0';
            if (strncmp(skip_ws(line), "KMO", 3) == 0) saw_magic = 1;
            parse_line(m, line);
            lp = 0;
        } else {
            line[lp++] = c;
        }
    }
    if (lp > 0) {
        line[lp] = '\0';
        if (strncmp(skip_ws(line), "KMO", 3) == 0) saw_magic = 1;
        parse_line(m, line);
    }
    if (!saw_magic) return -2;
    if (!m->target[0]) return -3;
    if (!m->default_msg[0]) scopy(m->default_msg, sizeof(m->default_msg), "status");
    if (!m->body[0]) append_body_line(m->body, sizeof(m->body), "This KMO routes a file-defined module request through KModTalk.");
    return 0;
}

static int already_loaded(const char* name)
{
    for (uint32_t i = 0; i < s_kmo_count; i++) {
        if (strcmp(s_kmos[i].file, name) == 0) return 1;
    }
    return 0;
}

void kmo_reset(void)
{
    memset(s_kmos, 0, sizeof(s_kmos));
    s_kmo_count = 0;
}

int kmo_load_file(const char* name)
{
    const FsFile* f;
    kmo_module_t m;
    if (!name || !name[0] || s_kmo_count >= KMO_MAX_MODULES) return -1;
    if (!has_suffix_ci(name, ".kmo")) return -2;
    if (already_loaded(name)) return 0;
    f = fs_open(name);
    if (!f || !f->data || f->size == 0) return -3;
    if (parse_kmo(&m, name, (const char*)f->data, f->size) != 0) return -4;
    s_kmos[s_kmo_count++] = m;
    return 0;
}

static void scan_cb(const char* name, uint32_t size, void* user)
{
    (void)size;
    (void)user;
    if (has_suffix_ci(name, ".kmo")) (void)kmo_load_file(name);
}

uint32_t kmo_reload(void)
{
    kmo_reset();
    fs_list(scan_cb, NULL);
    return s_kmo_count;
}

uint32_t kmo_count(void)
{
    return s_kmo_count;
}

const kmo_module_t* kmo_get(uint32_t index)
{
    if (index >= s_kmo_count) return NULL;
    return &s_kmos[index];
}

static int parse_index_key(const char* key, uint32_t* out)
{
    uint32_t v = 0;
    uint32_t i = 0;
    key = skip_ws(key);
    if (!key || key[0] < '0' || key[0] > '9') return -1;
    while (key[i] >= '0' && key[i] <= '9') {
        v = v * 10u + (uint32_t)(key[i] - '0');
        i++;
    }
    if (key[i]) return -1;
    if (out) *out = v;
    return 0;
}

const kmo_module_t* kmo_find(const char* key, uint32_t* index_out)
{
    uint32_t idx;
    if (s_kmo_count == 0) (void)kmo_reload();
    if (!key || !key[0]) return NULL;
    if (parse_index_key(key, &idx) == 0) {
        if (idx < s_kmo_count) {
            if (index_out) *index_out = idx;
            return &s_kmos[idx];
        }
        return NULL;
    }
    for (uint32_t i = 0; i < s_kmo_count; i++) {
        if (streq_ci(s_kmos[i].file, key) || streq_ci(s_kmos[i].id, key) ||
            streq_ci(s_kmos[i].name, key)) {
            if (index_out) *index_out = i;
            return &s_kmos[i];
        }
    }
    return NULL;
}

int kmo_format(const char* key, char* out, uint32_t out_cap)
{
    const kmo_module_t* m = kmo_find(key, NULL);
    if (!out || out_cap == 0) return -1;
    if (!m) {
        scopy(out, out_cap, "kmo: module not found");
        return -2;
    }
    snprintf(out, out_cap,
             "KMO %s\nfile: %s\nid: %s\ntarget: %s\ndefault: %s\nwritable: %s\nhelp: %s\n\n%s",
             m->name, m->file, m->id, m->target, m->default_msg,
             m->writable ? "yes" : "no", m->help, m->body);
    return 0;
}

int kmo_run(const char* key, const char* message, char* out, uint32_t out_cap)
{
    const kmo_module_t* m = kmo_find(key, NULL);
    const char* msg;
    int r;
    if (!out || out_cap == 0) return -1;
    out[0] = '\0';
    if (!m) {
        scopy(out, out_cap, "kmo: module not found");
        return -2;
    }
    msg = skip_ws(message);
    if (!msg[0]) msg = m->default_msg;
    r = kmodtalk_send(m->target, msg, out, out_cap);
    lardkit_trace_event("kmo", m->id, r);
    lardkit_journal_event("kmo", m->file);
    return r;
}

static int normalize_kmo_name(const char* in, char* out, uint32_t cap)
{
    uint32_t i = 0;
    if (!in || !in[0] || !out || cap == 0) return -1;
    while (in[i] && in[i] != ' ' && in[i] != '\t' && i + 1u < cap) {
        out[i] = in[i];
        i++;
    }
    out[i] = '\0';
    if (i == 0) return -1;
    if (in[i] && in[i] != ' ' && in[i] != '\t') return -2;
    if (!has_suffix_ci(out, ".kmo")) {
        if (i + 4u >= cap) return -2;
        out[i++] = '.';
        out[i++] = 'k';
        out[i++] = 'm';
        out[i++] = 'o';
        out[i] = '\0';
    }
    return 0;
}

static void make_id_from_name(const char* name, char* out, uint32_t cap)
{
    uint32_t i = 0;
    uint32_t j = 0;
    if (!out || cap == 0) return;
    while (name && name[i] && name[i] != '.' && j + 1u < cap) {
        char c = name[i++];
        out[j++] = (c == '_' || c == ' ') ? '-' : lower_char(c);
    }
    out[j] = '\0';
    if (!out[0]) scopy(out, cap, "user-kmo");
}

typedef struct {
    char name[32];
} kmo_slot_search_t;

static void free_slot_cb(const char* name, uint32_t size, void* user)
{
    kmo_slot_search_t* ctx = (kmo_slot_search_t*)user;
    FsWritableFile* w;
    kmo_module_t tmp;
    if (!ctx || ctx->name[0] || !has_suffix_ci(name, ".kmo")) return;
    w = fs_open_writable(name);
    if (!w) return;
    if (size == 0 || parse_kmo(&tmp, name, (const char*)w->data, w->size) != 0) {
        scopy(ctx->name, sizeof(ctx->name), name);
    }
}

static int find_free_slot(char* out, uint32_t cap)
{
    kmo_slot_search_t ctx;
    ctx.name[0] = '\0';
    fs_list_writable(free_slot_cb, &ctx);
    if (!ctx.name[0]) return -1;
    scopy(out, cap, ctx.name);
    return 0;
}

static void write_text_lines(char* out, uint32_t cap, const char* key, const char* text)
{
    uint32_t i = 0;
    uint32_t start = 0;
    if (!text || !text[0]) return;
    while (1) {
        if (text[i] == '\n' || text[i] == '\0') {
            char line[160];
            uint32_t n = 0;
            while (start < i && n + 1u < sizeof(line)) line[n++] = text[start++];
            line[n] = '\0';
            sappend(out, cap, key);
            sappend(out, cap, " ");
            sappend(out, cap, line);
            sappend(out, cap, "\n");
            if (text[i] == '\0') break;
            start = i + 1u;
        }
        i++;
    }
}

static int write_kmo_file(FsWritableFile* w, const kmo_module_t* m)
{
    char data[1536];
    uint32_t len;
    if (!w || !m) return -1;
    data[0] = '\0';
    sappend(data, sizeof(data), "KMO 1\nID ");
    sappend(data, sizeof(data), m->id);
    sappend(data, sizeof(data), "\nNAME ");
    sappend(data, sizeof(data), m->name);
    sappend(data, sizeof(data), "\nTARGET ");
    sappend(data, sizeof(data), m->target);
    sappend(data, sizeof(data), "\nHELP ");
    sappend(data, sizeof(data), m->help);
    sappend(data, sizeof(data), "\nDEFAULT ");
    sappend(data, sizeof(data), m->default_msg);
    sappend(data, sizeof(data), "\n");
    write_text_lines(data, sizeof(data), "TEXT", m->body);
    len = slen(data);
    if (len >= w->cap) return -2;
    (void)fs_write(w, 0, (const uint8_t*)data, len);
    return 0;
}

static FsWritableFile* prepare_writable_kmo(const char* name, const FsFile* source)
{
    FsWritableFile* w = fs_open_writable(name);
    char slot[32];
    if (w) return w;
    if (!source || !fs_open_readonly(name)) return NULL;
    if (find_free_slot(slot, sizeof(slot)) != 0) return NULL;
    if (fs_delete_readonly(name) != 0) return NULL;
    if (fs_rename_writable(slot, name) != 0) return NULL;
    return fs_open_writable(name);
}

int kmo_create(const char* name, const char* target, const char* default_msg)
{
    char file[32];
    char slot[32];
    FsWritableFile* w;
    kmo_module_t m;
    if (normalize_kmo_name(name, file, sizeof(file)) != 0) return -1;
    w = fs_open_writable(file);
    if ((w && w->size != 0) || fs_open_readonly(file)) return -2;
    if (!w) {
        if (find_free_slot(slot, sizeof(slot)) != 0) return -3;
        if (fs_rename_writable(slot, file) != 0) return -4;
        w = fs_open_writable(file);
    }
    if (!w) return -5;
    defaults_for(&m, file);
    make_id_from_name(file, m.id, sizeof(m.id));
    scopy(m.name, sizeof(m.name), m.id);
    scopy(m.target, sizeof(m.target), target && target[0] ? target : "boot");
    scopy(m.default_msg, sizeof(m.default_msg), default_msg && default_msg[0] ? default_msg : "status");
    scopy(m.help, sizeof(m.help), "User-created KMO file; edit with kmo set or write.");
    scopy(m.body, sizeof(m.body), "User-owned kernel module file. Change TARGET/DEFAULT/TEXT, then run kmo reload.\n");
    if (write_kmo_file(w, &m) != 0) return -6;
    (void)kmo_reload();
    lardkit_trace_event("kmo", "create", 0);
    lardkit_journal_event("kmo", "created user KMO");
    return 0;
}

int kmo_set_field(const char* name, const char* field, const char* value)
{
    char file[32];
    const FsFile* f;
    FsWritableFile* w;
    kmo_module_t m;
    if (normalize_kmo_name(name, file, sizeof(file)) != 0) return -1;
    if (!field || !field[0] || !value) return -2;
    f = fs_open(file);
    if (!f || !f->data || f->size == 0) return -3;
    if (parse_kmo(&m, file, (const char*)f->data, f->size) != 0) return -4;
    if (streq_ci(field, "id")) scopy(m.id, sizeof(m.id), value);
    else if (streq_ci(field, "name")) scopy(m.name, sizeof(m.name), value);
    else if (streq_ci(field, "target") || streq_ci(field, "module")) scopy(m.target, sizeof(m.target), value);
    else if (streq_ci(field, "help")) scopy(m.help, sizeof(m.help), value);
    else if (streq_ci(field, "default") || streq_ci(field, "message") || streq_ci(field, "cmd")) scopy(m.default_msg, sizeof(m.default_msg), value);
    else if (streq_ci(field, "text") || streq_ci(field, "body")) scopy(m.body, sizeof(m.body), value);
    else return -5;
    w = prepare_writable_kmo(file, f);
    if (!w) return -6;
    if (write_kmo_file(w, &m) != 0) return -7;
    (void)kmo_reload();
    lardkit_trace_event("kmo", "set", 0);
    lardkit_journal_event("kmo", "changed KMO file");
    return 0;
}

int kmo_delete(const char* name)
{
    char file[32];
    FsWritableFile* w;
    if (normalize_kmo_name(name, file, sizeof(file)) != 0) return -1;
    w = fs_open_writable(file);
    if (w) {
        static const uint8_t empty[] = "";
        (void)fs_write(w, 0, empty, 0);
        (void)kmo_reload();
        lardkit_trace_event("kmo", "delete", 0);
        lardkit_journal_event("kmo", "deleted writable KMO");
        return 0;
    }
    if (fs_open_readonly(file)) {
        int r = fs_delete_readonly(file);
        (void)kmo_reload();
        lardkit_trace_event("kmo", "delete-ro", r);
        lardkit_journal_event("kmo", "deleted read-only KMO");
        return r == 0 ? 0 : -2;
    }
    return -3;
}

int kmo_selftest(void)
{
    static const char sample[] =
        "KMO 1\n"
        "ID test-kmo\n"
        "NAME Test KMO\n"
        "TARGET boot\n"
        "HELP status route\n"
        "DEFAULT status\n"
        "TEXT hello\n";
    kmo_module_t m;
    char out[256];
    if (parse_kmo(&m, "test.kmo", sample, sizeof(sample) - 1) != 0) return -1;
    if (strcmp(m.id, "test-kmo") != 0) return -2;
    if (strcmp(m.target, "boot") != 0) return -3;
    if (strcmp(m.default_msg, "status") != 0) return -4;
    if (kmodtalk_send("boot", "status", out, sizeof(out)) != 0) return -5;
    if (!out[0]) return -6;
    return 0;
}
