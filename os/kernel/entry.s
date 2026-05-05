; kernel/entry.s - 32-bit protected-mode kernel entry
; Loaded at 0x00100000 by the bootloader.

BITS 32
GLOBAL _start
EXTERN kmain
EXTERN gdt_init
EXTERN idt_init
EXTERN isr_install
EXTERN irq_install
EXTERN timer_init
EXTERN keyboard_init

_start:
    mov esp, 0x80000

    call gdt_init
    call idt_init
    call isr_install
    call irq_install
    push dword 100
    call timer_init
    add esp, 4
    call keyboard_init

    sti
    call kmain

.halt:
    cli
    hlt
    jmp .halt

