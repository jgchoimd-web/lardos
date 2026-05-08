#include "crashlog.h"

#include "fs.h"
#include "version.h"

#include <stdint.h>

static uint32_t s_crashlog_count;
static char s_crashlog_view[1024];

static void append_s(FsWritableFile* w, const char* s)
{
    uint32_t n = 0;
    if (!w || !s) return;
    while (s[n]) n++;
    (void)fs_append(w, (const uint8_t*)s, n);
}

static void append_u32(FsWritableFile* w, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    if (v == 0) {
        append_s(w, "0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n) {
        char c = tmp[--n];
        (void)fs_append(w, (const uint8_t*)&c, 1);
    }
}

static void append_hex64(FsWritableFile* w, uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    append_s(w, "0x");
    for (int i = 15; i >= 0; i--) {
        char c = hex[(v >> ((uint32_t)i * 4u)) & 0xFu];
        (void)fs_append(w, (const uint8_t*)&c, 1);
    }
}

static FsWritableFile* log_file(void)
{
    return fs_open_writable("crashlog.txt");
}

void crashlog_init(void)
{
    FsWritableFile* w = log_file();
    if (w && w->size == 0) {
        append_s(w, "LardOS crashlog ");
        append_s(w, LARDOS_VERSION);
        append_s(w, "\n");
    }
}

void crashlog_record(const char* kind, const char* message)
{
    FsWritableFile* w = log_file();
    if (!w) return;
    if (w->size + 160u >= w->cap) {
        w->size = 0;
        append_s(w, "LardOS crashlog rotated\n");
    }
    s_crashlog_count++;
    append_s(w, "#");
    append_u32(w, s_crashlog_count);
    append_s(w, " ");
    append_s(w, kind ? kind : "event");
    append_s(w, ": ");
    append_s(w, message ? message : "(null)");
    append_s(w, "\n");
}

void crashlog_record_u64(const char* kind, const char* message, uint64_t value)
{
    FsWritableFile* w;
    crashlog_record(kind, message);
    w = log_file();
    if (!w) return;
    append_s(w, "  value=");
    append_hex64(w, value);
    append_s(w, "\n");
}

void crashlog_record_panic(const char* message)
{
    crashlog_record("panic", message);
    (void)fs_persist_save();
}

void crashlog_record_panic_u64(const char* message, uint64_t value)
{
    crashlog_record_u64("panic", message, value);
    (void)fs_persist_save();
}

int crashlog_clear(void)
{
    FsWritableFile* w = log_file();
    if (!w) return -1;
    w->size = 0;
    append_s(w, "LardOS crashlog cleared\n");
    return 0;
}

const char* crashlog_text(void)
{
    const FsFile* f = fs_open("crashlog.txt");
    uint32_t n = 0;
    if (!f || !f->data) return "";
    while (n + 1u < sizeof(s_crashlog_view) && n < f->size) {
        s_crashlog_view[n] = (char)f->data[n];
        n++;
    }
    s_crashlog_view[n] = '\0';
    return s_crashlog_view;
}

uint32_t crashlog_count(void)
{
    return s_crashlog_count;
}

int crashlog_selftest(void)
{
    FsWritableFile* w = log_file();
    uint32_t old_size;
    uint32_t old_count = s_crashlog_count;
    if (!w) return -1;
    old_size = w->size;
    crashlog_record("test", "selftest");
    if (s_crashlog_count != old_count + 1u || w->size <= old_size) return -2;
    w->size = old_size;
    s_crashlog_count = old_count;
    return 0;
}
