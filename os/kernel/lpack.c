#include "lpack.h"

#include "fs.h"

#include <stdint.h>

#define LPACK_UNDO_BYTES_MAX 4096u

typedef struct {
    char name[LPACK_NAME_MAX + 1u];
    uint32_t offset;
    uint32_t size;
} lpack_undo_file_t;

typedef struct {
    lpack_undo_file_t files[LPACK_UNDO_MAX_FILES];
    uint8_t data[LPACK_UNDO_BYTES_MAX];
    uint32_t count;
    uint32_t used;
    uint32_t ready;
} lpack_undo_state_t;

static lpack_undo_state_t s_lpack_undo;

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

static uint32_t lpack_hash_bytes(const uint8_t* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
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

static int valid_pack(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    if (!data || len < 7u) return 0;
    if (!next_line(&p, end, &ls, &le)) return 0;
    return line_eq(ls, le, "LPACK 1");
}

static void undo_reset(void)
{
    for (uint32_t i = 0; i < sizeof(s_lpack_undo); i++) ((uint8_t*)&s_lpack_undo)[i] = 0;
}

static int undo_index(const char* name)
{
    for (uint32_t i = 0; i < s_lpack_undo.count; i++) {
        if (cstr_eq(s_lpack_undo.files[i].name, name)) return (int)i;
    }
    return -1;
}

static int undo_capture(FsWritableFile* w)
{
    lpack_undo_file_t* u;
    if (!w) return -1;
    if (undo_index(w->name) >= 0) return 0;
    if (s_lpack_undo.count >= LPACK_UNDO_MAX_FILES) return -2;
    if (w->size > LPACK_UNDO_BYTES_MAX - s_lpack_undo.used) return -3;
    u = &s_lpack_undo.files[s_lpack_undo.count++];
    copy_cstr(u->name, sizeof(u->name), w->name);
    u->offset = s_lpack_undo.used;
    u->size = w->size;
    for (uint32_t i = 0; i < w->size; i++) s_lpack_undo.data[u->offset + i] = w->data[i];
    s_lpack_undo.used += w->size;
    s_lpack_undo.ready = 1u;
    return 0;
}

int lpack_file_count(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    int count = 0;
    if (!valid_pack(data, len)) return -1;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) count++;
    }
    return count;
}

int lpack_file_at(const uint8_t* data, uint32_t len, uint32_t index, lpack_file_info_t* out)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    uint32_t count = 0;
    if (!out || !valid_pack(data, len)) return -1;
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) {
            const char* body = p;
            const char* q = p;
            const char* fs;
            const char* fe;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            if (count == index) {
                copy_name(out->name, sizeof(out->name), v, le);
                out->size = (uint32_t)(fs > body ? fs - body : 0);
                return 0;
            }
            count++;
        }
    }
    return -2;
}

int lpack_verify(const uint8_t* data, uint32_t len, lpack_verify_info_t* out)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    lpack_verify_info_t info;
    uint32_t saw_end = 0;
    for (uint32_t i = 0; i < sizeof(info); i++) ((uint8_t*)&info)[i] = 0;
    info.hash = lpack_hash_bytes(data, len);
    if (!valid_pack(data, len)) {
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
        if (line_prefix(ls, le, "FILE", &v)) {
            char name[LPACK_NAME_MAX + 1u];
            const char* body = p;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            uint32_t found = 0;
            FsWritableFile* w;
            copy_name(name, sizeof(name), v, le);
            if (!name[0]) info.errors++;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) {
                    found = 1u;
                    break;
                }
            }
            if (!found) info.errors++;
            info.files++;
            if (fs > body) info.total_bytes += (uint32_t)(fs - body);
            w = fs_open_writable(name);
            if (w) {
                info.installable++;
                if (fs > body && (uint32_t)(fs - body) > w->cap) info.warnings++;
            } else {
                info.warnings++;
            }
            p = q;
        }
    }
    if (info.files == 0u) info.errors++;
    if (!saw_end) info.warnings++;
    if (out) *out = info;
    return info.errors ? -2 : 0;
}

int lpack_install(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    int installed = 0;
    lpack_verify_info_t vi;
    if (lpack_verify(data, len, &vi) != 0) return -1;
    undo_reset();
    while (next_line(&p, end, &ls, &le)) {
        const char* v;
        if (line_prefix(ls, le, "FILE", &v)) {
            char name[LPACK_NAME_MAX + 1u];
            FsWritableFile* w;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            copy_name(name, sizeof(name), v, le);
            w = fs_open_writable(name);
            if (w && undo_capture(w) != 0) {
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
            char name[LPACK_NAME_MAX + 1u];
            const char* body = p;
            const char* q = p;
            const char* fs = end;
            const char* fe = end;
            while (next_line(&q, end, &fs, &fe)) {
                if (line_eq(fs, fe, "ENDFILE")) break;
            }
            copy_name(name, sizeof(name), v, le);
            FsWritableFile* w = fs_open_writable(name);
            if (w && fs <= end) {
                uint32_t sz = (uint32_t)(fs > body ? fs - body : 0);
                uint32_t written = fs_write(w, 0, (const uint8_t*)body, sz);
                w->size = written;
                installed++;
            }
            p = q;
        }
    }
    return installed;
}

int lpack_undo_last(void)
{
    uint32_t restored = 0;
    if (!s_lpack_undo.ready) return -1;
    for (uint32_t i = 0; i < s_lpack_undo.count; i++) {
        lpack_undo_file_t* u = &s_lpack_undo.files[i];
        FsWritableFile* w = fs_open_writable(u->name);
        if (!w || u->offset > LPACK_UNDO_BYTES_MAX || u->size > LPACK_UNDO_BYTES_MAX - u->offset) continue;
        (void)fs_write(w, 0, s_lpack_undo.data + u->offset, u->size);
        w->size = u->size;
        restored++;
    }
    undo_reset();
    return (int)restored;
}

void lpack_undo_info(lpack_undo_info_t* out)
{
    if (!out) return;
    out->ready = s_lpack_undo.ready;
    out->files = s_lpack_undo.count;
    out->bytes = s_lpack_undo.used;
}

int lpack_selftest(void)
{
    static const uint8_t pack[] =
        "LPACK 1\n"
        "NAME demo\n"
        "FILE notes.txt\n"
        "hello\n"
        "ENDFILE\n"
        "END\n";
    lpack_file_info_t info;
    lpack_verify_info_t vi;
    if (lpack_file_count(pack, sizeof(pack) - 1) != 1) return -1;
    if (lpack_file_at(pack, sizeof(pack) - 1, 0, &info) != 0) return -2;
    if (info.name[0] != 'n' || info.size == 0) return -3;
    if (lpack_verify(pack, sizeof(pack) - 1, &vi) != 0) return -4;
    if (!vi.valid || vi.files != 1u || vi.installable != 1u || vi.hash == 0u) return -5;
    return 0;
}
