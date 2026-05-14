; boot/boot.s - stage2 BIOS loader.
; Stage1 loads this at 0x7E00, then this loads the LARDX kernel.

BITS 16
ORG 0x7E00

%ifndef KERNEL_LBA
%define KERNEL_LBA 5
%endif
%ifndef KERNEL_LOAD_SEG
%define KERNEL_LOAD_SEG 0x0900
%endif
%define KERNEL_LOAD_PADDR (KERNEL_LOAD_SEG << 4)
%define BOOT_IMAGE_COPY_PADDR 0x01000000

start:
    ; Optional handoff for raw-written hybrid ISO boots.
    ; EAX='LARD' and EBX=absolute kernel LBA lets an ISO MBR reuse this stage2.
    cmp eax, 0x4452414C
    jne .no_lba_override
    mov [cs:kernel_lba_base], ebx
    mov byte [cs:iso_boot_mode], 1
    jmp .handoff_done
.no_lba_override:
    cmp eax, 0x21534843
    jne .handoff_done
    mov byte [cs:iso_boot_mode], 2
.handoff_done:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    sti

    ; Print a short message using BIOS teletype
    mov si, boot_msg
.print_char:
    lodsb
    or al, al
    jz .done_print
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .print_char
.done_print:

    ; Set up VBE before reading the kernel. Some BIOSes use low memory as
    ; scratch during VBE calls, so calling it after the low staging read can
    ; corrupt the LARDX header or entry point.
    call vbe_try_enable

    ; Load LARDX executable (kernel) below VGA/EBDA into KERNEL_LOAD_SEG:0000.
    ; v1.68.0a builds the kernel with -Os, keeping the complete native feature
    ; set small enough for this safer BIOS-loading path.
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx

    ; Read 1 sector at KERNEL_LBA into ES:BX.
    mov eax, [kernel_lba_base]
    mov dword [dap_lba], eax
    mov word  [dap_count], 1
    mov word  [dap_off], bx
    mov word  [dap_seg], es
    call disk_read_lba

    ; Validate magic "LARD" or "BOSX" (backward compat)
    mov si, 0x0000
    mov ax, [es:si]
    cmp ax, 0x414C           ; 'LA' = LARD
    jne .check_bosx
    mov ax, [es:si+2]
    cmp ax, 0x4452           ; 'RD'
    jne disk_error
    jmp .magic_ok
.check_bosx:
    cmp ax, 0x4F42           ; 'BO' = BOSX
    jne disk_error
    mov ax, [es:si+2]
    cmp ax, 0x5853           ; 'SX'
    jne disk_error
.magic_ok:

    ; file_size: BOSX at 0x10, LARDX v2 at 0x12
    cmp word [es:0x04], 2
    jb .fs_bosx
    mov ax, [es:0x12]
    mov dx, [es:0x14]
    jmp .fs_done
.fs_bosx:
    mov ax, [es:0x10]
    mov dx, [es:0x12]
.fs_done:
    mov [kernel_file_size], ax
    mov [kernel_file_size + 2], dx
    ; sectors = (file_size+511)/512
    add ax, 511
    adc dx, 0
    mov cx, 9
.shrink:
    shr dx, 1
    rcr ax, 1
    loop .shrink
    ; AX now = total sectors to read starting at LBA=1
    cmp ax, 1
    jb disk_error
    mov [total_sectors], ax

    ; Read remaining sectors starting at KERNEL_LBA+1 into buffer+512.
    mov si, [total_sectors]
    dec si                      ; remaining count
    jz .done_load

    mov eax, [kernel_lba_base]
    inc eax
    mov dword [dap_lba], eax
    mov bx, 512                 ; offset into buffer
.read_loop:
    cmp si, 1
    jbe .chunk_ok
    mov cx, 1
    jmp .have_chunk
.chunk_ok:
    mov cx, si
.have_chunk:
    ; Keep every BIOS read inside one 64KiB ES:BX window.
    mov ax, bx
    shr ax, 9
    mov dx, 128
    sub dx, ax
    cmp cx, dx
    jbe .chunk_fits
    mov cx, dx
.chunk_fits:
    mov word [dap_count], cx
    mov word [dap_off], bx
    mov word [dap_seg], es
    call disk_read_lba
    mov al, '.'
    call boot_putc
    ; ES:BX += cx*512
    mov dx, cx
    shl dx, 9
    add bx, dx
    jnc .same_read_segment
    mov ax, es
    add ax, 0x1000
    mov es, ax
.same_read_segment:
    ; dap_lba += cx (low 32-bits)
    mov ax, cx
    add word [dap_lba], ax
    adc word [dap_lba+2], 0
    sub si, cx
    jnz .read_loop
.done_load:

    call enable_a20

    ; Set up a simple GDT in this sector
    cli
    lgdt [gdt_descriptor]

    ; Enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEL:protected_start

; 32-bit code

BITS 32

protected_start:
    cld
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000

    ; Preserve a full copy for the in-OS HDD/SSD installer before page tables
    ; and stacks reuse the low staging buffer.
    mov esi, KERNEL_LOAD_PADDR
    mov edi, BOOT_IMAGE_COPY_PADDR
    mov ecx, [kernel_file_size]
    rep movsb

    ; Parse LARDX/BOSX image in the low staging buffer and load segments.
    mov esi, KERNEL_LOAD_PADDR

    ; phnum u16 at +0x08, phoff u32 at +0x0E, entry u32 at +0x0A (LARDX v2)
    ; BOSX: phnum +0x06, phoff +0x0C, entry +0x08. LARDX v2: phnum +0x08, phoff +0x0E, entry +0x0A
    ; For compat: BOSX has phnum at +0x06, LARD at +0x08. Check version at +4.
    mov ax, [esi+0x04]       ; version u16
    cmp ax, 2
    jb .bosx_layout
    ; LARDX v2 layout
    movzx ebp, word [esi+0x08]
    mov eax, [esi+0x0E]
    jmp .have_phoff
.bosx_layout:
    ; BOSX layout
    movzx ebp, word [esi+0x06]
    mov eax, [esi+0x0C]
.have_phoff:
    add eax, esi                ; phoff + base = phdr table
    mov ebx, eax                ; phdr table ptr (ebp=phnum already set)
    ; phdr size: BOSX=16, LARDX v2=20
    mov eax, [esi+0x04]
    cmp ax, 2
    jb .phdr_16
    mov word [phdr_size], 20
    jmp .ph_loop
.phdr_16:
    mov word [phdr_size], 16

.ph_loop:
    test ebp, ebp
    jz .jump_entry

    ; phdr: paddr, file_off, file_sz, mem_sz
    mov edi, [ebx+0x00]         ; destination physical address
    mov eax, [ebx+0x04]         ; file_off
    add eax, esi
    mov edx, [ebx+0x08]         ; file_sz
    mov ecx, edx
    mov esi, eax
    rep movsb

    ; zero BSS tail (mem_sz - file_sz), if any
    mov ecx, [ebx+0x0C]         ; mem_sz
    sub ecx, edx
    jbe .next_ph
    xor eax, eax
    rep stosb

.next_ph:
    movzx eax, word [phdr_size]
    add ebx, eax
    dec ebp
    mov esi, KERNEL_LOAD_PADDR
    jmp .ph_loop

.jump_entry:
    ; entry: BOSX at +0x08, LARDX v2 at +0x0A
    mov ax, [esi+0x04]
    cmp ax, 2
    jb .entry_bosx
    mov eax, [esi+0x0A]
    jmp .entry_done
.entry_bosx:
    mov eax, [esi+0x08]
.entry_done:
    mov [kernel_entry], eax

    ; -----------------------------
    ; Enter long mode (x86_64)
    ; Identity-map first 128 MiB using 2 MiB pages.
    ; -----------------------------
    cli

    ; Build minimal 4-level paging structures at 0x70000.
    call setup_identity_paging

    ; Enable PAE (CR4.PAE=1).
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Enable Long Mode (IA32_EFER.LME=1).
    mov ecx, 0xC0000080          ; IA32_EFER
    rdmsr
    bts eax, 8                   ; LME
    bts eax, 11                  ; NXE (enables NX bit support)
    wrmsr

    ; Load CR3 with PML4 physical address.
    mov eax, 0x00070000
    mov cr3, eax

    ; Enable paging (CR0.PG=1). PE is already enabled.
    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    ; Far jump to 64-bit code segment to complete the transition.
    jmp LONG_CODE_SEL:long_mode_start

; -----------------------------
; Paging helpers (32-bit)
; -----------------------------

setup_identity_paging:
    ; PML4  = 0x70000
    ; PDPT  = 0x71000
    ; PD    = 0x72000
    ; Identity map 0..128MiB using PD[0..63] 2MiB pages.
    pushad

    ; Zero 3 pages (PML4/PDPT/PD)
    mov edi, 0x00070000
    xor eax, eax
    mov ecx, (3 * 4096) / 4
    rep stosd

    ; PML4[0] = PDPT | P|RW
    mov dword [0x00070000], 0x00071003
    mov dword [0x00070000 + 4], 0x00000000

    ; PDPT[0] = PD | P|RW
    mov dword [0x00071000], 0x00072003
    mov dword [0x00071000 + 4], 0x00000000

    ; PD[0..63] = 0..128MiB | P|RW|PS (2MiB pages)
    mov edi, 0x00072000
    mov eax, 0x00000083
    mov ecx, 64
.map_pd:
    mov dword [edi], eax
    mov dword [edi + 4], 0x00000000
    add eax, 0x00200000
    add edi, 8
    loop .map_pd

    popad
    ret

; -----------------------------
; 64-bit stub
; -----------------------------

BITS 64
long_mode_start:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, 0x000000000008F000

    ; Enable supervisor write-protect (CR0.WP=1)
    mov rax, cr0
    bts rax, 16
    btr rax, 2                   ; CR0.EM=0
    bts rax, 1                   ; CR0.MP=1
    mov cr0, rax

    ; Let the compiler-generated kernel code use SSE instructions.
    mov rax, cr4
    bts rax, 9                   ; CR4.OSFXSR=1
    bts rax, 10                  ; CR4.OSXMMEXCPT=1
    mov cr4, rax

    ; Jump to kernel entry (physical, identity-mapped).
    mov eax, dword [kernel_entry]
    jmp rax

; -----------------------------
; Data
; -----------------------------

BITS 16

boot_msg db 'Booting lardos...', 0x0D, 0x0A, 0
disk_err_msg db 'Disk read error!', 0

boot_drive db 0
iso_boot_mode db 0
total_sectors dw 0
kernel_file_size dd 0
chs_lba dd 0
chs_count dw 0
chs_cx dw 0
chs_dx dw 0
chs_retry db 0

; INT 13h Extensions Disk Address Packet (DAP)
dap:
    db 0x10                      ; size
    db 0x00                      ; reserved
dap_count:
    dw 0x0000                    ; sectors to read (1..127)
dap_off:
    dw 0x0000                    ; destination offset
dap_seg:
    dw 0x0000                    ; destination segment
dap_lba:
    dq 0x0000000000000000        ; LBA

; Read using INT 13h extensions (AH=42h) with DAP.
; Inputs: [dap_*] filled, DL = drive in [boot_drive]
disk_read_lba:
    pusha
    push ds
    xor ax, ax
    mov ds, ax
    cmp byte [iso_boot_mode], 0
    jne .edd_only
    ; VirtualBox and some BIOSes expose El Torito floppy-emulation media as
    ; either a floppy-class drive or a CD-style 0xE0 drive. In both cases
    ; classic CHS is more reliable than successful-but-wrong EDD reads.
    cmp byte [boot_drive], 0x80
    jb .chs_first
    cmp byte [boot_drive], 0xE0
    jae .chs_first
.edd_first:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    sti
    int 0x13
    jnc .ok
    call disk_read_chs
    jc disk_error
    jmp .ok
.chs_first:
    call disk_read_chs
    jnc .ok
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    sti
    int 0x13
    jc disk_error
    jmp .ok
.edd_only:
    cmp byte [iso_boot_mode], 2
    je .chs_only
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    sti
    int 0x13
    jc disk_error
    jmp .ok
.chs_only:
    call disk_read_chs
    jc disk_error
    jmp .ok
.ok:
    pop ds
    popa
    ret

; Classic floppy CHS fallback for El Torito emulation BIOSes without EDD.
disk_read_chs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov si, [dap_count]
    mov bx, [dap_off]
    mov ax, [dap_seg]
    mov es, ax
    mov eax, [dap_lba]
    mov [chs_lba], eax
.loop:
    test si, si
    jz .done

    mov eax, [chs_lba]
    xor edx, edx
    mov ecx, 36
    div ecx                 ; EAX=cylinder, EDX=head*18+sector_index
    mov di, ax
    mov ax, dx
    xor dx, dx
    mov cx, 18
    div cx                  ; AX=head, DX=sector_index
    mov dh, al
    mov ax, di
    mov ch, al
    and ah, 0x03
    shl ah, 6
    mov cl, dl
    inc cl
    or cl, ah
    mov [chs_cx], cx
    mov [chs_dx], dx

    ; Read as many sectors as possible without crossing a track or 64KiB
    ; DMA window. This keeps VirtualBox El Torito CHS boot from crawling.
    xor ax, ax
    mov al, dl
    mov bp, 18
    sub bp, ax
    cmp si, bp
    jae .count_not_left
    mov bp, si
.count_not_left:
    mov ax, bx
    shr ax, 9
    mov di, 128
    sub di, ax
    cmp bp, di
    jbe .count_ok
    mov bp, di
.count_ok:
    mov [chs_count], bp
    mov byte [chs_retry], 3

.retry:
    mov cx, [chs_cx]
    mov dx, [chs_dx]
    mov ax, 0x0200
    mov al, [chs_count]
    mov dl, [boot_drive]
    push bx
    push si
    sti
    int 0x13
    pop si
    pop bx
    jnc .read_ok
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [chs_retry]
    jnz .retry
    jmp .fail

.read_ok:
    mov ax, [chs_count]
    shl ax, 9
    add bx, ax
    jnc .same_seg
    mov ax, es
    add ax, 0x1000
    mov es, ax
.same_seg:
    mov ax, [chs_count]
    add word [chs_lba], ax
    adc word [chs_lba+2], 0
    sub si, [chs_count]
    jmp .loop

.done:
    clc
    jmp .out
.fail:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

disk_error:
    mov ax, 0x0003
    int 0x10
    mov si, disk_err_msg
.pe2:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x04
    int 0x10
    jmp .pe2
.halt:
    cli
    hlt
    jmp .halt

boot_putc:
    push ax
    push bx
    push cx
    push dx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    pop dx
    pop cx
    pop bx
    pop ax
    ret

enable_a20:
    ; BIOS A20 first; AMI/USB paths often also need the fast A20 gate.
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

; GDT (Global Descriptor Table)

gdt_start:
    ; Null descriptor
    dq 0x0000000000000000

    ; Code segment descriptor: base 0x0, limit 0xFFFFF, 4K granularity, 32-bit
    dq 0x00CF9A000000FFFF

    ; Data segment descriptor: base 0x0, limit 0xFFFFF, 4K granularity, 32-bit
    dq 0x00CF92000000FFFF

    ; 64-bit code segment descriptor (L=1, D=0)
    dq 0x00AF9A000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEL equ 0x08
DATA_SEL equ 0x10
LONG_CODE_SEL equ 0x18

kernel_entry dd 0
phdr_size   dw 16               ; 16 for BOSX, 20 for LARDX v2
kernel_lba_base dd KERNEL_LBA

; -----------------------------
; VBE (real mode) framebuffer setup
; -----------------------------

; bootinfo struct lives below stage2 and the kernel staging buffer.
BOOTINFO_SEG equ 0x0500
BOOTINFO_OFF equ 0x0000
VBE_MODEINFO_OFF equ 0x0200

vbe_try_enable:
    pusha
    push ds
    push es

    xor ax, ax
    mov ds, ax

    ; Default text mode first (in case firmware needs it)
    mov ax, 0x0003
    int 0x10

    ; ES = BOOTINFO_SEG to write bootinfo + modeinfo
    mov ax, BOOTINFO_SEG
    mov es, ax

    ; Clear bootinfo area (first 64 bytes)
    xor di, di
    xor ax, ax
    mov cx, 32
    rep stosw

    ; Query VBE mode info for 1024x768x32 (0x118)
    mov ax, 0x4F01
    mov cx, 0x0118
    mov di, VBE_MODEINFO_OFF
    int 0x10
    cmp ax, 0x004F
    jne .fail

    ; Set VBE mode with LFB enabled (bit 14)
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .fail

    ; Fill bootinfo (version 1)
    ; magic = 'BINF'
    mov dword [es:BOOTINFO_OFF + 0], 0x464E4942
    mov word  [es:BOOTINFO_OFF + 4], 1

    ; VBE: pitch +16, xres +18, yres +20, bpp +25, physbase +40.
    ; bootinfo_t: fb_addr +8, width +12, height +14, pitch +16, bpp +18.
    mov ax, [es:VBE_MODEINFO_OFF + 16]
    mov [es:BOOTINFO_OFF + 16], ax
    mov ax, [es:VBE_MODEINFO_OFF + 18]
    mov [es:BOOTINFO_OFF + 12], ax
    mov ax, [es:VBE_MODEINFO_OFF + 20]
    mov [es:BOOTINFO_OFF + 14], ax
    mov al, [es:VBE_MODEINFO_OFF + 25]
    mov [es:BOOTINFO_OFF + 18], al
    mov eax, [es:VBE_MODEINFO_OFF + 40]
    mov [es:BOOTINFO_OFF + 8], eax

    jmp .ok

.fail:
    ; Leave bootinfo magic = 0 (kernel will fall back to text mode)
.ok:
    pop es
    pop ds
    popa
    ret
