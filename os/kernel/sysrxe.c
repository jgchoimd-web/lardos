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

static void out_append_s(char* dst, uint32_t cap, uint32_t* pos, const char* src)
{
    if (!dst || cap == 0 || !pos || !src) return;
    while (*src && *pos + 1u < cap) dst[(*pos)++] = *src++;
    dst[*pos] = '\0';
}

static void out_append_ch(char* dst, uint32_t cap, uint32_t* pos, char ch)
{
    if (!dst || cap == 0 || !pos) return;
    if (*pos + 1u < cap) dst[(*pos)++] = ch;
    dst[*pos] = '\0';
}

static void out_append_u32(char* dst, uint32_t cap, uint32_t* pos, uint32_t v)
{
    char tmp[12];
    uint32_t n = 0;
    if (v == 0) {
        out_append_ch(dst, cap, pos, '0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) out_append_ch(dst, cap, pos, tmp[--n]);
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

static uint32_t parse_number_adv(const char** ps, uint32_t fallback)
{
    const char* s;
    uint32_t v = 0;
    int any = 0;
    if (!ps || !*ps) return fallback;
    s = skip_ws(*ps);
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
        any = 1;
    }
    *ps = s;
    return any ? v : fallback;
}

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) return 0;
        i++;
    }
    return a[i] == b[i];
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
    app->type = SYSRXE_TYPE_TEXT;
    copy_text(app->file, sizeof(app->file), file);
    copy_text(app->id, sizeof(app->id), file ? file : "sysrxe");
    copy_text(app->name, sizeof(app->name), "SYSRXE App");
    copy_text(app->icon, sizeof(app->icon), "X");
    copy_text(app->input_label, sizeof(app->input_label), "Input:");
    copy_text(app->button_label, sizeof(app->button_label), "Run");
    app->color = 0xFF57B8A6u;
    app->show_desktop = 1;
    app->show_dock = 0;
    copy_text(app->game_kind, sizeof(app->game_kind), "maze");
    app->game_start_x = 1;
    app->game_start_y = 1;
    app->game_px = 1;
    app->game_py = 1;
    app->game_goal_x = -1;
    app->game_goal_y = -1;
}

static void parse_game_row(sysrxe_app_t* app, const char* v)
{
    uint32_t y;
    uint32_t x = 0;
    if (!app || !v || app->game_rows >= SYSRXE_GAME_MAX_H) return;
    y = app->game_rows++;
    while (v[x] && x < SYSRXE_GAME_MAX_W) {
        char c = v[x];
        if (c == '@' || c == 'P' || c == 'S') {
            app->game_start_x = (int)x;
            app->game_start_y = (int)y;
            app->game_px = (int)x;
            app->game_py = (int)y;
            c = '.';
        } else if (c == 'G') {
            app->game_goal_x = (int)x;
            app->game_goal_y = (int)y;
        }
        app->game_map[y][x] = c;
        x++;
    }
    app->game_map[y][x] = '\0';
    if (app->game_w < x) app->game_w = x;
}

static void normalize_game(sysrxe_app_t* app)
{
    static const char* fallback[] = {
        "##########",
        "#@..#...#",
        "#.#...#G#",
        "#...#...#",
        "##########",
    };
    uint32_t y;
    if (!app || app->type != SYSRXE_TYPE_GAME) return;
    if (app->game_rows == 0) {
        for (y = 0; y < sizeof(fallback) / sizeof(fallback[0]); y++) {
            parse_game_row(app, fallback[y]);
        }
    }
    if (app->game_h == 0 || app->game_h > app->game_rows) app->game_h = app->game_rows;
    if (app->game_h > SYSRXE_GAME_MAX_H) app->game_h = SYSRXE_GAME_MAX_H;
    if (app->game_w == 0 || app->game_w > SYSRXE_GAME_MAX_W) app->game_w = SYSRXE_GAME_MAX_W;
    if (app->game_w < 3) app->game_w = 3;
    if (app->game_h < 3) app->game_h = 3;
    for (y = 0; y < app->game_h; y++) {
        uint32_t x = 0;
        while (app->game_map[y][x] && x < app->game_w) x++;
        while (x < app->game_w) {
            app->game_map[y][x] = (y == 0 || y + 1u == app->game_h) ? '#' : '.';
            x++;
        }
        app->game_map[y][app->game_w] = '\0';
    }
    if (app->game_start_x < 0 || app->game_start_y < 0 ||
        (uint32_t)app->game_start_x >= app->game_w || (uint32_t)app->game_start_y >= app->game_h ||
        app->game_map[app->game_start_y][app->game_start_x] == '#') {
        app->game_start_x = 1;
        app->game_start_y = 1;
    }
    if (app->game_goal_x < 0 || app->game_goal_y < 0 ||
        (uint32_t)app->game_goal_x >= app->game_w || (uint32_t)app->game_goal_y >= app->game_h) {
        app->game_goal_x = (int)app->game_w - 2;
        app->game_goal_y = (int)app->game_h - 2;
        app->game_map[app->game_goal_y][app->game_goal_x] = 'G';
    }
    app->game_px = app->game_start_x;
    app->game_py = app->game_start_y;
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
    if ((v = value_after_key(src, "TYPE")) != NULL) { app->type = streq_ci(v, "GAME") ? SYSRXE_TYPE_GAME : SYSRXE_TYPE_TEXT; return 0; }
    if ((v = value_after_key(src, "GAME")) != NULL) { app->type = SYSRXE_TYPE_GAME; copy_text(app->game_kind, sizeof(app->game_kind), v); return 0; }
    if ((v = value_after_key(src, "BOARD")) != NULL || (v = value_after_key(src, "SIZE")) != NULL) {
        const char* p = v;
        app->game_w = parse_number_adv(&p, app->game_w);
        app->game_h = parse_number_adv(&p, app->game_h);
        if (app->game_w > SYSRXE_GAME_MAX_W) app->game_w = SYSRXE_GAME_MAX_W;
        if (app->game_h > SYSRXE_GAME_MAX_H) app->game_h = SYSRXE_GAME_MAX_H;
        return 0;
    }
    if ((v = value_after_key(src, "PLAYER")) != NULL || (v = value_after_key(src, "START")) != NULL) {
        const char* p = v;
        app->game_start_x = (int)parse_number_adv(&p, (uint32_t)app->game_start_x);
        app->game_start_y = (int)parse_number_adv(&p, (uint32_t)app->game_start_y);
        app->game_px = app->game_start_x;
        app->game_py = app->game_start_y;
        return 0;
    }
    if ((v = value_after_key(src, "GOAL")) != NULL) {
        const char* p = v;
        app->game_goal_x = (int)parse_number_adv(&p, 0u);
        app->game_goal_y = (int)parse_number_adv(&p, 0u);
        return 0;
    }
    if ((v = value_after_key(src, "ROW")) != NULL || (v = value_after_key(src, "MAP")) != NULL) {
        app->type = SYSRXE_TYPE_GAME;
        parse_game_row(app, v);
        return 0;
    }
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
    normalize_game(app);
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

static sysrxe_app_t* sysrxe_get_mutable_by_app(int app)
{
    int idx = sysrxe_index_from_app(app);
    if (idx < 0 || (uint32_t)idx >= s_count) return NULL;
    return &s_apps[idx];
}

static void sysrxe_game_render(const sysrxe_app_t* a, char* out, uint32_t out_cap)
{
    uint32_t pos = 0;
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    out_append_s(out, out_cap, &pos, "RXE GAME: ");
    out_append_s(out, out_cap, &pos, a->name);
    out_append_s(out, out_cap, &pos, "\nfile: ");
    out_append_s(out, out_cap, &pos, a->file);
    out_append_s(out, out_cap, &pos, "\nMove with arrow keys, or type W/A/S/D and press ");
    out_append_s(out, out_cap, &pos, a->button_label);
    out_append_s(out, out_cap, &pos, ". Type reset to restart.\n\n");
    if (a->body[0]) {
        out_append_s(out, out_cap, &pos, a->body);
        out_append_ch(out, out_cap, &pos, '\n');
    }
    for (uint32_t y = 0; y < a->game_h; y++) {
        for (uint32_t x = 0; x < a->game_w; x++) {
            char c = a->game_map[y][x] ? a->game_map[y][x] : '.';
            if ((int)x == a->game_px && (int)y == a->game_py) c = '@';
            else if ((int)x == a->game_goal_x && (int)y == a->game_goal_y) c = 'G';
            out_append_ch(out, out_cap, &pos, c);
        }
        out_append_ch(out, out_cap, &pos, '\n');
    }
    out_append_s(out, out_cap, &pos, "\nMoves: ");
    out_append_u32(out, out_cap, &pos, a->game_moves);
    out_append_s(out, out_cap, &pos, "  Wins: ");
    out_append_u32(out, out_cap, &pos, a->game_wins);
    out_append_s(out, out_cap, &pos, a->game_won ? "  STATUS: CLEAR\n" : "  STATUS: PLAY\n");
}

static int sysrxe_game_dir(const char* input, int* dx, int* dy, int* reset)
{
    char c;
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    if (reset) *reset = 0;
    input = skip_ws(input);
    if (!input || !input[0]) return 0;
    if (streq_ci(input, "reset") || streq_ci(input, "restart")) {
        if (reset) *reset = 1;
        return 1;
    }
    if (streq_ci(input, "up")) { if (dy) *dy = -1; return 1; }
    if (streq_ci(input, "down")) { if (dy) *dy = 1; return 1; }
    if (streq_ci(input, "left")) { if (dx) *dx = -1; return 1; }
    if (streq_ci(input, "right")) { if (dx) *dx = 1; return 1; }
    c = lower_ascii(input[0]);
    if (c == 'w' || c == 'u') { if (dy) *dy = -1; return 1; }
    if (c == 's') { if (dy) *dy = 1; return 1; }
    if (c == 'a' || c == 'l') { if (dx) *dx = -1; return 1; }
    if (c == 'd' || c == 'r') { if (dx) *dx = 1; return 1; }
    return 0;
}

static int sysrxe_game_run(sysrxe_app_t* a, const char* input, char* out, uint32_t out_cap)
{
    int dx = 0;
    int dy = 0;
    int reset = 0;
    int nx;
    int ny;
    if (!a || !out || out_cap == 0) return -1;
    if (sysrxe_game_dir(input, &dx, &dy, &reset)) {
        if (reset) {
            a->game_px = a->game_start_x;
            a->game_py = a->game_start_y;
            a->game_moves = 0;
            a->game_won = 0;
        } else if (dx || dy) {
            nx = a->game_px + dx;
            ny = a->game_py + dy;
            if (nx >= 0 && ny >= 0 && (uint32_t)nx < a->game_w && (uint32_t)ny < a->game_h &&
                a->game_map[ny][nx] != '#') {
                a->game_px = nx;
                a->game_py = ny;
                a->game_moves++;
                if (nx == a->game_goal_x && ny == a->game_goal_y && !a->game_won) {
                    a->game_won = 1;
                    a->game_wins++;
                }
            }
        }
    }
    sysrxe_game_render(a, out, out_cap);
    return 0;
}

int sysrxe_format_home(int app, char* out, uint32_t out_cap)
{
    const sysrxe_app_t* a = sysrxe_get_by_app(app);
    if (!a || !out || out_cap == 0) return -1;
    if (a->type == SYSRXE_TYPE_GAME) {
        sysrxe_game_render(a, out, out_cap);
        return 0;
    }
    snprintf(out, out_cap,
             "SYSRXE app: %s\nfile: %s\n\n%s%s%s",
             a->name, a->file, a->body,
             a->command[0] ? "\nCommand: " : "",
             a->command[0] ? a->command : "");
    return 0;
}

int sysrxe_run(int app, const char* input, char* out, uint32_t out_cap)
{
    sysrxe_app_t* a = sysrxe_get_mutable_by_app(app);
    if (!a || !out || out_cap == 0) return -1;
    if (!input) input = "";
    if (a->type == SYSRXE_TYPE_GAME) return sysrxe_game_run(a, input, out, out_cap);
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

int sysrxe_is_game(int app)
{
    const sysrxe_app_t* a = sysrxe_get_by_app(app);
    return a && a->type == SYSRXE_TYPE_GAME;
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
    static const char game[] =
        "SYSRXE 1\n"
        "ID game\n"
        "NAME Game App\n"
        "TYPE GAME\n"
        "BOARD 5 3\n"
        "ROW #####\n"
        "ROW #@.G#\n"
        "ROW #####\n";
    sysrxe_app_t app;
    sysrxe_app_t game_app;
    char out[512];
    if (parse_sysrxe(&app, "test.sysrxe", sample, sizeof(sample) - 1) != 0) return -1;
    if (strcmp(app.name, "Test App") != 0) return -2;
    if (strcmp(app.icon, "T") != 0) return -3;
    if (app.color != 0xFF123456u) return -4;
    if (strcmp(app.button_label, "Do") != 0) return -5;
    if (strcmp(app.command, "echo sysrxe") != 0) return -6;
    if (parse_sysrxe(&game_app, "game.sysrxe", game, sizeof(game) - 1) != 0) return -7;
    if (game_app.type != SYSRXE_TYPE_GAME) return -8;
    if (game_app.game_w != 5u || game_app.game_h != 3u) return -9;
    if (game_app.game_px != 1 || game_app.game_py != 1) return -10;
    if (game_app.game_goal_x != 3 || game_app.game_goal_y != 1) return -11;
    if (sysrxe_game_run(&game_app, "right", out, sizeof(out)) != 0) return -12;
    if (game_app.game_px != 2 || game_app.game_py != 1 || game_app.game_moves != 1u) return -13;
    return 0;
}
