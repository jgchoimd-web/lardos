#pragma once

#include <stdint.h>

/* Runs built-in GASM "hello" (load 40, add 2, print, halt). */
int gasm_demo_hello(char* out, uint32_t out_cap);

/* Inline GASM (GASM_ASM): run GASM source at runtime. */
int gasm_demo_inline(char* out, uint32_t out_cap);

/* OOP demo: object with slots, method invocation. */
int gasm_demo_oop(char* out, uint32_t out_cap);
