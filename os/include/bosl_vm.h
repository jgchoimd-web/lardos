#pragma once

#include <stdint.h>

/*
 * BOSL (lard bytecode language): a tiny, OS-friendly bytecode VM.
 *
 * Stack values: 32-bit int, 64-bit int, or string (constant pool). Arithmetic,
 * compares, and branches accept int32/int64 mixes where defined in bosl_vm.c.
 * Constant pool: UTF-8 strings, i32, i64. Opcodes include call/ret, pick,
 * rot/nip/tuck/depth, i32↔i64, unsigned compare/div, rol/ror, MMIO peek/poke,
 * memcpy/memset, x86 in/out, cli/sti, memfence plus mfence/lfence/sfence/pause,
 * LIPC (lipc_send / lipc_recv / lipc_pending; kernel message ports, see lipc.h).
 * Privileged ops use the interpreter; the x86_64 JIT covers a small i32-only subset.
 */

typedef void (*bosl_putc_fn)(char c, void* user);

/* Execute a BOSL bytecode image loaded in memory.
 * Returns 0 on success, non-zero on error.
 */
int bosl_vm_run(const uint8_t* image, uint32_t size);

/* Execute with a custom output sink (used by GUI builds).
 * The VM uses this callback for OP_PRINT output (strings and ints).
 * Errors still go to the kernel console via kprintf.
 */
int bosl_vm_run_io(const uint8_t* image, uint32_t size, bosl_putc_fn putc, void* user);

/* JIT compiler (x86_64): translates BOSL into native code and executes it.
 * Falls back to the interpreter if the program uses unsupported features.
 */
int bosl_vm_run_jit(const uint8_t* image, uint32_t size);
int bosl_vm_run_jit_io(const uint8_t* image, uint32_t size, bosl_putc_fn putc, void* user);

/* Inline BOSL (like C __asm__): assemble and run BOSL source string at runtime.
 * src: BOSL source (must include halt). putc/user: optional output, 0 = console.
 * Returns 0 on success.
 */
int bosl_asm_eval(const char* src, bosl_putc_fn putc, void* user);

#define BOSL_ASM(src) bosl_asm_eval(src, 0, 0)
#define BOSL_ASM_IO(src, putc, user) bosl_asm_eval(src, putc, user)

