; boot/boot.s - 512-byte BIOS boot sector
; Assembled with: nasm -f bin boot/boot.s -o boot/boot.bin

BITS 16
ORG 0x7C00

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

    ; Load LARDX executable (kernel) from disk (LBA 1..N) into 0x1000:0000.
    ; We read the first sector to get total file size, then read the rest.
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    ; Read 1 sector at LBA=1 into ES:BX (0x10000)
    mov dword [dap_lba], 1
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

    ; Read remaining sectors starting at LBA=2 into buffer+512
    mov si, [total_sectors]
    dec si                      ; remaining count
    jz .done_load

    mov dword [dap_lba], 2
    mov bx, 512                 ; offset into buffer
.read_loop:
    cmp si, 127
    jbe .chunk_ok
    mov cx, 127
    jmp .have_chunk
.chunk_ok:
    mov cx, si
.have_chunk:
    mov word [dap_count], cx
    mov word [dap_off], bx
    mov word [dap_seg], es
    call disk_read_lba
    ; bx += cx*512
    mov dx, cx
    shl dx, 9
    add bx, dx
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
    mov esp, 0x90000

    ; Parse LARDX/BOSX image at 0x00010000 and load segments to physical addresses.
    mov esi, 0x00010000

    ; phnum u16 at +0x08, phoff u32 at +0x0E, entry u32 at +0x0A (LARDX v2)
    ; BOSX: phnum +0x06, phoff +0x0C, entry +0x08. LARDX v2: phnum +0x08, phoff +0x0E, entry +0x0A
    ; For compat: BOSX has phnum at +0x06, LARD at +0x08. Check version at +4.
    mov ax, [esi+0x04]       ; version u16
    cmp ax, 2
    jb .bosx_layout
    ; LARDX v2 layout
    mov ax, [esi+0x08]
    mov ebp, eax
    mov eax, [esi+0x0E]
    jmp .have_phoff
.bosx_layout:
    ; BOSX layout
    mov ax, [esi+0x06]
    mov ebp, eax
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
    ; Identity-map first 2 MiB using a single 2 MiB page.
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
    ; Identity map 0..2MiB using PD[0] 2MiB page.
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

    ; PD[0] = 0x00000000 | P|RW|PS (2MiB page)
    mov dword [0x00072000], 0x00000083
    mov dword [0x00072000 + 4], 0x00000000

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
    mov cr0, rax

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
    jc disk_error
    pop ds
    popa
    ret

disk_error:
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

; BIOS passes boot drive in DL; save it early
save_drive:
    mov [boot_drive], dl
    ret

; -----------------------------
; VBE (real mode) framebuffer setup
; -----------------------------

; bootinfo struct lives at physical 0x90000 = 0x9000:0000
BOOTINFO_SEG equ 0x9000
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

    ; pitch u16 at +16, xres u16 at +18, yres u16 at +20, bpp u8 at +25, physbase u32 at +40
    mov ax, [es:VBE_MODEINFO_OFF + 16]
    mov [es:BOOTINFO_OFF + 14], ax
    mov ax, [es:VBE_MODEINFO_OFF + 18]
    mov [es:BOOTINFO_OFF + 12], ax
    mov ax, [es:VBE_MODEINFO_OFF + 20]
    mov [es:BOOTINFO_OFF + 10], ax
    mov al, [es:VBE_MODEINFO_OFF + 25]
    mov [es:BOOTINFO_OFF + 16], al
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

times 510-($-$$) db 0
dw 0xAA55

