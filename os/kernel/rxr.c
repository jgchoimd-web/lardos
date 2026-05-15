#include "rxr.h"

#include "fs.h"
#include "lardkit.h"
#include "string.h"

#include <stdint.h>

#define RXR_UNDO_BYTES_MAX 8192u

typedef struct {
    char name[RXR_NAME_MAX + 1u];
    uint32_t offset;
    uint32_t size;
    uint32_t created;
} rxr_undo_file_t;

typedef struct {
    rxr_undo_file_t files[RXR_UNDO_MAX_FILES];
    uint8_t data[RXR_UNDO_BYTES_MAX];
    uint32_t count;
    uint32_t used;
    uint32_t ready;
} rxr_undo_state_t;

static rxr_undo_state_t s_rxr_undo;

static int line_eq(const char* p, const char* end, const char* s)
{
    while (p < end && *s && *p == *s) {
        p++;
        s++;
    }
    return p == end && *s == '\0';
}

static int line_prefix(const char* p, const char* end, const char* prefix, const char** value)
{
    while (p < end && *prefix && *p == *prefix) {
        p++;
        prefix++;
    }
    if (*prefix) return 0;
    if (p < end && *p != ' ' && *p != '\t') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (value) *value = p;
    return 1;
}

static int next_line(const char** p, const char* end, const char** ls, const char** le)
{
    const char* s = *p;
    if (s >= end) return 0;
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

static void copy_name(char* out, uint32_t cap, const char* p, const char* end)
{
    uint32_t n = 0;
    if (!out || cap == 0) return;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (p < end && *p != ' ' && *p != '\t' && n + 1u < cap) out[n++] = *p++;
    out[n] = '\0';
}

static void copy_cstr(char* dst, uint32_t cap, const char* src)
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

static int cstr_eq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static char lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int suffix_ci(const char* name, const char* suffix)
{
    uint32_t nl = 0;
    uint32_t sl = 0;
    if (!name || !suffix) return 0;
    while (name[nl]) nl++;
    while (suffix[sl]) sl++;
    if (nl < sl) return 0;
    for (uint32_t i = 0; i < sl; i++) {
        if (lower(name[nl - sl + i]) != lower(suffix[i])) return 0;
    }
    return 1;
}

static int is_app_file(const char* name)
{
    return suffix_ci(name, ".rxe") || suffix_ci(name, ".sysrxe");
}

static uint32_t hash_bytes(const uint8_t* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static int valid_rxr(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    if (!data || len < 6u) return 0;
    if (!next_line(&p, end, &ls, &le)) return 0;
    return line_eq(ls, le, "RXR 1");
}

static void undo_reset(void)
{
    for (uint32_t i = 0; i < sizeof(s_rxr_undo); i++) ((uint8_t*)&s_rxr_undo)[i] = 0;
}

static int undo_index(const char* name)
{
    for (uint32_t i = 0; i < s_rxr_undo.count; i++) {
        if (cstr_eq(s_rxr_undo.files[i].name, name)) return (int)i;
    }
    return -1;
}

static int undo_capture(FsWritableFile* w, uint32_t created)
{
    rxr_undo_file_t* u;
    if (!w) return -1;
    if (undo_index(w->name) >= 0) return 0;
    if (s_rxr_undo.count >= RXR_UNDO_MAX_FILES) return -2;
    if (w->size > RXR_UNDO_BYTES_MAX - s_rxr_undo.used) return -3;
    u = &s_rxr_undo.files[s_rxr_undo.count++];
    copy_cstr(u->name, sizeof(u->name), w->name);
    u->offset = s_rxr_undo.used;
    u->size = w->size;
    u->created = created ? 1u : 0u;
    for (uint32_t i = 0; i < w->size; i++) s_rxr_undo.data[u->offset + i] = w->data[i];
    s_rxr_undo.used += w->size;
    s_rxr_undo.ready = 1u;
    return 0;
}

int rxr_file_count(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    int count = 0;
    if (!valid_rxr(data, len)) return -1;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) {
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            count++;
            p = q;
        }
    }
    return count;
}

int rxr_file_at(const uint8_t* data, uint32_t len, uint32_t index, rxr_file_info_t* out)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    uint32_t count = 0;
    if (!out || !valid_rxr(data, len)) return -1;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) {
            const char* body = p;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            if (count == index) {
                copy_name(out->name, sizeof(out->name), v, le);
                out->size = (uint32_t)(fs > body ? fs - body : 0);
                out->is_app = is_app_file(out->name) ? 1u : 0u;
                return 0;
            }
            count++;
            p = q;
        }
    }
    return -2;
}

int rxr_verify(const uint8_t* data, uint32_t len, rxr_verify_info_t* out)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    rxr_verify_info_t info;
    uint32_t saw_end = 0;
    uint32_t saw_app = 0;
    uint32_t primary_in_files = 0;
    uint32_t new_files = 0;
    for (uint32_t i = 0; i < sizeof(info); i++) ((uint8_t*)&info)[i] = 0;
    info.hash = hash_bytes(data, len);
    if (!valid_rxr(data, len)) {
        info.errors = 1u;
        if (out) *out = info;
        return -1;
    }
    info.valid = 1u;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_eq(ls, le, "END")) {
            saw_end = 1u;
            break;
        }
        if (line_prefix(ls, le, "APP", &v)) {
            copy_name(info.primary_app, sizeof(info.primary_app), v, le);
            saw_app = 1u;
            if (!is_app_file(info.primary_app)) info.errors++;
        } else if (line_prefix(ls, le, "FILE", &v)) {
            char name[RXR_NAME_MAX + 1u];
            const char* body = p;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            uint32_t found = 0;
            uint32_t sz;
            uint32_t cap;
            copy_name(name, sizeof(name), v, le);
            if (!name[0]) info.errors++;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) {
                    found = 1u;
                    break;
                }
            }
            if (!found) info.errors++;
            sz = (uint32_t)(fs > body ? fs - body : 0);
            cap = fs_writable_capacity_for(name);
            info.files++;
            info.total_bytes += sz;
            if (is_app_file(name)) {
                info.app_files++;
                if (!info.primary_app[0]) copy_cstr(info.primary_app, sizeof(info.primary_app), name);
            }
            if (info.primary_app[0] && cstr_eq(name, info.primary_app)) primary_in_files = 1u;
            if (!fs_open_writable(name) && !fs_open_readonly(name)) new_files++;
            if (cap && sz <= cap && fs_can_create_writable(name)) info.installable++;
            else info.errors++;
            p = q;
        }
    }
    if (!saw_end) info.warnings++;
    if (new_files > fs_creatable_writable_slots()) info.errors++;
    if (info.files == 0u || info.app_files == 0u || !info.primary_app[0] || !saw_app || !primary_in_files) info.errors++;
    if (out) *out = info;
    if (info.valid) {
        lardkit_trace_event("rxr", "verify", (int32_t)info.hash);
        lardkit_journal_event("rxr", info.errors ? "verify failed" : "verify ok");
    }
    return info.errors ? -2 : 0;
}

int rxr_install(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    int installed = 0;
    rxr_verify_info_t vi;
    if (rxr_verify(data, len, &vi) != 0) return -1;
    lardkit_trace_event("rxr", "install", (int32_t)vi.hash);
    undo_reset();
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) {
            char name[RXR_NAME_MAX + 1u];
            FsWritableFile* w;
            uint32_t existed;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            copy_name(name, sizeof(name), v, le);
            existed = fs_open_writable(name) ? 1u : 0u;
            w = fs_open_or_create_writable(name);
            if (!w || undo_capture(w, existed ? 0u : 1u) != 0) {
                undo_reset();
                return -3;
            }
            p = q;
        }
    }
    p = (const char*)data;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) {
            char name[RXR_NAME_MAX + 1u];
            const char* body = p;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            FsWritableFile* w;
            uint32_t sz;
            uint32_t written;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            copy_name(name, sizeof(name), v, le);
            w = fs_open_or_create_writable(name);
            sz = (uint32_t)(fs > body ? fs - body : 0);
            if (!w || sz > w->cap) {
                undo_reset();
                return -4;
            }
            written = fs_write(w, 0, (const uint8_t*)body, sz);
            w->size = written;
            if (written != sz) {
                undo_reset();
                return -5;
            }
            installed++;
            p = q;
        }
    }
    if (installed > 0) lardkit_journal_event("rxr", "installed app bundle");
    return installed;
}

static const char* rxr_slot_name(uint32_t idx)
{
    if (idx == 0) return "rxrslot0.dat";
    if (idx == 1) return "rxrslot1.dat";
    if (idx == 2) return "rxrslot2.dat";
    return "rxrslot3.dat";
}

static void release_created_slot(const char* name)
{
    if (!name || !fs_open_writable(name)) return;
    for (uint32_t i = 0; i < 4u; i++) {
        const char* slot = rxr_slot_name(i);
        if (!fs_open_writable(slot)) {
            (void)fs_rename_writable(name, slot);
            return;
        }
    }
}

int rxr_undo_last(void)
{
    uint32_t restored = 0;
    if (!s_rxr_undo.ready) return -1;
    for (uint32_t i = 0; i < s_rxr_undo.count; i++) {
        rxr_undo_file_t* u = &s_rxr_undo.files[i];
        FsWritableFile* w = fs_open_writable(u->name);
        if (!w || u->offset > RXR_UNDO_BYTES_MAX || u->size > RXR_UNDO_BYTES_MAX - u->offset) continue;
        (void)fs_write(w, 0, s_rxr_undo.data + u->offset, u->size);
        w->size = u->size;
        if (u->created && u->size == 0u) release_created_slot(u->name);
        restored++;
    }
    undo_reset();
    if (restored) lardkit_journal_event("rxr", "undo restored bundle files");
    return (int)restored;
}

void rxr_undo_info(rxr_undo_info_t* out)
{
    if (!out) return;
    out->ready = s_rxr_undo.ready;
    out->files = s_rxr_undo.count;
    out->bytes = s_rxr_undo.used;
}

int rxr_selftest(void)
{
    static const uint8_t pack[] =
        "RXR 1\n"
        "NAME demo\n"
        "APP userapp.sysrxe\n"
        "FILE userapp.sysrxe\n"
        "SYSRXE 1\n"
        "ID rxr-selftest\n"
        "NAME RXR Selftest\n"
        "LANG C\n"
        "CODE println(\"rxr\");\n"
        "ENDFILE\n"
        "FILE notes.txt\n"
        "needed data\n"
        "ENDFILE\n"
        "END\n";
    rxr_file_info_t info;
    rxr_verify_info_t vi;
    if (rxr_file_count(pack, sizeof(pack) - 1) != 2) return -1;
    if (rxr_file_at(pack, sizeof(pack) - 1, 0, &info) != 0) return -2;
    if (!info.is_app || info.size == 0) return -3;
    if (rxr_verify(pack, sizeof(pack) - 1, &vi) != 0) return -4;
    if (!vi.valid || vi.files != 2u || vi.app_files != 1u || vi.installable != 2u || vi.hash == 0u) return -5;
    if (!cstr_eq(vi.primary_app, "userapp.sysrxe")) return -6;
    return 0;
}
