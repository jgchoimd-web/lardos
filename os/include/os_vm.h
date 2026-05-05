/*
 * os_vm.h - LardOS용 간단한 스택 VM
 *
 * 최소 opcode: PUSH, ADD, SUB, MUL, DIV, PRINT, HALT, JMP, JZ
 * 정수만 지원. BOSL/GASM보다 단순한 범용 스크립트용.
 */
#pragma once

#include <stdint.h>

typedef void (*os_vm_putc_fn)(char c, void* user);

/* Run OS VM bytecode. Returns 0 on success. */
int os_vm_run(const uint8_t* image, uint32_t size);

/* Run with custom output. */
int os_vm_run_io(const uint8_t* image, uint32_t size, os_vm_putc_fn putc, void* user);

/* Inline source: push N, add, sub, mul, div, print, halt, jmp L, jz L */
int os_vm_asm_eval(const char* src, os_vm_putc_fn putc, void* user);

#define OS_VM_ASM(src) os_vm_asm_eval(src, 0, 0)
#define OS_VM_ASM_IO(src, putc, user) os_vm_asm_eval(src, putc, user)
