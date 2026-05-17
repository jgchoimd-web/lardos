; boot/iso_mbr.s - hybrid ISO MBR bootstrap.
; Lets a LardOS ISO written raw to USB boot like a disk image.

BITS 16
ORG 0x7C00

%ifndef ISO_STAGE2_LBA
%define ISO_STAGE2_LBA 93
%endif

%ifndef ISO_KERNEL_LBA
%define ISO_KERNEL_LBA 97
%endif

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 4
%endif

STAGE2_LOAD equ 0x0600

    jmp short start
    nop

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    xor ah, ah
    int 0x13

    mov word [dap_count], STAGE2_SECTORS
    mov word [dap_off], STAGE2_LOAD
    mov word [dap_seg], 0
    mov dword [dap_lba], ISO_STAGE2_LBA
    mov dword [dap_lba + 4], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    mov eax, 0x4452414C          ; "LARD" handoff magic
    mov ebx, ISO_KERNEL_LBA      ; absolute kernel LBA inside the ISO file
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_LOAD

disk_error:
    mov si, disk_err_msg
.print:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x04
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

boot_drive db 0
disk_err_msg db 'LardOS hybrid ISO boot error!', 0

dap:
    db 0x10
    db 0
dap_count:
    dw 0
dap_off:
    dw 0
dap_seg:
    dw 0
dap_lba:
    dq 0

times 446-($-$$) db 0
times 64 db 0
dw 0xAA55
