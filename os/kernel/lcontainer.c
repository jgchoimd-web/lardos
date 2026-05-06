#include "lcontainer.h"

#include "lardx_load.h"
#include "string.h"
#include <stddef.h>

static char s_lc_name[LCONTAINER_MAX][LCONTAINER_NAME_MAX];
static uint32_t s_lc_caps[LCONTAINER_MAX];
static uint32_t s_lc_runs[LCONTAINER_MAX];
static uint8_t s_lc_used[LCONTAINER_MAX];
static int s_active = -1;

static int copy_name(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || !src || !src[0] || cap == 0) return -1;
    while (src[i] && i + 1 < cap) {
        char c = src[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return -2;
        }
        dst[i] = c;
        i++;
    }
    if (src[i]) return -3;
    dst[i] = '\0';
    return 0;
}

static void clear_slot(int idx)
{
    s_lc_name[idx][0] = '\0';
    s_lc_caps[idx] = 0;
    s_lc_runs[idx] = 0;
    s_lc_used[idx] = 0;
}

static int find_container(const char* name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < LCONTAINER_MAX; i++) {
        if (s_lc_used[i] && strcmp(s_lc_name[i], name) == 0) return i;
    }
    return -1;
}

void lcontainer_init(void)
{
    for (int i = 0; i < LCONTAINER_MAX; i++) clear_slot(i);
    s_active = -1;
    (void)lcontainer_create("sealed", SYSCALL_CAP_BASE);
    (void)lcontainer_create("fsbox", SYSCALL_CAP_FS);
    (void)lcontainer_create("guibox", SYSCALL_CAP_FS | SYSCALL_CAP_GUI | SYSCALL_CAP_KEYS);
}

int lcontainer_create(const char* name, uint32_t caps)
{
    char clean[LCONTAINER_NAME_MAX];
    int slot = -1;
    int r = copy_name(clean, sizeof(clean), name);
    if (r != 0) return -1;
    if (find_container(clean) >= 0) return -2;
    for (int i = 0; i < LCONTAINER_MAX; i++) {
        if (!s_lc_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -3;
    for (uint32_t i = 0; i < LCONTAINER_NAME_MAX; i++) {
        s_lc_name[slot][i] = clean[i];
        if (clean[i] == '\0') break;
    }
    s_lc_caps[slot] = caps & SYSCALL_CAP_ALL;
    s_lc_runs[slot] = 0;
    s_lc_used[slot] = 1;
    return 0;
}

int lcontainer_remove(const char* name)
{
    int idx = find_container(name);
    if (idx < 0) return -1;
    clear_slot(idx);
    if (s_active == idx) s_active = -1;
    return 0;
}

int lcontainer_use(const char* name)
{
    int idx = find_container(name);
    if (idx < 0) return -1;
    s_active = idx;
    return 0;
}

void lcontainer_exit(void)
{
    s_active = -1;
}

int lcontainer_has_active(void)
{
    return s_active >= 0 && s_active < LCONTAINER_MAX && s_lc_used[s_active];
}

const char* lcontainer_active_name(void)
{
    return lcontainer_has_active() ? s_lc_name[s_active] : "";
}

uint32_t lcontainer_active_caps(void)
{
    return lcontainer_has_active() ? s_lc_caps[s_active] : SYSCALL_CAP_ALL;
}

uint32_t lcontainer_profile_caps(const char* profile)
{
    if (!profile || !profile[0] || strcmp(profile, "sealed") == 0) return SYSCALL_CAP_BASE;
    if (strcmp(profile, "fs") == 0 || strcmp(profile, "fsbox") == 0) return SYSCALL_CAP_FS;
    if (strcmp(profile, "gui") == 0 || strcmp(profile, "guibox") == 0) {
        return SYSCALL_CAP_FS | SYSCALL_CAP_GUI | SYSCALL_CAP_KEYS;
    }
    if (strcmp(profile, "dev") == 0) {
        return SYSCALL_CAP_FS | SYSCALL_CAP_LDLL | SYSCALL_CAP_LAFILLO;
    }
    if (strcmp(profile, "ipc") == 0) {
        return SYSCALL_CAP_FS | SYSCALL_CAP_LIPC;
    }
    if (strcmp(profile, "open") == 0 || strcmp(profile, "host") == 0) return SYSCALL_CAP_ALL;
    return SYSCALL_CAP_BASE;
}

uint32_t lcontainer_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < LCONTAINER_MAX; i++) {
        if (s_lc_used[i]) n++;
    }
    return n;
}

int lcontainer_get(uint32_t idx, const char** name, uint32_t* caps, uint32_t* runs, int* active)
{
    uint32_t seen = 0;
    for (int i = 0; i < LCONTAINER_MAX; i++) {
        if (!s_lc_used[i]) continue;
        if (seen == idx) {
            if (name) *name = s_lc_name[i];
            if (caps) *caps = s_lc_caps[i];
            if (runs) *runs = s_lc_runs[i];
            if (active) *active = (s_active == i) ? 1 : 0;
            return 0;
        }
        seen++;
    }
    return -1;
}

int lcontainer_run(const char* name, const char* path, int argc, const char** argv)
{
    int idx = name && name[0] ? find_container(name) : s_active;
    uint32_t old_caps;
    int r;
    if (idx < 0 || idx >= LCONTAINER_MAX || !s_lc_used[idx]) return -40;
    old_caps = syscall_get_caps();
    syscall_set_caps(s_lc_caps[idx]);
    r = lardx_run(path, argc, argv);
    syscall_set_caps(old_caps);
    if (r == 0) s_lc_runs[idx]++;
    return r;
}

const char* lcontainer_profile_name(uint32_t caps)
{
    caps &= SYSCALL_CAP_ALL;
    if (caps == SYSCALL_CAP_BASE) return "sealed";
    if (caps == SYSCALL_CAP_FS) return "fs";
    if (caps == (SYSCALL_CAP_FS | SYSCALL_CAP_GUI | SYSCALL_CAP_KEYS)) return "gui";
    if (caps == (SYSCALL_CAP_FS | SYSCALL_CAP_LDLL | SYSCALL_CAP_LAFILLO)) return "dev";
    if (caps == (SYSCALL_CAP_FS | SYSCALL_CAP_LIPC)) return "ipc";
    if (caps == SYSCALL_CAP_ALL) return "open";
    return "custom";
}

static void append_word(char* out, uint32_t* pos, uint32_t cap, const char* word)
{
    if (*pos && *pos + 1 < cap) out[(*pos)++] = ',';
    while (*word && *pos + 1 < cap) out[(*pos)++] = *word++;
    if (*pos < cap) out[*pos] = '\0';
}

void lcontainer_caps_text(uint32_t caps, char* out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    caps &= SYSCALL_CAP_ALL;
    if (caps == 0) {
        append_word(out, &pos, cap, "base");
        return;
    }
    if (caps & SYSCALL_CAP_FS) append_word(out, &pos, cap, "fs");
    if (caps & SYSCALL_CAP_LDLL) append_word(out, &pos, cap, "ldll");
    if (caps & SYSCALL_CAP_GUI) append_word(out, &pos, cap, "gui");
    if (caps & SYSCALL_CAP_KEYS) append_word(out, &pos, cap, "keys");
    if (caps & SYSCALL_CAP_LIPC) append_word(out, &pos, cap, "lipc");
    if (caps & SYSCALL_CAP_LAFILLO) append_word(out, &pos, cap, "lafillo");
}
