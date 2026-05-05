; AP 트램펄린: SIPI로 0x4000에서 시작.
; 실행 시 APIC ID를 확인하여 ID==1이면 0x200000(aux)으로 점프, 아니면 HLT.
; 위치 무관 코드로 작성 후 0x4000에 복사하여 실행.

BITS 16
GLOBAL ap_trampoline_start
ap_trampoline_start:
    cli
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x3F00
    ; DS=0x400 → 0x4000 기준. lgdt가 0x4000+offset에서 GDTR 읽음
    mov ax, 0x400
    mov ds, ax

    ; Protected mode 진입 (GDTR은 0x4000+offset에 있음)
    lgdt [ap_gdtr - ap_trampoline_start]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:ap_pm_start

BITS 32
ap_pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x3F00

    ; AP 전용 페이지 테이블 @ 0x50000
    ; PML4[0]->PDPT, PDPT[0,1,3]->PD들, PD에서 0-4MB + APIC 매핑
    mov edi, 0x50000
    xor eax, eax
    mov ecx, (6 * 4096) / 4
    rep stosd

    mov dword [0x50000], 0x51003      ; PML4[0] = PDPT
    mov dword [0x51000], 0x52003      ; PDPT[0] = PD0 (0-2MB)
    mov dword [0x51004], 0x53003      ; PDPT[1] = PD1 (2-4MB)
    mov dword [0x5100C], 0x54003      ; PDPT[3] = PD_apic
    mov dword [0x52000], 0x00000083   ; PD0[0] = 0 (2MB)
    mov dword [0x53000], 0x00200083   ; PD1[0] = 2MB
    ; PD_apic[503] = 0xFEE00000 (2MB). 503*8 = 0xFB8
    mov dword [0x54000 + 0xFB8], 0xFEE00083
    mov dword [0x54000 + 0xFBC], 0x00000000

    ; PAE, LME, NX
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    bts eax, 8
    bts eax, 11
    wrmsr

    mov eax, 0x50000
    mov cr3, eax

    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    jmp 0x18:ap_lm_start

BITS 64
ap_lm_start:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; APIC ID 읽기 (0xFEE00020)
    mov rax, 0xFEE00020
    mov eax, [rax]
    shr eax, 24
    and eax, 0xFF

    cmp eax, 1
    je .run_aux
.halt_loop:
    cli
    hlt
    jmp .halt_loop

.run_aux:
    mov rsp, 0x2FF000
    mov rax, 0x200000
    jmp rax

align 8
ap_gdt:
    dq 0
    dq 0x00CF9A000000FFFF   ; 32b code
    dq 0x00CF92000000FFFF   ; 32b data
    dq 0x00AF9A000000FFFF   ; 64b code
    dq 0x00AF92000000FFFF   ; 64b data
ap_gdtr:
    dw ap_gdtr - ap_gdt - 1
    dd ap_gdt

GLOBAL ap_trampoline_end
ap_trampoline_end:
