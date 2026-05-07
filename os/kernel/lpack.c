#include "lpack.h"

#include "fs.h"

#include <stdint.h>

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

int lpack_install(const uint8_t* data, uint32_t len)
{
    const char* p = (const char*)data;
    const char* end = p + len;
    const char* ls;
    const char* le;
    int installed = 0;
    if (!valid_pack(data, len)) return -1;
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
                (void)fs_write(w, 0, (const uint8_t*)body, sz);
                w->size = sz;
                installed++;
            }
            p = q;
        }
    }
    return installed;
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
    if (lpack_file_count(pack, sizeof(pack) - 1) != 1) return -1;
    if (lpack_file_at(pack, sizeof(pack) - 1, 0, &info) != 0) return -2;
    if (info.name[0] != 'n' || info.size == 0) return -3;
    return 0;
}
