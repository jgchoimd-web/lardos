; kernel/entry64.s - 64-bit long-mode kernel entry
; Loaded at 0x00100000 (identity-mapped by the bootloader).

BITS 64
GLOBAL _start
EXTERN kmain

_start:
    ; Use a stack below 8MiB, away from VGA, EBDA, bootinfo, and staging data.
    mov rsp, 0x00000000007F0000

    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
