#include "bootprof.h"

#include "fs.h"

#include <stdint.h>

typedef struct {
    char name[BOOTPROF_NAME_MAX + 1u];
    uint32_t force_post;
    uint32_t network;
    uint32_t dev_mode;
    uint32_t safe_mode;
} bootprof_state_t;

static bootprof_state_t s_bootprof;

static void scopy(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && src[i] != '\r' && src[i] != '\n' && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int streq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int apply_profile(const char* name)
{
    if (!name || !name[0] || streq(name, "normal")) {
        scopy(s_bootprof.name, sizeof(s_bootprof.name), "normal");
        s_bootprof.force_post = 0;
        s_bootprof.network = 1;
        s_bootprof.dev_mode = 0;
        s_bootprof.safe_mode = 0;
        return 0;
    }
    if (streq(name, "safe")) {
        scopy(s_bootprof.name, sizeof(s_bootprof.name), "safe");
        s_bootprof.force_post = 1;
        s_bootprof.network = 0;
        s_bootprof.dev_mode = 0;
        s_bootprof.safe_mode = 1;
        return 0;
    }
    if (streq(name, "netoff")) {
        scopy(s_bootprof.name, sizeof(s_bootprof.name), "netoff");
        s_bootprof.force_post = 0;
        s_bootprof.network = 0;
        s_bootprof.dev_mode = 0;
        s_bootprof.safe_mode = 0;
        return 0;
    }
    if (streq(name, "dev")) {
        scopy(s_bootprof.name, sizeof(s_bootprof.name), "dev");
        s_bootprof.force_post = 0;
        s_bootprof.network = 1;
        s_bootprof.dev_mode = 1;
        s_bootprof.safe_mode = 0;
        return 0;
    }
    return -1;
}

void bootprof_init(void)
{
    (void)apply_profile("normal");
}

void bootprof_load(void)
{
    const FsFile* f = fs_open("bootprof.txt");
    char name[BOOTPROF_NAME_MAX + 1u];
    bootprof_init();
    if (!f || !f->data || f->size == 0) return;
    uint32_t n = f->size < BOOTPROF_NAME_MAX ? f->size : BOOTPROF_NAME_MAX;
    for (uint32_t i = 0; i < n; i++) name[i] = (char)f->data[i];
    name[n] = '\0';
    if (apply_profile(name) != 0) (void)apply_profile("normal");
}

int bootprof_set(const char* name)
{
    char buf[BOOTPROF_NAME_MAX + 2u];
    uint32_t n = 0;
    if (apply_profile(name) != 0) return -1;
    while (s_bootprof.name[n] && n < BOOTPROF_NAME_MAX) {
        buf[n] = s_bootprof.name[n];
        n++;
    }
    buf[n++] = '\n';
    FsWritableFile* w = fs_open_writable("bootprof.txt");
    if (!w) return -2;
    if (fs_write(w, 0, (const uint8_t*)buf, n) != n) return -3;
    return 0;
}

void bootprof_info(bootprof_info_t* out)
{
    if (!out) return;
    scopy(out->name, sizeof(out->name), s_bootprof.name);
    out->force_post = s_bootprof.force_post;
    out->network = s_bootprof.network;
    out->dev_mode = s_bootprof.dev_mode;
    out->safe_mode = s_bootprof.safe_mode;
}

int bootprof_network_enabled(void)
{
    return s_bootprof.network != 0;
}

int bootprof_force_post(void)
{
    return s_bootprof.force_post != 0;
}

int bootprof_dev_mode(void)
{
    return s_bootprof.dev_mode != 0;
}

int bootprof_safe_mode(void)
{
    return s_bootprof.safe_mode != 0;
}

int bootprof_selftest(void)
{
    bootprof_state_t saved = s_bootprof;
    if (apply_profile("safe") != 0 || !bootprof_force_post() || bootprof_network_enabled()) {
        s_bootprof = saved;
        return -1;
    }
    if (apply_profile("dev") != 0 || !bootprof_dev_mode() || !bootprof_network_enabled()) {
        s_bootprof = saved;
        return -2;
    }
    if (apply_profile("netoff") != 0 || bootprof_network_enabled() || bootprof_force_post()) {
        s_bootprof = saved;
        return -3;
    }
    if (apply_profile("normal") != 0 || !bootprof_network_enabled() || bootprof_safe_mode()) {
        s_bootprof = saved;
        return -4;
    }
    s_bootprof = saved;
    return 0;
}
