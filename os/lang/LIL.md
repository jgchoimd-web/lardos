## LIL (Lard Interpreter Language)

**LIL** is a small s-expression interpreter shipped with the kernel
(`kernel/lil.c`) and mirrored by the host runner (`tools/lil.c`). It is meant
for quick scripts, boot-time demos, REPL-style experiments, and self-checks.
It is not a bytecode VM or JIT; use BOSL for that layer.

### Syntax

- One top-level expression per `lil_run` / per `tools/lil` invocation. Wrap
  multiple steps in `(begin ...)`.
- Integers only (`int64`): decimal or `0x` hex. Optional leading `-`.
- Symbols: letters, digits, `_`, and `+ - * / % < > = ! ?`.
- Comments: `;` or `#` to end of line.
- Lists: `(op arg1 arg2 ...)`.

### Built-ins

| Form | Meaning |
|------|---------|
| `(+ a ...)` | Sum; empty `(+)` is `0` |
| `(- x)` | Negate |
| `(- a b ...)` | Subtract left-to-right |
| `(* a ...)` | Product; empty `(*)` is `1` |
| `(/ a b ...)` | Integer divide left-to-right; divide by zero is an error |
| `(mod a b)` or `(% a b)` | Remainder |
| `(eq a b)` `(ne a b)` `(lt a b)` `(le a b)` `(gt a b)` `(ge a b)` | Compare; returns `0` or `1` |
| `(and a b)` `(or a b)` `(xor a b)` | Bitwise |
| `(shl a b)` `(shr a b)` | Shift, with count masked to 6 bits |
| `(neg x)` `(abs x)` `(not x)` | Negate, absolute value, bitwise NOT |
| `(min a ...)` `(max a ...)` | Minimum / maximum over one or more integers |
| `(clamp x lo hi)` | Clamp `x` into the inclusive range |
| `(between x lo hi)` | Inclusive range check: `lo <= x <= hi` |
| `(within x lo hi)` | Half-open range check: `lo <= x < hi` |
| `(pow base exp)` | Integer exponent; `exp` must be non-negative |
| `(gcd a ...)` `(lcm a ...)` | Greatest common divisor / least common multiple |
| `(print x)` | Print decimal + newline; returns `x` |
| `(printn x)` | Print decimal without newline |
| `(emit n)` | Output ASCII char (`n & 0xFF`) |
| `(assert cond ...)` | Error if any condition is `0`; useful for scripts and selftests |
| `(defun name (params...) body)` | Define a function; max 6 params |
| `(if cond then)` `(if cond then else)` | Truthy = non-zero |
| `(when cond body...)` | Run body expressions only when `cond` is truthy |
| `(unless cond body...)` | Run body expressions only when `cond` is zero |
| `(let name value body...)` | Bind `name` across one or more body expressions |
| `(begin e1 e2 ...)` | Evaluate in order; value is last expression |
| `(while cond body)` | While `cond` is truthy, evaluate body |
| `(for var start end body)` | Loop `var` from `start` to `end - 1` |
| `(for var start end step body)` | Stepped loop; negative steps count down |
| `(repeat count body...)` | Run body `count` times with `it` bound to `0..count-1` |
| `(cond (pred e...) ... (else e...))` | First truthy clause yields its last body value |
| `(rand)` | Pseudo-random integer `0..32767` |
| `(time)` | Current Unix seconds |

### Host Runner

```bash
./build/tools/lil lang/examples/features.lil
# or
echo '(print (+ 1 2))' | ./build/tools/lil
```

### LSH Runner

Inside LardOS:

```text
lil features.lil
```

The same interpreter also backs `LIL_ASM` and `lil_eval_int` from C.

### C API

```c
#include "lil.h"

int lil_run(const char* src, lil_putc_fn putc, void* user);
int lil_eval_int(const char* src, int64_t* out);
```

Errors are reported as non-zero return codes; successful evaluation returns `0`.
