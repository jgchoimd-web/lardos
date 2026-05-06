#pragma once

#include <stdint.h>

/*
 * LDLL - LardOS Dynamic Link Library
 *
 * Format:
 *   0x00 magic[4]     = "LDLL"
 *   0x04 version u8   = 1
 *   0x05 reserved[3]
 *   0x08 code_sz u32
 *   0x0C data_sz u32
 *   0x10 code[code_sz]
 *   ... data[data_sz]
 *   ... export_count u16
 *   ... for each export: name_len u8, name[], offset u32 (into code)
 *
 * Loaded at USER_LDLL_BASE. Code must be position-independent.
 */

#define LDLL_MAGIC  0x4C4C444C  /* "LDLL" */

#define USER_LDLL_BASE  0x00610000u

/* Syscall: load LDLL from FS, return handle (1..N) or -1 */
int ldll_load(const char* name);

/* Syscall: get symbol address; returns 0 if not found */
void* ldll_sym(int handle, const char* name);

/* Syscall: close handle */
void ldll_close(int handle);
