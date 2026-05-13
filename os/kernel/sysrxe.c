#include "sysrxe.h"

#include "fs.h"
#include "lsh.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

static sysrxe_app_t s_apps[SYSRXE_MAX_APPS];
static uint32_t s_count;

static void copy_text(char* dst, uint32_t cap, const char* src)
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

static void append_text(char* dst, uint32_t cap, const char* src)
{
    uint32_t n = 0;
    uint32_t i = 0;
    if (!dst || cap == 0 || !src) return;
    while (dst[n] && n + 1u < cap) n++;
    while (src[i] && n + 1u < cap) dst[n++] = src[i++];
    if (n + 1u < cap) dst[n++] = '\n';
    dst[n] = '\0';
}

static const char* skip_ws(const char* s)
{
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static void strip_tail(char* s)
{
    uint32_t n = 0;
    if (!s) return;
    while (s[n]) n++;
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

static uint32_t parse_number(const char* s, uint32_t fallback)
{
    uint32_t v = 0;
    uint32_t i = 0;
    int any = 0;
    s = skip_ws(s);
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        i = 2;
        while (s[i]) {
            char c = s[i++];
            uint32_t d;
            if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
            else break;
            v = (v << 4) | d;
            any = 1;
        }
        return any ? v : fallback;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10u + (uint32_t)(s[i++] - '0');
        any = 1;
    }
    return any ? v : fallback;
}

static int has_suffix_ci(const char* name, const char* suffix)
{
    uint32_t nl = 0;
    uint32_t sl = 0;
    if (!name || !suffix) return 0;
    while (name[nl]) nl++;
    while (suffix[sl]) sl++;
    if (nl < sl) return 0;
    for (uint32_t i = 0; i < sl; i++) {
        char a = name[nl - sl + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static void defaults_for(sysrxe_app_t* app, const char* file)
{
    if (!app) return;
    memset(app, 0, sizeof(*app));
    app->used = 1;
    copy_text(app->file, sizeof(app->file), file);
    copy_text(app->id, sizeof(app->id), file ? file : "sysrxe");
    copy_text(app->name, sizeof(app->name), "SYSRXE App");
    copy_text(app->icon, sizeof(app->icon), "X");
    copy_text(app->input_label, sizeof(app->input_label), "Input:");
    copy_text(app->button_label, sizeof(app->button_label), "Run");
    app->color = 0xFF57B8A6u;
    app->show_desktop = 1;
    app->show_dock = 0;
}

static int parse_line(sysrxe_app_t* app, const char* src)
{
    char line[192];
    const char* v;
    uint32_t i = 0;
    if (!app || !src) return -1;
    while (src[i] && i + 1u < sizeof(line)) {
        line[i] = src[i];
        i++;
    }
    line[i] = '\0';
    strip_tail(line);
    src = skip_ws(line);
    if (!src[0] || src[0] == '#') return 0;
    if (strncmp(src, "SYSRXE", 6) == 0) return 0;
    if ((v = value_after_key(src, "ID")) != NULL) { copy_text(app->id, sizeof(app->id), v); return 0; }
    if ((v = value_after_key(src, "NAME")) != NULL) { copy_text(app->name, sizeof(app->name), v); return 0; }
    if ((v = value_after_key(src, "ICON")) != NULL) { copy_text(app->icon, sizeof(app->icon), v); return 0; }
    if ((v = value_after_key(src, "COLOR")) != NULL) { app->color = parse_number(v, app->color); return 0; }
    if ((v = value_after_key(src, "INPUT")) != NULL) { copy_text(app->input_label, sizeof(app->input_label), v); return 0; }
    if ((v = value_after_key(src, "BUTTON")) != NULL) { copy_text(app->button_label, sizeof(app->button_label), v); return 0; }
    if ((v = value_after_key(src, "COMMAND")) != NULL) { copy_text(app->command, sizeof(app->command), v); return 0; }
    if ((v = value_after_key(src, "CMD")) != NULL) { copy_text(app->command, sizeof(app->command), v); return 0; }
    if ((v = value_after_key(src, "TEXT")) != NULL) { append_text(app->body, sizeof(app->body), v); return 0; }
    if ((v = value_after_key(src, "BODY")) != NULL) { append_text(app->body, sizeof(app->body), v); return 0; }
    if ((v = value_after_key(src, "DESKTOP")) != NULL) { app->show_desktop = parse_number(v, 1u) ? 1 : 0; return 0; }
    if ((v = value_after_key(src, "DOCK")) != NULL) { app->show_dock = parse_number(v, 0u) ? 1 : 0; return 0; }
    return 0;
}

static int parse_sysrxe(sysrxe_app_t* app, const char* file, const char* data, uint32_t size)
{
    char line[192];
    uint32_t lp = 0;
    int saw_magic = 0;
    defaults_for(app, file);
    if (!data || size == 0) return -1;
    for (uint32_t i = 0; i < size; i++) {
        char c = data[i];
        if (c == '\n' || lp + 1u >= sizeof(line)) {
            line[lp] = '\0';
            if (strncmp(skip_ws(line), "SYSRXE", 6) == 0) saw_magic = 1;
            parse_line(app, line);
            lp = 0;
        } else {
            line[lp++] = c;
        }
    }
    if (lp > 0) {
        line[lp] = '\0';
        if (strncmp(skip_ws(line), "SYSRXE", 6) == 0) saw_magic = 1;
        parse_line(app, line);
    }
    if (!saw_magic) return -2;
    if (!app->body[0]) append_text(app->body, sizeof(app->body), "This app was loaded from SYSRXE.");
    return 0;
}

static int already_loaded(const char* name)
{
    for (uint32_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i].file, name) == 0) return 1;
    }
    return 0;
}

void sysrxe_reset(void)
{
    memset(s_apps, 0, sizeof(s_apps));
    s_count = 0;
}

int sysrxe_load_file(const char* name)
{
    const FsFile* f;
    sysrxe_app_t app;
    if (!name || !name[0] || s_count >= SYSRXE_MAX_APPS) return -1;
    if (!has_suffix_ci(name, ".sysrxe")) return -2;
    if (already_loaded(name)) return 0;
    f = fs_open(name);
    if (!f || !f->data || f->size == 0) return -3;
    if (parse_sysrxe(&app, name, (const char*)f->data, f->size) != 0) return -4;
    s_apps[s_count++] = app;
    return 0;
}

static void scan_cb(const char* name, uint32_t size, void* user)
{
    (void)size;
    (void)user;
    if (has_suffix_ci(name, ".sysrxe")) (void)sysrxe_load_file(name);
}

uint32_t sysrxe_reload(void)
{
    sysrxe_reset();
    fs_list(scan_cb, NULL);
    return s_count;
}

uint32_t sysrxe_count(void)
{
    return s_count;
}

const sysrxe_app_t* sysrxe_get(uint32_t index)
{
    if (index >= s_count) return NULL;
    return &s_apps[index];
}

int sysrxe_app_id(uint32_t index)
{
    if (index >= SYSRXE_MAX_APPS) return -1;
    return SYSRXE_APP_BASE + (int)index;
}

int sysrxe_index_from_app(int app)
{
    int idx = app - SYSRXE_APP_BASE;
    if (idx < 0 || idx >= (int)SYSRXE_MAX_APPS) return -1;
    return idx;
}

int sysrxe_is_app(int app)
{
    int idx = sysrxe_index_from_app(app);
    return idx >= 0 && (uint32_t)idx < s_count && s_apps[idx].used;
}

const sysrxe_app_t* sysrxe_get_by_app(int app)
{
    int idx = sysrxe_index_from_app(app);
    if (idx < 0 || (uint32_t)idx >= s_count) return NULL;
    return &s_apps[idx];
}

int sysrxe_format_home(int app, char* out, uint32_t out_cap)
{
    const sysrxe_app_t* a = sysrxe_get_by_app(app);
    if (!a || !out || out_cap == 0) return -1;
    snprintf(out, out_cap,
             "SYSRXE app: %s\nfile: %s\n\n%s%s%s",
             a->name, a->file, a->body,
             a->command[0] ? "\nCommand: " : "",
             a->command[0] ? a->command : "");
    return 0;
}

int sysrxe_run(int app, const char* input, char* out, uint32_t out_cap)
{
    const sysrxe_app_t* a = sysrxe_get_by_app(app);
    if (!a || !out || out_cap == 0) return -1;
    if (!input) input = "";
    if (a->command[0]) {
        char cmd[256];
        uint32_t n = 0;
        uint32_t i = 0;
        while (a->command[n] && n + 1u < sizeof(cmd)) { cmd[n] = a->command[n]; n++; }
        if (input[0] && n + 2u < sizeof(cmd)) {
            cmd[n++] = ' ';
            while (input[i] && n + 1u < sizeof(cmd)) cmd[n++] = input[i++];
        }
        cmd[n] = '\0';
        lsh_exec(cmd);
        copy_text(out, out_cap, lsh_get_output());
        return 0;
    }
    snprintf(out, out_cap, "%s\nInput: %s\n\nNo COMMAND is set, so SYSRXE only rendered the app body.", a->body, input);
    return 0;
}

int sysrxe_selftest(void)
{
    static const char sample[] =
        "SYSRXE 1\n"
        "ID test\n"
        "NAME Test App\n"
        "ICON T\n"
        "COLOR 0xFF123456\n"
        "INPUT Thing:\n"
        "BUTTON Do\n"
        "TEXT hello\n"
        "COMMAND echo sysrxe\n";
    sysrxe_app_t app;
    if (parse_sysrxe(&app, "test.sysrxe", sample, sizeof(sample) - 1) != 0) return -1;
    if (strcmp(app.name, "Test App") != 0) return -2;
    if (strcmp(app.icon, "T") != 0) return -3;
    if (app.color != 0xFF123456u) return -4;
    if (strcmp(app.button_label, "Do") != 0) return -5;
    if (strcmp(app.command, "echo sysrxe") != 0) return -6;
    return 0;
}
