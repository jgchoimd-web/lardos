# LardOS

LardOS is a small educational 64-bit x86 operating system inspired by the
user-control spirit of TempleOS. It is built as a self-owned, inspectable OS
where power stays visible, editable, explainable, local, and recoverable.

The project favors in-tree C code, native LardOS formats, and user-controlled
system surfaces over hidden automation or external runtime dependencies.

Distribution branches carry focused variants. `lardos-arm` is the pure ARM
source port line for non-mobile ARM machines; it records ARM port requirements
without pretending that current x86 boot media are ARM artifacts.

## Values

LardOS is guided by a simple rule: the user owns the machine.

- Users should be able to inspect, change, override, repair, and replace OS
  behavior.
- Powerful actions should be visible and explainable.
- Risky changes should have history, rollback, or recovery paths.
- Native formats such as LARS, LARDD, LGUILIB, LTHEME, LSND, LPACK, RXR, DRFL,
  MDFS, SYSRXE, KMO, and LFS
  are part of the system identity.
- Kernel modules and new shell command surfaces should be reachable through
  visible user-owned channels such as KModTalk and KMO instead of hidden
  control paths; if the user explicitly chooses raw-control, the system should
  allow that power and make the risk visible.
- Release channels are explicit: `a` is official, `b` is beta, and `p` is a
  hotpatch.

Inside LardOS, run `values` or `userlaw show` to reread the built-in user-law
document.

## Build

The OS source lives in `os/`.

```bash
cd os
make
```

Release media can be built with:

```bash
make release
make release-all-hardware
```

Hardware release profiles include `universal`, `seabios`, `ami`, `vbox`, `usb`,
and `realpc`. Each profile produces versioned `.img` and `.iso` artifacts.

## Run

```bash
cd os
make run
make run-iso
```

The default QEMU target boots the BIOS image, enters 64-bit long mode, and opens
the LardOS GUI.

## Documentation

For detailed build notes, shell commands, native file formats, and feature
history, see `os/README.md` and `os/RELEASES.lardd`.

## License

LardOS is released under the Unlicense. See `LICENSE`.
