/*
 * LSS - Lard Subsystem for Shrine
 * Runs Shrine programs on LardOS (WSL-like compatibility layer).
 */
#include "lss.h"
#include "bosl_vm.h"
#include "fs.h"
#include "syscall.h"
#include <stddef.h>
#include <stdint.h>

static lss_info_t s_lss;

static void lss_putc(char c, void* user)
{
    (void)user;
    syscall_append(&c, 1);
}

static uint32_t lss_rd32(const uint8_t* d)
{
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

static void lss_copy_name(const char* name)
{
    uint32_t i = 0;
    while (name && name[i] && i + 1u < sizeof(s_lss.last_name)) {
        s_lss.last_name[i] = name[i];
        i++;
    }
    s_lss.last_name[i] = '\0';
}

const char* lss_type_name(uint8_t type)
{
    if (type == LSS_TYPE_BOSL) return "bosl";
    if (type == LSS_TYPE_NATIVE) return "native-reserved";
    return "unknown";
}

static int lss_validate_file(const FsFile* f, const char* name, lss_info_t* out)
{
    if (!f || !f->data || f->size < 5u) {
        s_lss.last_error = -1;
        lss_copy_name(name);
        if (out) *out = s_lss;
        return -1;
    }
    const uint8_t* d = f->data;
    uint32_t mag = lss_rd32(d);
    if (mag != LSS_MAGIC) {
        s_lss.last_error = -2;
        lss_copy_name(name);
        s_lss.last_size = f->size;
        if (out) *out = s_lss;
        return -2;
    }

    uint8_t type = d[4];
    s_lss.last_type = type;
    s_lss.last_size = f->size;
    lss_copy_name(name);
    if (type == LSS_TYPE_NATIVE) {
        s_lss.unsupported++;
        s_lss.last_error = -4;
        if (out) *out = s_lss;
        return -4;
    }
    if (type != LSS_TYPE_BOSL) {
        s_lss.unsupported++;
        s_lss.last_error = -5;
        if (out) *out = s_lss;
        return -5;
    }
    if (f->size <= 5u) {
        s_lss.last_error = -6;
        if (out) *out = s_lss;
        return -6;
    }
    s_lss.verified++;
    s_lss.last_error = 0;
    if (out) *out = s_lss;
    return 0;
}

int lss_probe(const char* name, lss_info_t* out)
{
    return lss_validate_file(fs_open(name), name, out);
}

int lss_run(const char* name)
{
    const FsFile* f = fs_open(name);
    int vr = lss_validate_file(f, name, NULL);
    s_lss.runs++;
    if (vr != 0) {
        s_lss.failures++;
        return vr;
    }

    const uint8_t* bosl = f->data + 5;
    uint32_t bosl_len = f->size - 5;
    int r = bosl_vm_run_jit_io(bosl, bosl_len, lss_putc, NULL);
    if (r != 0) {
        s_lss.failures++;
        s_lss.last_error = -3;
        return -3;
    }
    s_lss.last_error = 0;
    return 0;
}

void lss_init(void)
{
    s_lss.initialized = 1;
    if (!s_lss.last_name[0]) {
        lss_copy_name("none");
        s_lss.last_type = 0xFFu;
    }
}

void lss_info(lss_info_t* out)
{
    if (out) {
        *out = s_lss;
    }
}

int lss_selftest(void)
{
    lss_info_t info;
    if (!s_lss.initialized) {
        lss_init();
    }
    if (lss_probe("hello.shrine", &info) != 0) {
        return -1;
    }
    if (info.last_type != LSS_TYPE_BOSL || info.last_size <= 5u) {
        return -2;
    }
    return 0;
}
