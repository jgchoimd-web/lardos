; kernel/entry64.s - 64-bit long-mode kernel entry
; Loaded at 0x00100000 (identity-mapped by the bootloader).

BITS 64
GLOBAL _start
EXTERN kmain

_start:
    ; Use a simple stack in low memory (identity-mapped).
    mov rsp, 0x00000000000A0000

    call kmain

.halt:
    cli
    hlt
    jmp .halt

