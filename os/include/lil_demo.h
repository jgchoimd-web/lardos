#pragma once

#include <stdint.h>

/* Runs a built-in LIL script and writes its output to `out`. */
int lil_demo_hello(char* out, uint32_t out_cap);

/* Inline LIL (LIL_ASM): evaluate LIL source string at runtime. */
int lil_demo_inline(char* out, uint32_t out_cap);
