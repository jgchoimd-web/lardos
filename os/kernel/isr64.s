; 64-bit exception stubs (vectors 0..31)
BITS 64

GLOBAL isr64_stub_0
GLOBAL isr64_stub_1
GLOBAL isr64_stub_2
GLOBAL isr64_stub_3
GLOBAL isr64_stub_4
GLOBAL isr64_stub_5
GLOBAL isr64_stub_6
GLOBAL isr64_stub_7
GLOBAL isr64_stub_8
GLOBAL isr64_stub_9
GLOBAL isr64_stub_10
GLOBAL isr64_stub_11
GLOBAL isr64_stub_12
GLOBAL isr64_stub_13
GLOBAL isr64_stub_14
GLOBAL isr64_stub_15
GLOBAL isr64_stub_16
GLOBAL isr64_stub_17
GLOBAL isr64_stub_18
GLOBAL isr64_stub_19
GLOBAL isr64_stub_20
GLOBAL isr64_stub_21
GLOBAL isr64_stub_22
GLOBAL isr64_stub_23
GLOBAL isr64_stub_24
GLOBAL isr64_stub_25
GLOBAL isr64_stub_26
GLOBAL isr64_stub_27
GLOBAL isr64_stub_28
GLOBAL isr64_stub_29
GLOBAL isr64_stub_30
GLOBAL isr64_stub_31

EXTERN isr64_dispatch

%macro ISR_NOERR 1
isr64_stub_%1:
    push qword 0            ; err
    push qword %1           ; vec
    jmp isr64_common
%endmacro

%macro ISR_ERR 1
isr64_stub_%1:
    ; CPU already pushed error code. Stack top = err, then RIP/CS/RFLAGS...
    push qword %1           ; vec
    jmp isr64_common_err
%endmacro

; Common path when we pushed (err, vec) ourselves:
; [rsp+0] vec
; [rsp+8] err
isr64_common:
    jmp isr64_save

; Common path when CPU pushed err and we pushed vec:
; [rsp+0] vec
; [rsp+8] err (from CPU)
isr64_common_err:
    jmp isr64_save

isr64_save:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr64_dispatch
    test eax, eax
    jnz .iret_to_user

.halt:
    cli
    hlt
    jmp .halt

.iret_to_user:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

; Vectors with error code: 8,10,11,12,13,14,17,21,29,30
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; Vector 128 (INT 0x80) - syscall from user
GLOBAL isr64_stub_128
isr64_stub_128:
    push qword 0
    push qword 128
    jmp isr64_save

section .note.GNU-stack noalloc noexec nowrite progbits
