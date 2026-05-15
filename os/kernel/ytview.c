#include "ytview.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

static char yt_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int yt_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int yt_id_char(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static int yt_prefix_ci(const char* s, const char* p)
{
    uint32_t i = 0;
    if (!s || !p) return 0;
    while (p[i]) {
        if (!s[i] || yt_lower(s[i]) != yt_lower(p[i])) return 0;
        i++;
    }
    return 1;
}

static const char* yt_find_ci(const char* s, const char* needle)
{
    uint32_t i = 0;
    if (!s || !needle || !needle[0]) return NULL;
    while (s[i]) {
        if (yt_prefix_ci(&s[i], needle)) return &s[i];
        i++;
    }
    return NULL;
}

static uint32_t yt_trim(const char* s, uint32_t* start, uint32_t* end)
{
    uint32_t a = 0;
    uint32_t b = 0;
    if (!s) {
        if (start) *start = 0;
        if (end) *end = 0;
        return 0;
    }
    while (s[b]) b++;
    while (a < b && yt_space(s[a])) a++;
    while (b > a && yt_space(s[b - 1u])) b--;
    if (start) *start = a;
    if (end) *end = b;
    return b - a;
}

static int yt_copy_id_at(const char* s, uint32_t pos, char* out)
{
    if (!s || !out) return -1;
    for (uint32_t i = 0; i < 11u; i++) {
        char c = s[pos + i];
        if (!yt_id_char(c)) return -1;
        out[i] = c;
    }
    if (yt_id_char(s[pos + 11u])) return -1;
    out[11] = '\0';
    return 0;
}

static int yt_copy_raw_id(const char* s, uint32_t start, uint32_t end, char* out)
{
    if (!s || !out || end <= start || end - start != 11u) return -1;
    for (uint32_t i = 0; i < 11u; i++) {
        char c = s[start + i];
        if (!yt_id_char(c)) return -1;
        out[i] = c;
    }
    out[11] = '\0';
    return 0;
}

static int yt_find_query_id(const char* s, char* out)
{
    uint32_t i = 0;
    if (!s || !out) return -1;
    while (s[i]) {
        int boundary = (i == 0u || s[i - 1u] == '?' || s[i - 1u] == '&');
        if (boundary && s[i] == 'v' && s[i + 1u] == '=') {
            return yt_copy_id_at(s, i + 2u, out);
        }
        i++;
    }
    return -1;
}

static int yt_find_path_id(const char* s, const char* marker, char* out)
{
    const char* p = yt_find_ci(s, marker);
    if (!p) return -1;
    return yt_copy_id_at(p, (uint32_t)strlen(marker), out);
}

static void yt_append(char* out, uint32_t cap, const char* text)
{
    uint32_t i = 0;
    if (!out || cap == 0 || !text) return;
    while (i + 1u < cap && out[i]) i++;
    while (*text && i + 1u < cap) out[i++] = *text++;
    out[i] = '\0';
}

static void yt_fill_urls(ytview_info_t* out)
{
    out->watch_url[0] = '\0';
    out->embed_url[0] = '\0';
    out->thumb_url[0] = '\0';
    yt_append(out->watch_url, sizeof(out->watch_url), "https://www.youtube.com/watch?v=");
    yt_append(out->watch_url, sizeof(out->watch_url), out->id);
    yt_append(out->embed_url, sizeof(out->embed_url), "https://www.youtube.com/embed/");
    yt_append(out->embed_url, sizeof(out->embed_url), out->id);
    yt_append(out->thumb_url, sizeof(out->thumb_url), "https://i.ytimg.com/vi/");
    yt_append(out->thumb_url, sizeof(out->thumb_url), out->id);
    yt_append(out->thumb_url, sizeof(out->thumb_url), "/hqdefault.jpg");
    out->valid = 1u;
}

int ytview_parse_url(const char* input, ytview_info_t* out)
{
    uint32_t start;
    uint32_t end;
    const char* s;
    const char* p;
    if (!input || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (yt_trim(input, &start, &end) == 0) return -2;
    if (yt_copy_raw_id(input, start, end, out->id) == 0) {
        yt_fill_urls(out);
        return 0;
    }
    s = input + start;

    p = yt_find_ci(s, "youtu.be/");
    if (p && yt_copy_id_at(p, 9u, out->id) == 0) {
        yt_fill_urls(out);
        return 0;
    }

    p = yt_find_ci(s, "youtube.com/");
    if (!p) p = yt_find_ci(s, "youtube-nocookie.com/");
    if (!p) return -3;

    if (yt_find_path_id(p, "/shorts/", out->id) == 0) {
        out->shorts = 1u;
        yt_fill_urls(out);
        return 0;
    }
    if (yt_find_path_id(p, "/embed/", out->id) == 0 ||
        yt_find_path_id(p, "/live/", out->id) == 0 ||
        yt_find_query_id(p, out->id) == 0) {
        yt_fill_urls(out);
        return 0;
    }
    return -4;
}

int ytview_format_card(const ytview_info_t* info, char* out, uint32_t cap)
{
    if (!info || !out || cap == 0 || !info->valid) return -1;
    out[0] = '\0';
    yt_append(out, cap, "YouTube Native View\n");
    yt_append(out, cap, "  id: ");
    yt_append(out, cap, info->id);
    yt_append(out, cap, "\n  kind: ");
    yt_append(out, cap, info->shorts ? "shorts" : "watch");
    yt_append(out, cap, "\n  watch: ");
    yt_append(out, cap, info->watch_url);
    yt_append(out, cap, "\n  embed: ");
    yt_append(out, cap, info->embed_url);
    yt_append(out, cap, "\n  thumbnail: ");
    yt_append(out, cap, info->thumb_url);
    yt_append(out, cap, "\n  status: URL recognized by in-tree C and opened as a LardOS-native watch card.\n");
    yt_append(out, cap, "  next: native stream selection, demux, codec, audio, and sync layers for real playback.\n");
    yt_append(out, cap, "  values: no external browser engine, no hidden JavaScript bridge, visible fetch targets.\n");
    return 0;
}

int ytview_format_lars(const ytview_info_t* info, char* out, uint32_t cap)
{
    if (!info || !out || cap == 0 || !info->valid) return -1;
    out[0] = '\0';
    yt_append(out, cap, "LARS 1\n");
    yt_append(out, cap, "title YouTube View\n");
    yt_append(out, cap, "p LardOS recognized this YouTube target without an external browser engine.\n");
    yt_append(out, cap, "p Video ID: ");
    yt_append(out, cap, info->id);
    yt_append(out, cap, info->shorts ? " (shorts)\n" : " (watch)\n");
    yt_append(out, cap, "p This is a native watch card. Full playback still needs native stream, codec, audio, and sync layers.\n");
    yt_append(out, cap, "fetch Watch page | ");
    yt_append(out, cap, info->watch_url);
    yt_append(out, cap, "\nfetch Embed page | ");
    yt_append(out, cap, info->embed_url);
    yt_append(out, cap, "\nfetch Thumbnail | ");
    yt_append(out, cap, info->thumb_url);
    yt_append(out, cap, "\nbutton Use HEAD | cfgsh http 3\n");
    yt_append(out, cap, "button NetWatch | netwatch on\n");
    yt_append(out, cap, "button WebStack status | webstack status\n");
    yt_append(out, cap, "input video ");
    yt_append(out, cap, info->id);
    yt_append(out, cap, "\nend\n");
    return 0;
}

int ytview_selftest(void)
{
    ytview_info_t info;
    char buf[1024];
    if (ytview_parse_url("https://www.youtube.com/watch?v=jNQXAC9IVRw", &info) != 0) return -1;
    if (strcmp(info.id, "jNQXAC9IVRw") != 0 || info.shorts) return -2;
    if (ytview_parse_url("https://youtu.be/jNQXAC9IVRw?t=1", &info) != 0) return -3;
    if (strcmp(info.id, "jNQXAC9IVRw") != 0) return -4;
    if (ytview_parse_url("https://www.youtube.com/shorts/aqz-KE-bpKQ", &info) != 0) return -5;
    if (!info.shorts || strcmp(info.id, "aqz-KE-bpKQ") != 0) return -6;
    if (ytview_format_card(&info, buf, sizeof(buf)) != 0 || !buf[0]) return -7;
    if (ytview_format_lars(&info, buf, sizeof(buf)) != 0 || strncmp(buf, "LARS 1", 6) != 0) return -8;
    if (ytview_parse_url("https://example.com/watch?v=jNQXAC9IVRw", &info) == 0) return -9;
    return 0;
}
