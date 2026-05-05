#pragma once

#include <stdint.h>

/*
 * LIL — auxiliary s-expression interpreter (int64 only). For REPL-style scripts,
 * quick tests, and host-side tooling. Not a bytecode VM; see BOSL for that.
 */

typedef void (*lil_putc_fn)(char c, void* user);

/* Evaluate a single top-level expression. Output via putc (e.g. decimal + '\n' for print).
 * Returns 0 on success, non-zero on parse/eval error. */
int lil_run(const char* src, lil_putc_fn putc, void* user);

/* Inline LIL (like BOSL_ASM): evaluate LIL source at runtime. */
#define LIL_ASM(src) lil_run(src, 0, 0)
#define LIL_ASM_IO(src, putc, user) lil_run(src, putc, user)

/* Evaluate a single LIL expression, return integer result. E.g. lil_eval_int("(+ 40 2)", &v). */
int lil_eval_int(const char* src, int64_t* out);
