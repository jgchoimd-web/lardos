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

static void lss_putc(char c, void* user)
{
    (void)user;
    syscall_append(&c, 1);
}

int lss_run(const char* name)
{
    const FsFile* f = fs_open(name);
    if (!f || f->size < 6) return -1;

    const uint8_t* d = f->data;
    uint32_t mag = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (mag != LSS_MAGIC) return -2;

    uint8_t type = d[4];
    if (type == LSS_TYPE_BOSL) {
        const uint8_t* bosl = d + 5;
        uint32_t bosl_len = f->size - 5;
        int r = bosl_vm_run_jit_io(bosl, bosl_len, lss_putc, NULL);
        return (r == 0) ? 0 : -3;
    }
    return -4; /* unsupported type */
}

void lss_init(void)
{
    /* No-op for now; reserved for future setup */
}
