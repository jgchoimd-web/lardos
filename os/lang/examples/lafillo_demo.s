; lafillo_demo.s - LARDX demo: read lafillo_saved.txt, SYS_LAFILLO_HTML, write result.
; Syscalls: SYS_OPEN=12, SYS_READ=13, SYS_LAFILLO_HTML=19, SYS_WRITE=1, SYS_CLOSE=14, SYS_EXIT=2
; Build: make -C os lafillo_demo.bosx
; Run: run lafillo_demo.bosx (requires lafillo_demo.bosx on FS)

BITS 64
SECTION .rodata
path:   db "lafillo_saved.txt", 0
msg_ok: db "Lafillo demo: HTML extracted.", 10
msg_no: db "Lafillo demo: file not found or empty.", 10
msg_err:db "Lafillo demo: conversion failed.", 10

SECTION .bss
in_buf:  resb 4096
out_buf: resb 4096

SECTION .text
GLOBAL _start
_start:
    ; SYS_OPEN("lafillo_saved.txt") - LardOS uses int 0x80
    mov eax, 12
    lea rdi, [rel path]
    int 0x80
    cmp eax, 0
    jl L_fail
    mov ebx, eax              ; fd in ebx

    ; SYS_READ(fd, in_buf, 4095)
    mov eax, 13
    mov edi, ebx
    lea rsi, [rel in_buf]
    mov edx, 4095
    int 0x80
    cmp eax, 0
    jle L_fail_close
    mov r12d, eax              ; in_len in r12d

    ; SYS_CLOSE(fd)
    mov eax, 14
    mov edi, ebx
    int 0x80

    ; SYS_LAFILLO_HTML(in_buf, in_len, out_buf, 4096) - LardOS uses int 0x80
    mov eax, 19
    lea rdi, [rel in_buf]
    mov esi, r12d
    lea rdx, [rel out_buf]
    mov r10d, 4096
    int 0x80
    cmp eax, 0
    jl L_conv_fail
    mov r13d, eax              ; out_len in r13d

    ; SYS_WRITE(1, out_buf, out_len)
    mov eax, 1
    mov edi, 1
    lea rsi, [rel out_buf]
    mov edx, r13d
    int 0x80

    ; SYS_WRITE(1, msg_ok, 30)
    mov eax, 1
    mov edi, 1
    lea rsi, [rel msg_ok]
    mov edx, 30
    int 0x80

    mov eax, 2
    xor edi, edi
    int 0x80

L_conv_fail:
    mov eax, 1
    mov edi, 2
    lea rsi, [rel msg_err]
    mov edx, 37
    int 0x80
    mov eax, 2
    mov edi, 1
    int 0x80

L_fail_close:
    mov eax, 14
    mov edi, ebx
    int 0x80
    jmp L_fail

L_fail:
    mov eax, 1
    mov edi, 2
    lea rsi, [rel msg_no]
    mov edx, 40
    int 0x80
    mov eax, 2
    mov edi, 1
    int 0x80
