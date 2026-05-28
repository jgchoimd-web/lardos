LardOS Bundled Apps
===================

Put default apps that ship with the OS under `bundled/`.

These files are source app files, not C string literals. The build converts
them into generated kernel include files so the boot image still carries them
as read-only default files, while app authors can edit the app source here.

Rules:

- `.rxe` is a normal executable app.
- `.sysrxe` is a system executable app.
- Keep bundled app source out of `kernel/fs.c`; `fs.c` should only register the
  generated file payload.
- User-owned writable app templates may still live in writable filesystem
  slots, but default shipped apps belong here.
