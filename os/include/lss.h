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

/* Initialize LSS subsystem. Call after fs_init. */
void lss_init(void);

/* Run Shrine program from FS by name. Returns 0 on success, -1 on error.
   Output goes to syscall buffer (same as usermode). */
int lss_run(const char* name);
