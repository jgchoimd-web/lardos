## lard

LardOS is a small educational 64-bit x86 operating system. It boots from BIOS,
switches through protected mode into long mode, then runs the kernel at
`entry64.s` / `kernel64.c`.

### Prerequisites

- `nasm`
- `qemu-system-x86_64`
- `gcc`
- `ld`
- `make`

### Build

```bash
make
```

This produces `os-image.bin`, a raw bootable image with the boot sector and
64-bit kernel payload.

### Run in QEMU

```bash
make run
```

The default run target uses `qemu-system-x86_64`, an RTL8139 NIC, QEMU user
networking, and `-smp 3`.

### Networking And TLS

The kernel networking stack owns DHCP, DNS, IPv4, UDP, a small TCP path, and
plain HTTP.

TLS is intentionally in-tree now. External TLS libraries, host fetch bridges,
and generated CA bundles are not linked into the kernel. The native `lard_tls`
module currently builds a TLS 1.2 ClientHello, sends it over the kernel TCP
stack, parses ServerHello, and then returns a clear
`native TLS crypto is not finished` status until the owned crypto pieces are
implemented.

The next TLS work is:

- certificate parsing and validation
- key exchange
- transcript hashing and key schedule
- encrypted record read/write
- HTTP over completed TLS sessions

### Real Hardware

`os-image.bin` is a raw BIOS-style image. It is meant for QEMU first and for
legacy BIOS / CSM machines second. UEFI-only machines need a different
bootloader path.

### Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the 64-bit boot path, kernel pieces,
network stack, and native TLS layout.
