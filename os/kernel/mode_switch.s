; Controlled CPU mode bridge.
; The 64-bit kernel copies cpu_mode_trampoline_start..end to 0x6000.
; A probe call briefly walks long64 -> compat32 -> real16 -> compat32 -> long64.

BITS 64

%define MODE_TRAMPOLINE_PA 0x6000
%define MODE_TMP_STACK     0x7000
%define SEL_KDATA          0x10
%define SEL_LCODE64        0x18
%define SEL_PCODE32        0x40
%define IA32_EFER          0xC0000080

section .text

GLOBAL cpu_mode_enter_real_probe_asm
GLOBAL cpu_mode_enter_panicroom_texture_asm
GLOBAL cpu_mode_enter_auxkernel_real8_asm
GLOBAL cpu_mode_trampoline_start
GLOBAL cpu_mode_trampoline_end
GLOBAL cpu_mode_real8_marker

cpu_mode_enter_real_probe_asm:
    mov rax, MODE_TRAMPOLINE_PA
    call rax
    ret

cpu_mode_enter_panicroom_texture_asm:
    mov rdx, MODE_TRAMPOLINE_PA + mode_request - cpu_mode_trampoline_start
    mov dword [rdx], 1
    mov rax, MODE_TRAMPOLINE_PA
    call rax
    ret

cpu_mode_enter_auxkernel_real8_asm:
    mov rdx, MODE_TRAMPOLINE_PA + mode_request - cpu_mode_trampoline_start
    mov dword [rdx], 2
    mov rax, MODE_TRAMPOLINE_PA
    call rax
    ret

cpu_mode_trampoline_start:
    pushfq
    cli
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov dword [rel mode_result], 0
    mov [rel mode_saved_rsp], rsp
    mov rax, cr0
    mov [rel mode_saved_cr0], rax
    mov rax, cr3
    mov [rel mode_saved_cr3], rax
    mov rax, cr4
    mov [rel mode_saved_cr4], rax
    mov ecx, IA32_EFER
    rdmsr
    mov [rel mode_saved_efer_lo], eax
    mov [rel mode_saved_efer_hi], edx

    push qword SEL_PCODE32
    push qword (MODE_TRAMPOLINE_PA + mode_pm32 - cpu_mode_trampoline_start)
    retfq

BITS 32
mode_pm32:
    mov ax, SEL_KDATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax
    jmp SEL_PCODE32:(MODE_TRAMPOLINE_PA + mode_pm32_no_paging - cpu_mode_trampoline_start)

mode_pm32_no_paging:
    mov eax, cr4
    and eax, 0xFFFFFFDF
    mov cr4, eax

    mov ecx, IA32_EFER
    rdmsr
    btr eax, 8
    btr eax, 10
    wrmsr

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:(MODE_TRAMPOLINE_PA + mode_real16 - cpu_mode_trampoline_start)

BITS 16
mode_real16:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, MODE_TMP_STACK

    mov word [MODE_TRAMPOLINE_PA + mode_real_marker - cpu_mode_trampoline_start], 0x16
    cmp dword [MODE_TRAMPOLINE_PA + mode_request - cpu_mode_trampoline_start], 1
    jne .skip_panic_texture
    call mode_real16_panic_texture
    mov word [MODE_TRAMPOLINE_PA + mode_panic_marker - cpu_mode_trampoline_start], 0x5052
.skip_panic_texture:
    cmp dword [MODE_TRAMPOLINE_PA + mode_request - cpu_mode_trampoline_start], 2
    jne .skip_auxkernel_real8
    call mode_auxkernel_real8_probe
.skip_auxkernel_real8:

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp SEL_PCODE32:(MODE_TRAMPOLINE_PA + mode_pm32_from_real - cpu_mode_trampoline_start)

mode_real16_panic_texture:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov cx, 2000
.texture_fill:
    mov bx, cx
    mov ax, 0x172E
    test bx, 1
    jnz .texture_write
    mov ax, 0x182F
    test bx, 4
    jz .texture_write
    mov ax, 0x1E5C
.texture_write:
    stosw
    loop .texture_fill

    xor di, di
    mov cx, 80
    mov ax, 0x1E3D
.top_border:
    stosw
    loop .top_border

    mov di, 3840
    mov cx, 80
    mov ax, 0x1E3D
.bottom_border:
    stosw
    loop .bottom_border

    mov cx, 23
    mov di, 160
.side_border:
    mov word [es:di], 0x1E7C
    mov word [es:di + 158], 0x1E7C
    add di, 160
    loop .side_border

    mov di, ((2 * 80 + 4) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_badge0 - cpu_mode_trampoline_start
    mov ah, 0x1E
    call mode_real16_write_string
    mov di, ((3 * 80 + 4) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_badge1 - cpu_mode_trampoline_start
    mov ah, 0x1E
    call mode_real16_write_string
    mov di, ((4 * 80 + 4) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_badge2 - cpu_mode_trampoline_start
    mov ah, 0x1E
    call mode_real16_write_string
    mov di, ((5 * 80 + 4) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_badge3 - cpu_mode_trampoline_start
    mov ah, 0x1E
    call mode_real16_write_string

    mov di, ((2 * 80 + 25) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_title - cpu_mode_trampoline_start
    mov ah, 0x1F
    call mode_real16_write_string
    mov di, ((4 * 80 + 25) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_subtitle - cpu_mode_trampoline_start
    mov ah, 0x1B
    call mode_real16_write_string
    mov di, ((22 * 80 + 5) * 2)
    mov si, MODE_TRAMPOLINE_PA + texture_footer - cpu_mode_trampoline_start
    mov ah, 0x1E
    call mode_real16_write_string

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mode_real16_write_string:
    lodsb
    test al, al
    jz .done
    mov [es:di], ax
    add di, 2
    jmp mode_real16_write_string
.done:
    ret

mode_auxkernel_real8_probe:
    push ax
    push bx
    push cx

    mov byte [MODE_TRAMPOLINE_PA + cpu_mode_real8_marker - cpu_mode_trampoline_start], 0xA8
    mov byte [MODE_TRAMPOLINE_PA + cpu_mode_real8_marker + 1 - cpu_mode_trampoline_start], 0x08
    mov byte [MODE_TRAMPOLINE_PA + cpu_mode_real8_marker + 2 - cpu_mode_trampoline_start], 0x00
    mov byte [MODE_TRAMPOLINE_PA + cpu_mode_real8_marker + 3 - cpu_mode_trampoline_start], 0x00
    xor al, al
    mov bl, 0x11
    mov cl, 8
.real8_mix:
    add al, bl
    xor al, cl
    inc bl
    dec cl
    jnz .real8_mix
    mov byte [MODE_TRAMPOLINE_PA + cpu_mode_real8_marker + 2 - cpu_mode_trampoline_start], al
    mov byte [MODE_TRAMPOLINE_PA + cpu_mode_real8_marker + 3 - cpu_mode_trampoline_start], 0x01

    pop cx
    pop bx
    pop ax
    ret

BITS 32
mode_pm32_from_real:
    mov ax, SEL_KDATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, MODE_TMP_STACK

    mov eax, [MODE_TRAMPOLINE_PA + mode_saved_cr4 - cpu_mode_trampoline_start]
    mov cr4, eax

    mov ecx, IA32_EFER
    mov eax, [MODE_TRAMPOLINE_PA + mode_saved_efer_lo - cpu_mode_trampoline_start]
    mov edx, [MODE_TRAMPOLINE_PA + mode_saved_efer_hi - cpu_mode_trampoline_start]
    bts eax, 8
    btr eax, 10
    wrmsr

    mov eax, [MODE_TRAMPOLINE_PA + mode_saved_cr3 - cpu_mode_trampoline_start]
    mov cr3, eax

    mov eax, [MODE_TRAMPOLINE_PA + mode_saved_cr0 - cpu_mode_trampoline_start]
    or eax, 0x80000001
    mov cr0, eax
    jmp SEL_LCODE64:(MODE_TRAMPOLINE_PA + mode_long64_return - cpu_mode_trampoline_start)

BITS 64
mode_long64_return:
    mov ax, SEL_KDATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov dword [rel mode_result], 1
    mov dword [rel mode_request], 0
    mov rsp, [rel mode_saved_rsp]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    mov eax, [rel mode_result]
    popfq
    ret

align 8
mode_saved_rsp:      dq 0
mode_saved_cr0:      dq 0
mode_saved_cr3:      dq 0
mode_saved_cr4:      dq 0
mode_saved_efer_lo:  dd 0
mode_saved_efer_hi:  dd 0
mode_result:         dd 0
mode_real_marker:    dw 0
mode_panic_marker:   dw 0
mode_request:        dd 0
cpu_mode_real8_marker: db 0, 0, 0, 0
texture_badge0:      db '   /====\   ', 0
texture_badge1:      db '  / LPR  \  ', 0
texture_badge2:      db '  \ REAL /  ', 0
texture_badge3:      db '   \====/   ', 0
texture_title:       db 'LARDOS PANIC ROOM', 0
texture_subtitle:    db 'REAL16 DEFAULT TEXTURE', 0
texture_footer:      db 'real-mode first responder is drawing this texture before long64 recovery', 0

cpu_mode_trampoline_end:

section .note.GNU-stack noalloc noexec nowrite progbits
