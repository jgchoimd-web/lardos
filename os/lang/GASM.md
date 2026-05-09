## GASM (Gasing Machine Language)

**GASM** is a minimal **accumulator-based, object-oriented** virtual machine: accumulator `A`, self register `S`. Contrast with BOSL (stack-based) and LIL (s-expression interpreter).

Run source from LSH with `gasm file.gasm`. `vm status`, `vm limits`, and
`vm selftest` show GASM under the shared VM Monitor. The runtime has a step
budget, so accidental infinite loops report a VM failure instead of hanging the
OS surface.

### Syntax

- One instruction per line
- Labels: `name:` (for `jmp`/`jz`/`jnz`/`call`/`invoke`)
- Comments: `;` or `#` to end of line
- Integers: decimal or `0x` hex

### Instructions

**Basic**

| Instruction | Effect |
|-------------|--------|
| `load <n>` | A = n |
| `add <n>` | A += n |
| `sub <n>` | A -= n |
| `mul <n>` | A *= n |
| `div <n>` | A /= n (divide by zero is an error) |
| `print` | Output A as decimal + newline |
| `printn` | Output A as decimal (no newline) |
| `halt` | Stop execution |
| `nop` | No operation |
| `jmp <label>` | Jump to label |
| `jz <label>` | If A == 0, jump |
| `jnz <label>` | If A != 0, jump |
| `lt <n>` `le <n>` `gt <n>` `ge <n>` `eq <n>` `ne <n>` | A = (A op n) ? 1 : 0 |

**Object-oriented**

| Instruction | Effect |
|-------------|--------|
| `new <n>` | Allocate object with n slots (1–8). A = handle |
| `get <handle> <slot>` | A = object[handle][slot] |
| `set <handle> <slot>` | object[handle][slot] = A |
| `getself <slot>` | A = object[S][slot] (S = current self) |
| `setself <slot>` | object[S][slot] = A |
| `addself <slot>` | A += object[S][slot] |
| `call <label>` | Push return address, jump to label |
| `ret` | Pop and return |
| `invoke <handle> <label>` | S = handle, call label (method invocation) |

### Example

```gasm
; 40 + 2 = 42
load 40
add 2
print
halt
```

**OOP example** (object with x,y, method that adds them):

```gasm
new 2
load 3
set 0 0
load 4
set 0 1
invoke 0 add
print
halt
add:
getself 0
addself 1
ret
```

Loop example (countdown 10..1):

```gasm
load 10
loop:
print
sub 1
jnz loop
halt
```

### OOB (Out-of-Bounds)

Jump targets are validated: `jmp`/`jz`/`jnz` must target a valid instruction boundary within the code. Invalid targets cause the VM to return `-4` (GASM_ERR_OOB).

### Inline GASM (from C)

```c
#include "gasm_vm.h"

// Run at runtime; output to console
GASM_ASM("load 42\nprint\nhalt\n");

// With custom output
gasm_asm_eval("load 40\nadd 2\nprint\nhalt\n", my_putc, my_user);
```
