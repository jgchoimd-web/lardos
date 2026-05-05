## lard

This is a small educational 32‑bit x86 operating system that boots via BIOS,
switches to protected mode, and runs a C kernel.

### Prerequisites (macOS)

- `nasm`
- `qemu-system-i386`
- `gcc` and `binutils` (provided by Xcode Command Line Tools or Homebrew)

Example with Homebrew:

```bash
brew install nasm qemu
```

### Build

```bash
make
```

This produces `os-image.bin`, a raw floppy-sized disk image with the boot
sector and kernel.

### Run in QEMU

```bash
make run
```

You should see the OS boot in a QEMU window. The default `make run` line enables the **RTL8139** NIC with QEMU **user networking** (`-netdev user`).

### Internet, HTTPS, and YouTube

The kernel implements DHCP, DNS, and **plain HTTP (port 80)** only. There is **no TLS** in the kernel, so `https://` sites (including **YouTube**) cannot be contacted directly from lard. A full **video player / browser** is also out of scope for this project.

To still load **HTTPS** pages as text (for example `https://example.com/`):

1. On the **host** machine (where QEMU runs), start the bridge:

   ```bash
   ./scripts/host_https_bridge
   ```
   (Build with `make` first; or `gcc -O2 -o scripts/host_https_bridge scripts/host_https_bridge.c`.)

2. In the lard URL bar after boot, open:

   ```text
   http://10.0.2.2:8765/?url=https://example.com/
   ```

   In QEMU user networking, **`10.0.2.2`** is the host as seen from the guest.

3. **YouTube** often returns **403**, very large HTML, or scripts that only work in a real browser — **do not expect video playback** inside lard. To watch YouTube, use a browser on the host OS.

You can also use literal IPv4 addresses and ports in URLs (for example `http://10.0.2.2:8765/...`) without DNS.

### Notes

This project is for learning purposes and intentionally keeps things simple:
flat binaries, 32‑bit protected mode only, and a minimal kernel.

The kernel console and VGA text output accept **UTF-8** strings and map Unicode to
**CP437** (VGA font). Characters with no CP437 glyph are shown as `?` (for example
most CJK code points).

### Run on real hardware (USB)

This project produces `os-image.bin`, a **raw floppy-sized image**. Booting it
on real hardware works best on machines that support **legacy BIOS / CSM**
booting. Many modern UEFI-only systems will not boot this image without a
different (UEFI) bootloader.

#### Write the image to a USB drive (macOS)

1) Build the image:

```bash
make
```

2) Insert a USB drive, then find its disk identifier:

```bash
diskutil list
```

Look for something like `/dev/disk2` that matches your USB size.

3) Unmount the whole disk (replace `disk2` with yours):

```bash
diskutil unmountDisk /dev/disk2
```

4) Write the image (this **erases the USB**; double-check the disk number):

```bash
sudo dd if=os-image.bin of=/dev/rdisk2 bs=1m conv=sync
```

5) Eject the USB:

```bash
diskutil eject /dev/disk2
```

If `bs=1m` is rejected on your system, use `bs=1048576` instead.

#### Booting steps (BIOS / CSM)

- Enter firmware setup (often `F2`, `Del`, or `Esc` during power-on).
- **Disable Secure Boot** (required for legacy boot on many systems).
- **Enable CSM / Legacy Boot** (wording varies by vendor).
- Put the USB device first in the boot order, or use the one-time boot menu
  (often `F12`/`F10`/`Esc`) to pick it.

#### Troubleshooting

- **USB doesn’t show up as bootable**: your machine may be UEFI-only, or legacy
  boot is disabled. Enable CSM/Legacy Boot, or try an older machine.
- **Boots to a blinking cursor / black screen**: try a different USB port
  (USB-A vs USB-C adapters can matter), and confirm you wrote to the correct
  `/dev/diskN`.
- **Still no boot**: some BIOSes are picky about how they emulate a
  floppy-sized “superfloppy” image from USB. A more compatible approach is to
  generate a disk image with an MBR + partition and install a proper boot
  loader stage there (out of scope for this minimal BIOS image).

### 아키텍처

LardOS의 계층별 구조, 부팅 흐름, 파일시스템, Syscall, VM, GUI 등 전체 아키텍처는
[ARCHITECTURE.md](ARCHITECTURE.md)를 참고한다.

