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
module now completes a constrained TLS 1.2 client path without external TLS
code: ClientHello with SNI, ServerHello parsing, DER leaf certificate parsing,
RSA SubjectPublicKeyInfo extraction, SAN/CN hostname checks, RTC-based
certificate validity checks, RSA PKCS#1 v1.5 ClientKeyExchange, SHA-256
transcript hashing and PRF key schedule, ChangeCipherSpec/Finished verification,
and AES-128-CBC/HMAC encrypted record read/write for HTTPS requests.

Supported cipher suites are `TLS_RSA_WITH_AES_128_CBC_SHA` and
`TLS_RSA_WITH_AES_128_CBC_SHA256`. Modern ECDHE-only servers will correctly fail
with an unsupported-cipher status. Public CA chain validation still requires a
native trust-anchor store; the current kernel validates the leaf certificate
identity and validity window but does not ship a generated CA bundle.

### Real Hardware

`os-image.bin` is a raw BIOS-style image. It is meant for QEMU first and for
legacy BIOS / CSM machines second. UEFI-only machines need a different
bootloader path.

### Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the 64-bit boot path, kernel pieces,
network stack, and native TLS layout.
