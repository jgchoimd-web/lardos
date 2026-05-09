; boot/boot.s - stage2 BIOS loader.
; Stage1 loads this at 0x7E00, then this loads the LARDX kernel.

BITS 16
ORG 0x7E00

%ifndef KERNEL_LBA
%define KERNEL_LBA 5
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

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

    ; Try to switch to a VBE linear framebuffer mode for GUI.
    ; If it fails, we keep text mode.
    call vbe_try_enable

    ; Load LARDX executable (kernel) from disk into 0x1000:0000.
    ; We read the first sector to get total file size, then read the rest.
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    ; Read 1 sector at KERNEL_LBA into ES:BX (0x10000)
    mov dword [dap_lba], KERNEL_LBA
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

    ; Read remaining sectors starting at KERNEL_LBA+1 into buffer+512
    mov si, [total_sectors]
    dec si                      ; remaining count
    jz .done_load

    mov dword [dap_lba], KERNEL_LBA + 1
    mov bx, 512                 ; offset into buffer
.read_loop:
    cmp si, 127
    jbe .chunk_ok
    mov cx, 127
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

    ; Enable A20 line (very simple fast A20 via BIOS, may not work everywhere)
    ; Use INT 15h, AX=2401h if available
    mov ax, 0x2401
    int 0x15

    ; Set up a simple GDT in this sector
    lgdt [gdt_descriptor]

    ; Enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEL:protected_start

; 32-bit code

BITS 32

protected_start:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000

    ; Parse LARDX/BOSX image at 0x00010000 and load segments to physical addresses.
    mov esi, 0x00010000

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
    mov esi, 0x00010000
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
    ; Identity-map first 8 MiB using 2 MiB pages.
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
    ; Identity map 0..8MiB using PD[0..3] 2MiB pages.
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

    ; PD[0..3] = 0..8MiB | P|RW|PS (2MiB pages)
    mov dword [0x00072000], 0x00000083
    mov dword [0x00072000 + 4], 0x00000000
    mov dword [0x00072000 + 8], 0x00200083
    mov dword [0x00072000 + 12], 0x00000000
    mov dword [0x00072000 + 16], 0x00400083
    mov dword [0x00072000 + 20], 0x00000000
    mov dword [0x00072000 + 24], 0x00600083
    mov dword [0x00072000 + 28], 0x00000000

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
total_sectors dw 0
chs_lba dd 0

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
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .ok
    call disk_read_chs
    jc disk_error
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

    mov ax, 0x0201
    mov dl, [boot_drive]
    int 0x13
    jc .fail

    add bx, 512
    jnc .same_seg
    mov ax, es
    add ax, 0x1000
    mov es, ax
.same_seg:
    inc dword [chs_lba]
    dec si
    jmp .loop

.done:
    clc
    jmp .out
.fail:
    stc
.out:
    pop es
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

; -----------------------------
; VBE (real mode) framebuffer setup
; -----------------------------

; bootinfo struct lives at physical 0x9C000 = 0x9C00:0000.
; Keep it above the kernel image staging buffer and below the EBDA/VGA area.
BOOTINFO_SEG equ 0x9C00
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

    ; ES = 0x9000 to write bootinfo + modeinfo
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
