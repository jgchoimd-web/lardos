# LardOS Driver Branch

This branch is a user-installable driver catalog for LardOS.

The core rule is simple: driver files live outside the monolithic OS tree until
the user chooses to install them. The catalog uses LardOS-native formats only:

- `.drfl` is the LardOS driver descriptor format.
- `.rxr` bundles a driver descriptor plus a tiny RXE helper app so it can be
  installed with `rxr install`.
- `.lardd` documents the driver in LardOS' own document format.

## Layout

- `network/` network adapters.
- `storage/` disk and controller descriptors.
- `display/` display and framebuffer descriptors.
- `input/` keyboard, mouse, and pointer descriptors.
- `usb/` USB controller and device descriptors.
- `vm/` virtual-machine helper descriptors.
- `bundles/` combined RXR packs for common setups.
- `tools/` small in-tree helper tools for creating descriptors.

## Install In LardOS

1. Put the wanted `.rxr` file on a LardOS-readable drive such as `A:` or `Z:`.
2. Run `rxr verify A:core-drivers.rxr`.
3. Run `rxr install A:core-drivers.rxr`.
4. Run `drivers reload`.
5. Run `drivers` to inspect loaded DRFL descriptors.
6. Run `sync`, then restart if the driver must be present during boot probing.

Raw `.drfl` files can also be installed manually into a writable `.drfl` file
slot and loaded with `drivers load file.drfl`.

## Honesty Rule

A `.drfl` file describes hardware and selects an in-kernel driver name. It is
not a magic binary blob. If the actual driver code is not in the running kernel,
the descriptor must be marked as planned or source-only, not shipped as a
working install bundle.
