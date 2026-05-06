; gdt64_load(gdtr_addr, tss_sel)
; Load GDT and TR.
BITS 64
GLOBAL gdt64_load

gdt64_load:
    lgdt [rdi]
    mov ax, si
    ltr ax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
