# LardOS App Branch

This branch is for app ecosystem files that are not baked into the OS kernel.
Apps stay visible as `.rxe` files, and app bundles stay visible as `.rxr` files.

## Folders

- `games/` - small playable RXE games and game bundles.
- `tools/` - useful utility apps and RXR bundles.
- `creative/` - drawing, glyph, theme, and writing experiments.
- `dev/` - examples for app authors and LardOS language experiments.

## Install

Inside LardOS, copy a file into the writable filesystem, then use:

```text
rxe reload
rxr verify file.rxr
rxr install file.rxr
```

RXR install expands the bundled app and required files as normal user-owned
files, then reloads launchers. This keeps the LardOS rule: apps are files, not
hidden OS branches.
