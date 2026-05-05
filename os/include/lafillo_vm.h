/*
 * lafillo_vm.h - Lafillo 전용 간단한 VM
 *
 * HTML 처리에 특화된 최소형 스택 VM.
 * Opcodes: PUSH_STR, LAFILLO (html->text), PRINT, HALT
 */
#pragma once

#include <stdint.h>

typedef void (*lafillo_vm_putc_fn)(char c, void* user);

/* Run Lafillo VM bytecode. Returns 0 on success, non-zero on error. */
int lafillo_vm_run(const uint8_t* image, uint32_t size);

/* Run with custom output callback (0 = kernel console). */
int lafillo_vm_run_io(const uint8_t* image, uint32_t size, lafillo_vm_putc_fn putc, void* user);

/* Inline source: assemble and run at runtime.
 * Syntax: push "str" | lafillo | print | halt
 */
int lafillo_vm_asm_eval(const char* src, lafillo_vm_putc_fn putc, void* user);

#define LAFILLO_VM_ASM(src) lafillo_vm_asm_eval(src, 0, 0)
#define LAFILLO_VM_ASM_IO(src, putc, user) lafillo_vm_asm_eval(src, putc, user)
