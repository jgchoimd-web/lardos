#pragma once

#include <stdint.h>

/*
 * GASM (Gasing Machine Language): accumulator-based, object-oriented VM.
 * Accumulator A, self register S. OOP: new, get, set, getself, setself, call, ret, invoke.
 */

typedef void (*gasm_putc_fn)(char c, void* user);

#define GASM_ERR_OOB (-4)  /* Jump target out of bounds */

/* Run GASM bytecode. Returns 0 on success, non-zero on error.
 * -1: invalid args, -2: divide by zero, -3: unknown opcode, GASM_ERR_OOB: jump OOB */
int gasm_vm_run(const uint8_t* code, uint32_t size);

/* Run with custom output callback (0 = kernel console). */
int gasm_vm_run_io(const uint8_t* code, uint32_t size, gasm_putc_fn putc, void* user);

/* Inline GASM: assemble and run source string at runtime (like BOSL_ASM). */
int gasm_asm_eval(const char* src, gasm_putc_fn putc, void* user);

#define GASM_ASM(src) gasm_asm_eval(src, 0, 0)
#define GASM_ASM_IO(src, putc, user) gasm_asm_eval(src, putc, user)
