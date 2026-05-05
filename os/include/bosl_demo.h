#pragma once

#include <stdint.h>

/* Runs a built-in BOSL "hello" program and writes its output to `out`. */
int bosl_demo_hello(char* out, uint32_t out_cap);

/* Inline BOSL (BOSL_ASM): run BOSL source string at runtime. */
int bosl_demo_inline(char* out, uint32_t out_cap);

