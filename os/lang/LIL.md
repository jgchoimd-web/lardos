## LIL (Lard Interpreter Language)

**LIL** is a small **s-expression** interpreter shipped with the kernel (`kernel/lil.c`). It is meant for quick scripts, REPL-style experiments, and host-side checks—not for bytecode or JIT (see **BOSL** for that).

### Syntax

- One **top-level** expression per `lil_run` / per `tools/lil` invocation (wrap multiple steps in `(begin ...)`).
- **Integers** only (`int64`): decimal or `0x` hex. Optional leading `-`.
- **Symbols**: letters, digits, `_`, and `+ - * / % < > = ! ?`
- **Comments**: `;` or `#` to end of line
- **Lists**: `(op arg1 arg2 ...)`

### Built-ins

| Form | Meaning |
|------|---------|
| `(+ a …)` | Sum; empty `(+)` is `0` |
| `(- x)` | Negate |
| `(- a b …)` | Subtract (left-associative) |
| `(* …)` | Product; empty is `1` |
| `(/ a …)` | Integer divide (left-associative); divide by zero is an error |
| `(mod a b)` or `(% a b)` | Remainder |
| `(eq …)` `(ne …)` `(lt …)` `(le …)` `(gt …)` `(ge …)` | Compare → `0` or `1` |
| `(and a b)` `(or a b)` `(xor a b)` | Bitwise |
| `(shl a b)` `(shr a b)` | Shift (count masked to 6 bits) |
| `(neg x)` `(abs x)` `(not x)` | Negate, absolute, bitwise NOT |
| `(print x)` | Print decimal + newline; returns `x` |
| `(printn x)` | Print decimal, no newline |
| `(emit n)` | Output ASCII char (n & 0xFF) |
| `(defun name (params…) body)` | Define function; max 6 params |
| `(for var start end body)` | Loop `var` from start to end-1 |
| `(if cond then)` `(if cond then else)` | Truthy = non-zero |
| `(let name value body)` | Bind `name` in `body` |
| `(begin e1 e2 …)` | Evaluate in order; value is last |
| `(while cond body)` | While `cond` is truthy, evaluate `body`; value is last `body` result (or `0` if never entered) |
| `(cond (pred e) …)` | First clause whose `pred` is truthy yields `e`; optional `(else e)` clause is always taken if reached |
| `(min a …)` `(max a …)` | Minimum / maximum over one or more integers |
| `(rand)` | Pseudo-random integer (0–32767); LCG seeded from RTC (kernel) or `time(0)` (host) |
| `(time)` | Current Unix seconds (kernel: RTC; host: `time(0)`) |

### Host runner

```bash
./tools/lil lang/examples/hello.lil
# or
echo '(print (+ 1 2))' | ./tools/lil
```

### C API

```c
#include "lil.h"
int lil_run(const char* src, lil_putc_fn putc, void* user);
```

Errors are reported as non-zero return codes; successful evaluation returns `0`.
