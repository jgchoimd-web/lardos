; boot/stage1.s - 512-byte BIOS stage1.
; Loads stage2 from LBA 1 to 0x7E00 and jumps to it.

BITS 16
ORG 0x7C00

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 4
%endif

STAGE2_LOAD equ 0x7E00

    jmp short start
    nop

    ; 1.44M floppy BPB. Raw BIOS boot does not need FAT, but El Torito
    ; floppy emulation firmware often expects these geometry fields.
    db 'LARDOS  '        ; OEM
    dw 512               ; bytes per sector
    db 1                 ; sectors per cluster
    dw 1                 ; reserved sectors
    db 2                 ; FAT count
    dw 224               ; root entries
    dw 2880              ; total sectors
    db 0xF0              ; media descriptor
    dw 9                 ; sectors per FAT
    dw 18                ; sectors per track
    dw 2                 ; heads
    dd 0                 ; hidden sectors
    dd 0                 ; large total sectors
    db 0                 ; drive number
    db 0                 ; reserved
    db 0x29              ; extended boot signature
    dd 0x4C415244        ; volume serial "LARD"
    db 'LARDOS BOOT'     ; volume label
    db 'FAT12   '        ; filesystem type

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
    mov dword [dap_lba], 1
    mov dword [dap_lba + 4], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .stage2_ok

    ; El Torito floppy emulation may expose only classic CHS reads.
    xor ax, ax
    mov es, ax
    mov bx, STAGE2_LOAD
    mov ax, 0x0200 | STAGE2_SECTORS
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

.stage2_ok:
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
disk_err_msg db 'Stage2 read error!', 0

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

times 510-($-$$) db 0
dw 0xAA55
