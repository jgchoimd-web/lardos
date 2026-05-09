/*
 * LSS - Lard Subsystem for Shrine
 *
 * WSL-like subsystem to run Shrine programs on LardOS.
 * Shrine = TempleOS distro; LSS provides compatibility layer.
 *
 * Supported formats:
 *   - .shrine: LSS wrapper (magic "LSS\0") + payload
 *     type 0: BOSL bytecode (Shrine-compatible script)
 *     type 1: reserved (future: native Shrine binary)
 */
#pragma once

#include <stdint.h>

#define LSS_MAGIC  0x0053534Cu  /* "LSS\0" LE */

#define LSS_TYPE_BOSL  0
#define LSS_TYPE_NATIVE 1

typedef struct {
    uint32_t initialized;
    uint32_t runs;
    uint32_t failures;
    uint32_t verified;
    uint32_t unsupported;
    uint32_t last_size;
    uint8_t last_type;
    int32_t last_error;
    char last_name[32];
} lss_info_t;

/* Initialize LSS subsystem. Call after fs_init. */
void lss_init(void);

/* Run Shrine program from FS by name. Returns 0 on success, -1 on error.
   Output goes to syscall buffer (same as usermode). */
int lss_run(const char* name);

/* Inspect and validate a .shrine file without running it. */
int lss_probe(const char* name, lss_info_t* out);
void lss_info(lss_info_t* out);
const char* lss_type_name(uint8_t type);
int lss_selftest(void);
