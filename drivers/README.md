# LardOS Driver Branch

This branch is a user-installable driver catalog for LardOS.

The core rule is simple: driver files live outside the monolithic OS tree until
the user chooses to install them. The catalog uses LardOS-native formats only:

- `.drfl` is the LardOS driver-file format. DRFL 2 is text and must carry
  `CODE` lines, so the file contains the driver body the user can inspect/edit.
- `.rxr` bundles a driver file plus a tiny RXE helper app so it can be installed
  with `rxr install`.
- `.lardd` documents the driver in LardOS' own document format.

## Layout

- `network/` network adapter driver files.
- `storage/` disk and controller driver files.
- `display/` display and framebuffer driver files.
- `input/` keyboard, mouse, and pointer driver files.
- `usb/` USB controller and device driver files.
- `vm/` virtual-machine helper driver files.
- `bundles/` combined RXR packs for common setups.
- `tools/` small in-tree helper tools for creating driver files.

## Install In LardOS

1. Put the wanted `.rxr` file on a LardOS-readable drive such as `A:` or `Z:`.
2. Run `rxr verify A:core-drivers.rxr`.
3. Run `rxr install A:core-drivers.rxr`.
4. Run `drivers reload`.
5. Run `drivers` to inspect loaded DRFL files.
6. Run `drivers show rtl8139` or `drivers show ata-pio` to inspect the code
   carried inside the installed `.drfl`.
7. Run `sync`, then restart if the driver must be present during boot probing.

Raw DRFL 2 `.drfl` files can also be installed manually into a writable `.drfl`
slot and loaded with `drivers load file.drfl`.

## Honesty Rule

A `.drfl` file is the driver source/control body. It names hardware, declares
the type, and carries editable `CODE` lines. The kernel may still use a native
ABI path to run that code safely, but the code body is no longer hidden outside
the `.drfl` file.
