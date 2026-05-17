# LSH (Lard Shell)

**LSH** is the LardOS shell, available in the GUI LSH tab. Drive letters: X (main built-in/LFS), Y/F (floppy-style media), Z/S (auxiliary SSD/HDD media), A/U (first extra USB-style media), R (RAM).

## Commands

- `dir` `type` `ver` `echo` `cls` `bosl` `cd` `X:` etc. `dir X:` lists read-only system files; `dir R:` lists every writable RAM file; `dir Y:`/`dir Z:`/`dir A:` list MediaFS device stores.
- `release policy` - show when to use `a`, `b`, or `p` release suffixes.
- `post` / `selftest` - rerun the Power-On Self-Test diagnostics.
- `post baseline` / `postbaseline show` - show the saved POST baseline report.
- `time` / `date` / `lunar` / `dangun` - show LardOS Time ticks, five-digit year dates, native lunar view, and Dangun year.
- `magic command [args]` - predict and run a mistyped safe built-in command.
- `magic dryrun command [args]` - show the Magic prediction without executing it.
- `magic explain` - show why the last Magic prediction executed or stopped.
- `install status|preview|hdd yes|ssd yes|guide` - preview or write the current
  LardOS boot image to an ATA HDD/SSD from inside the OS. `install` is
  raw-control for Magic, so automatic execution requires `magic -f`.
- `media list|format Z|sync all|read Z file|write Z file text|append Z file text|delete Z file` - manage native MDFS device stores for auxiliary SSD/HDD (`Z:`/`S:`), first extra USB-style (`A:`/`U:`), and floppy-style (`Y:`/`F:`) files. Normal `dir`, `type`, `write`, `append`, and `copy` commands can also use those drives.
- `dos on|off|status|help|map|log|test` - enter L-DOS mode, a native DOS-style shell layer with case-insensitive `DIR`, `TYPE`, `COPY`, `DEL`, `DEL -F`, `DEL -T`, `RESTORE`, `UNDELETE`, `TOMB`, `REN`, `MD`, `RD`, `CD`, `CLS`, `VER`, `SET`, `ECHO`, `MEM`, and `EXIT`.
- `tomb list|show|hide file|drop file|clear` - inspect, create soft hides, or delete user-owned `DEL -F` hard-delete records from `fsdelete.lardd`.
- `mode [status|probe|real|guard]` - inspect or run the controlled real16/long64 bridge.
- `vm status|limits|selftest|clear` - inspect VM Monitor counters and budgets
  for BOSL, LIL, GASM, Lafillo VM, and OSVM.
- `shrine status|list|info|verify|run|test [file.shrine]` / `lss ...` -
  inspect and run LSS Shrine subsystem wrappers, including BOSL payload
  magic/version verification. `srine` is accepted too.
- `gasm file.gasm` - run an in-tree GASM source file from LSH.
- `sram` / `screenram` - use a quiet or selected screen rectangle as scratch RAM.
- `screencheck status|retro|test` - probe or draw the retro visual screen checker.
- GUI polish was beta-tracked in `v1.36.0b` and promoted in official `v1.40.0a` with the glyph and rough-edge repair track; `v1.58.0a` makes the default GUI a desktop with app icons, a dock, and hideable app windows, `v1.58.1p` hotpatches settings-panel clicks, full-desktop window movement, and fullscreen/restore, `v1.59.0b` adds runtime desktop/dock items, folders, per-app windows, and z-order, `v1.59.0a` promotes that model officially, `v1.60.0a` adds official L-DOS mode, `v1.60.1p` adds `DEL -F` read-only tombstones plus restore, `v1.61.0a` adds user-owned tombstone record deletion, `v1.62.0a` makes `DEL -F` hard-delete from the active read-only filesystem view, `v1.63.0a` adds the official in-OS HDD/SSD installer option, and `v1.63.1p` fixes the VirtualBox black-screen boot memory layout.
- `glyph demo|list|load|auto|show|move|copy|rename|pixel|clear|live|click|insert|write` - bind BMP pictures to private-use Unicode slots U+E000..U+E0FF, edit assigned slots, render them inline, click them in the GUI, and toggle realtime hover/click rendering.
- `cursor status|set U+E000|off` - bind the GUI mouse cursor to a user-owned picture Unicode slot.
- `cfgsh` / `cfg setting value` - settings shell for `mode-name on|off` or numbered values.
- `buddy on|off|status|joke|next|mood` - optional roaming assistant overlay with calm/funny/strict/silent moods.
- `bugeye on|off|scan` - visual bug monitor for framebuffer/layout checks; writes `bugreport.lardd`.
- `bugreplay status|last|show|draw|clear` - replay BugEye scan frames from `bugreplay.lardd`.
- `lardtrace on|off|show|module name` / `trace ...` - LardTrace event timeline for shell/modules.
- `netwatch on|off|show|clear` - readable network, OSLink, and HTTP GET/POST/HEAD watcher.
- `webstack status|guide|demo|selftest` - inspect the native LARS/HTTP stack, including LARS `link`/`fetch` records and the request-builder selftest.
- `journal show|add|clear` - automatic `.lardd` OS journal.
- `rollback snap|last|apply` - settings snapshot and restore.
- `trust list|allow|deny|history` - user-owned permission policy map and audit log.
- `lfsdoctor scan|repair|show` - filesystem and LPST persistence health report.
- `panic capsule` / `paniccapsule show` - write and view a recovery bundle report; runtime-ready kernel panic paths enter a real16-backed PanicRoom texture screen before halt.
- `panicroom texture` - enter the real16 bridge and draw the default LPR PanicRoom texture.
- `bootmap` / `bootreplay show` / `post baseline` / `postbaseline show` / `devmap draw` / `oldcheck draw` / `awakemon` - boot, POST, device, storage, and Awakening views.
- `ltheme list|show|preview|use name` - native shell theme presets and `.ltheme` files.
- `cfgprof save|load name` / `cfg profile` in the feature map / `userlaw show` - settings profiles and user-right policy.
- `oschat say|send|read` - local OSLink chat-style messages.
- `larsview open|reload|back|actions file` / `larsapp open|form|run` / `notes show|add|clear` - native document/app browser state and notes synced between `notes.lardd` and GUI `notes.txt`.
- `kmo list|reload|show|run|create|command|raw|set|delete` - manage native `.kmo` kernel module files; `COMMAND name`, `SHELL name`, or `BIND name` turns a KMO into a direct shell command without editing LSH, normal KMO routes through visible KModTalk targets, while `kmo raw file.kmo command`, `RAW 1`, or `TARGET raw` explicitly enables direct risky LSH/raw-control execution.
- `lunit run tests.lunit` - run small native feature tests.
- `lguilib status|show|use|test [file.lguilib]` - inspect or apply native GUI library themes.
- `awake on|off|status|test` - control the default-off Awakening fast-boot mode.
- `oslink status|bus|emit|ping|send|exec|recv|peers` - local/remote OSLink messages and safe remote commands.
- `task list|set|default|run|history|boost|urgent|drop` - inspect and change task priority.
- `priority history` / `prio history` - audit who granted priority `lev.10`.
- `tasktop` - show queued tasks with status and priority bars.
- `nice priority command` - queue a command at a chosen priority.
- `bootprof status|set` - inspect or select normal, safe, netoff, dev, or awakening boot profiles.
- `crashlog show|clear|test` - inspect or write diagnostic crash history.
- `larsform file` / `larsact file index` - list or run LARS button/link/fetch/input actions.
- `lpack info|list|verify|install|undo file.lpack` - inspect, validate, install, or roll back a native LardPack package.
- `rxr info|list|verify|install|undo file.rxr` / `rxr path rxr/file` - inspect, validate, install, roll back, or resolve a native app bundle containing one RXE/SYSRXE app plus required files. LardOS exposes bundled files through the OS filesystem namespace `rxr/name`.
- `vpath path` / `pathmap path` - inspect the OS filesystem mapping for a virtual path such as `folder/inside/path`. Use quotes for spaces, for example `vpath "Final Final Release/final fix"`.
- `drivers status|reload|load file.drfl|show name` - inspect loaded DRFL files, rescan installed `.drfl` files, load one driver file directly, or show the code carried inside a DRFL 2 file.
- `set` — list or set environment variables
- `more` — read from pipe stdin (use with `|`)

## Environment variables

- `set` — list all variables
- `set VAR=value` — set variable
- `set VAR` — show value
- Expansion: `%VAR%` in command line (before parsing)

## Pipe

- `cmd1 | cmd2` — run cmd1, pass output to cmd2
- Example: `type readme.txt | more`
- Two segments only (`cmd1 | cmd2`)

## Background

- `cmd &` — queue command, return immediately
- Commands run one per `gui_tick`, highest effective priority first
- Queue limit: 8 tasks
- `task set id priority` changes queued work; priority range is 0..10
- Priority `lev.10` is user-grantable urgent work via `task urgent id`, `task set id 10`, or `nice 10 command`; wait-time aging cannot create it
- `task history` shows lev.10 grants with sequence, actor, action, and task name
- `task pause id` keeps a task visible but skips it until `task resume id`

## Recovery And Audit

- `bugreplay` keeps a small ring of BugEye frames with scan number, changed samples, bad tiles, and last render error
- `trust history` records who changed file, screen, network, OSLink, package, raw, and SUM permissions
- `lpack verify file.lpack` checks package structure before install and prints hash, warnings, errors, installable files, and bytes
- `lpack undo` restores the last writable-file snapshot captured before a package install
- `rxr verify file.rxr` checks app bundle structure, primary app, payload hash, installable files, and RXR writable slots before install
- `rxr undo` restores the last app-bundle snapshot and reloads RXE/SYSRXE launchers
- `rxr/name` is resolved by the OS filesystem layer to the installed target for files carried inside the app bundle, so RXE/SYSRXE source does not hard-code the install drive/path
- `paniccapsule show` builds a compact LARDD recovery bundle from panic room, crashlog, BugEye, replay, trust, priority, LFSDoctor, and BootMap state
- On runtime-ready `panic` or `panic_u64`, PanicRoom first draws the real16 default texture, auto-writes the capsule, and offers crashlog view, capsule rebuild, rollback apply, queued-task drop, and halt keys
- `lfsdoctor repair` runs the native LPST repair path and rewrites `lfsdoctor.lardd`

## Settings Shell

- `cfgsh` enters the settings-focused `CFG#` prompt; `exitcfg` leaves it
- Inside `CFG#`, use `setting value`: `awake on`, `ltheme night`, `http 3`, `boot 4`, `priority 10`
- `buddy on` / `buddy off`, `bugeye on`, `ltheme night`, and `rollback snap|apply` can also be changed from `CFG#`
- Outside `CFG#`, use `cfg setting value` for one-shot changes
- Number maps: http 1=GET 2=POST 3=HEAD; boot 1=normal 2=safe 3=netoff 4=dev 5=awakening
