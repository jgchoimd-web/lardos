# LardOS Architecture

## Shape

```text
lardos/
|-- os/
|   |-- boot/        BIOS boot sector and long-mode transition
|   |-- kernel/      64-bit kernel C and assembly
|   |-- include/     kernel headers
|   |-- scripts/     host build tools
|   |-- lang/        BOSL, GASM, LIL, LML, examples, libraries
|   |-- tools/       host LIL
|   |-- Makefile
|   |-- deps.mk
|   `-- linker.ld
`-- build/
```

There is no `third_party/` TLS tree. TLS work lives in `kernel/lard_tls.c` and
`include/lard_tls.h`.

## Boot

```mermaid
flowchart LR
    BIOS["BIOS"]
    Boot["boot.s"]
    Protected["32-bit protected mode"]
    Long["64-bit long mode"]
    Entry["entry64.s"]
    Kernel["kernel64.c:kmain"]

    BIOS --> Boot --> Protected --> Long --> Entry --> Kernel
```

The boot sector uses the 32-bit phase only as a bridge into long mode. The
kernel payload is built with `-m64`, linked as `elf_x86_64`, and entered through
`entry64.s`.

## Kernel

```mermaid
flowchart TB
    Kmain["kernel64.c"]
    GDT["gdt64"]
    IDT["idt64 / isr64"]
    MMU["mmu"]
    SMP["smp + ap_trampoline"]
    Syscall["syscall"]
    Drivers["pci / rtl8139 / ps2 / rtc"]
    Storage["fs / lfs / ldll"]
    Net["net DHCP DNS TCP HTTP"]
    TLS["lard_tls native TLS"]
    GUI["gui / lafillo / lsh"]
    VM["BOSL GASM LIL LML OSVM"]

    Kmain --> GDT
    Kmain --> IDT
    Kmain --> MMU
    Kmain --> SMP
    Kmain --> Syscall
    Kmain --> Drivers
    Kmain --> Storage
    Kmain --> Net
    Net --> TLS
    Kmain --> GUI
    GUI --> VM
```

## Build

```mermaid
flowchart LR
    BootS["boot/boot.s"]
    KernelS["kernel/*64.s"]
    KernelC["kernel/*.c"]
    NativeTLS["kernel/lard_tls.c"]
    NASM["nasm"]
    GCC["gcc -m64"]
    LD["ld -m elf_x86_64"]
    MKARDX["mkardx"]
    MKIMG["mkimg"]
    Image["os-image.bin"]

    BootS --> NASM
    KernelS --> NASM
    KernelC --> GCC
    NativeTLS --> GCC
    NASM --> LD
    GCC --> LD
    LD --> MKARDX --> MKIMG --> Image
```

## Networking And TLS

```mermaid
flowchart TB
    RTL["rtl8139"]
    Net["net.c"]
    TCP["small TCP path"]
    HTTP["net_http_get"]
    HTTPS["net_https_get"]
    TLS["lard_tls"]

    RTL --> Net --> TCP
    TCP --> HTTP
    TCP --> HTTPS --> TLS
```

The HTTPS path does not call an external TLS library or host HTTPS bridge.
`lard_tls` currently owns the TLS record writer, TLS 1.2 ClientHello, SNI
extension, basic ServerHello parsing, and explicit status reporting for the
remaining crypto work.

## Important Files

| Area | Files |
| --- | --- |
| Boot | `os/boot/boot.s` |
| 64-bit entry | `os/kernel/entry64.s`, `os/kernel/kernel64.c` |
| Descriptor tables | `os/kernel/gdt64.c`, `os/kernel/idt64.c`, `os/kernel/isr64.s` |
| Memory | `os/kernel/mem.c`, `os/kernel/mmu.c` |
| SMP | `os/kernel/smp.c`, `os/kernel/ap_trampoline.s`, `os/kernel/aux_kernel.s` |
| Network | `os/kernel/net.c`, `os/kernel/rtl8139.c` |
| Native TLS | `os/kernel/lard_tls.c`, `os/include/lard_tls.h` |
| GUI and shell | `os/kernel/gui.c`, `os/kernel/lsh.c`, `os/kernel/lafillo.c` |
