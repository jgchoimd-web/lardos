## Languages in this tree

| Name | Role | Kernel | Host tool |
|------|------|--------|-----------|
| **BOSL** | Stack bytecode (MMIO, JIT subset, etc.) | `kernel/bosl_vm.c` | `lang/bosla` (C) |
| **OS VM** | 운영체제용 간단한 스택 VM (push/add/sub/mul/div/print/halt) | `kernel/os_vm.c` | `osvm` LSH |
| **Lafillo VM** | HTML→text 전용 (push/lafillo/print/halt) | `kernel/lafillo_vm.c` | `lafvm` LSH |
| **LIL** | S-expression **auxiliary** interpreter (int64, `let`/`if`/…) | `kernel/lil.c` | `tools/lil` (C) |
| **GASM** | Accumulator-based VM (load/add/sub/mul/div/print) | `kernel/gasm_vm.c` | `gasm` LSH / `GASM_ASM` |
| **LML** | Markup language (config, UI, documents) | `kernel/lml.c` | — |
| **Seed** | 셀프 호스팅 컴파일러 언어 (C 스타일 → BOSL) | — | `lang/seed/seedc` (C) |

See `LIL.md` for LIL syntax, `GASM.md` for GASM, `LML.md` for LML, `LSH.md` for LSH shell, `lang/seed/SEED.md` for Seed, and `bosla` / this file for BOSL.

`vm status`, `vm limits`, and `vm selftest` expose the shared VM Monitor for
BOSL, LIL, GASM, Lafillo VM, and OSVM. The monitor tracks runs, failures,
budget hits, last steps, max steps, and return codes. GASM and Lafillo VM are
now step-budgeted, and branchy BOSL falls back from JIT to the budgeted
interpreter path.

`shrine status`, `shrine verify hello.shrine`, and `shrine run hello.shrine`
expose LSS, the Lard Subsystem for Shrine. Current `.shrine` files wrap a type
0 BOSL payload after the `LSS\0` magic and one-byte type; verification checks
the wrapped BOSL magic and version before accepting the file.

---

## BOSL (lard bytecode language)

This repo contains a bytecode VM in the kernel (`kernel/bosl_vm.c`).  
`lang/bosla` (C) is the **host-side assembler** that turns a small text format into a BOSL image the kernel can run.

### BOSL source format

- One instruction per line
- Labels: `name:`
- Comments: same-line end-of-line comment after `;` or `#` (ignored to end of line). Inside `"..."` strings, `;` and `#` are literal. Trailing spaces before `;` are trimmed so e.g. `jmp label ; note` works.

### Instructions

**Literals & pool**

- `pushi <int>`: push 32-bit signed integer (inline)
- `pushi64 <int>`: push 64-bit signed integer (inline)
- `pushc "text"`: push a constant string (UTF-8)
- `pushk <int>`: push an **i32** from the constant pool (deduplicated)
- `pushk64 <int>`: push an **i64** from the constant pool

**Arithmetic & bits**

- `add | sub | mul | div | mod`: integer ops (`mod` is signed remainder; division by zero is an error). Operands may be i32 or i64 per VM rules.
- `neg`: negate top value
- `and | or | xor`: bitwise binary (pop `b`, pop `a`, push result)
- `not`: bitwise NOT of top integer
- `shl | shr | ushr`: shifts (pop `b` = count masked to 0–31 or 0–63 for i64, pop `a` = value; `shr` is signed, `ushr` is unsigned)
- `rol | ror`: rotate left / right (pop count, pop value; 32-bit or 64-bit width matching operand types)

**Stack**

- `dup | drop | swap | over`
- `2dup`: `(a b -- a b a b)` — duplicate pair
- `2drop`: `(a b -- )` — drop two items
- `rot`: `(a b c -- b c a)` — rotate third item to top
- `nip`: `(a b -- b)` — drop second-from-top
- `tuck`: `(x y -- y x y)` — copy top under second item
- `depth`: push current stack depth as i32
- `nop`: no operation
- `pick <n>`: copy stack slot at depth `n` (`0` = top); `n` must be 0–255
- `i32toi64` / `i64toi32`: convert top value (type-checked)

**Compare & print**

- `eq | ne | lt | le | gt | ge`: pop `b`, pop `a`, push `1` or `0` (signed compare for ints)
- `min | max`: pop `b`, pop `a`, push min(a,b) or max(a,b)
- `abs`: replace top with its absolute value
- `sgn`: replace top with -1, 0, or 1 by sign
- `printn`: like `print` but no newline
- `emit`: pop int; print as ASCII character (no newline)
- `inc` / `dec`: increment/decrement top by 1
- `?dup`: duplicate top only if non-zero
- `ult | ule | ugt | uge`: unsigned compare (32-bit if both i32, else 64-bit unsigned)
- `udiv | umod`: unsigned divide / remainder (`udiv` by zero is an error)
- `print`: pop value; prints int (i32 or i64, with newline) or string (as-is)
- `lafillop` / `lafillo_print`: pop string; convert HTML to plain text (Lafillo), print result. See `lib/lafillo.boslib`.

**Control flow**

- `jmp <label>`
- `jz <label>` / `jnz <label>`: pop integer; jump if zero / non-zero
- `call <label>`: push return address, jump to label
- `ret`: return to address pushed by `call`
- `halt`

**LIPC — kernel message ports (LardOS IPC)**  

Ports `0` .. `15`, FIFO depth 8, max `240` bytes per message (`lipc.h`). Interpreter-only; not in JIT.

- `lipc_send`: stack `(port buf len -- result)` with `buf` a raw address (`pushi` / pointer as int). `result` is byte count or `-1`.
- `lipc_recv`: stack `(port buf cap -- n)`. `n` is length, `0` if empty, `-1` on error or buffer too small.
- `lipc_pending`: stack `(port -- count)` queued messages.
- `lipc_send_str`: stack `(port str -- result)`; `str` from `pushc "..."` (same send limits).

User ring‑3 / LDLL: syscalls `SYS_LIPC_SEND` (24), `SYS_LIPC_RECV` (25), `SYS_LIPC_PENDING` (26); `rax` = nr, `rdi` = port, `rsi` = buf, `rdx` = len or cap.

**Kernel / OS (privileged; interpreter only, not JIT)**  
Addresses and ports may be i32 or i64 on the stack (truncated to 16 bits for I/O ports).

- `peeku8` / `pokeu8`, `peeku16` / `pokeu16`, `peeku32` / `pokeu32`, `peeku64` / `pokeu64`
- `inb` / `outb`, `inw` / `outw`, `inl` / `outl` (same stack order as before: e.g. `outb` pops value, then port)
- `cli` / `sti`
- `memfence`: compiler-level barrier (empty asm memory clobber), not a full CPU fence
- `mfence` / `lfence` / `sfence`: x86 memory-ordering instructions (interpreter only)
- `pause`: x86 `pause` (spin-wait hint)
- `memcpy`: pop `n`, pop `dst`, pop `src` (top of stack is `n`); copies up to 16MiB; overlap-safe (memmove semantics)
- `memset`: pop `n`, pop `c` (byte), pop `dst`; fill up to 16MiB bytes

**String & utility**

- `slen`: pop string; push its length as i32
- `within`: stack `( x lo hi -- f )`; push `1` if `lo <= x < hi` else `0`
- `2swap`: `(a b c d -- c d a b)` swap two pairs (Forth-style)
- `rand`: push pseudo-random i32 (0 to 32767); LCG seeded from RTC
- `time`: push current LardOS Time ticks since `00000-01-01` (i64)

Programs that use only inline ints, `pushk`, and the JIT-supported opcodes can use the **JIT**. Anything with `pushc`, i64-only paths not in the JIT whitelist, `call`/`ret`, `pick`, `rol`/`ror`, unsigned ops, `memcpy`/`memset`, MMIO, ports, fences, or `cli`/`sti` runs in the **interpreter** (the JIT falls back automatically).

### boslib – library files (like C .h)

Library files use the `.boslib` extension. Include them with:

```bosl
include "../lib/stdio.boslib"
pushi 42
call print_hex
```

- Search order: path relative to current file's directory, then each `-I dir`
- Example: `bosla main.bosl -o out.bosli -I lang`
- Built-in libs in `lang/lib/`: `stdio.boslib` (print_hex, print_space, print_newline), `math.boslib` (clamp, negate), `lafillo.boslib` (lafillo_print: HTML→text)

### Inline BOSL (like C `__asm__`)

From C code in the kernel, run BOSL source at runtime:

```c
#include "bosl_vm.h"

// Assemble and run; output goes to kernel console
BOSL_ASM("pushi 42\npushi 2\nadd\nprint\nhalt\n");

// With custom output callback
bosl_asm_eval("pushc \"hi\"\nprintn\nhalt\n", my_putc, my_user);
```

The in-kernel assembler supports a subset of opcodes (no MMIO, I/O ports, or include). Use for quick calculations or string output.

### Build a program

From the repo root:

```bash
./lang/bosla lang/examples/hello.bosl -o build/hello.bosli
```

Then embed the image in the kernel’s in-memory filesystem if your build does that.

---

## OS VM (운영체제용 간단한 VM)

LardOS용 최소 스택 VM. **Opcodes**: `push N`, `add`, `sub`, `mul`, `div`, `print`, `halt`, `jmp`, `jz`. 정수만. LSH: `osvm file.ovm`.

## Lafillo VM (HTML 전용)

`push "html"`, `lafillo`, `print`, `halt`. LSH: `lafvm file.dvm`.
