# LSH (Lard Shell)

**LSH** is the LardOS shell, available in the GUI LSH tab. Drive letters: `_` (merged top-level drive over R/X/Y/Z/A), X (main built-in/LFS), Y/F (floppy-style media), Z/S (auxiliary SSD/HDD media), A/U (first extra USB-style media), R (RAM).

## Commands

- `dir` `type` `ver` `echo` `cls` `bosl` `cd` `X:` etc. `dir _:` lists the merged top-level drive with prefixes; `dir X:` lists read-only system files; `dir R:` lists every writable RAM file; `dir Y:`/`dir Z:`/`dir A:` list MediaFS device stores.
- `release policy` - show when to use `a`, `b`, or `p` release suffixes.
- Release numbers use `v<cycle>.<feature/change>.<patch/code-change><channel>-<subname>`; the third number is one digit only, so after 9 it carries into the middle number.
- `release codename` - show the current OS-era subname, now Mirage.
- `release lts` - compatibility command that says the old LTS model is retired and Tiara support ended.
- `lword`/`lwrite`, `lsheet`, `lshow` - edit the bundled native office document,
  workbook, and deck files from the shell. LardWrite supports title/section/
  bullet/quote/code/find/stats; LardSheet supports col/cell/formula/csv/sum;
  LardShow supports play/next/prev/slide/theme/note.
- `post` / `selftest` - rerun the Power-On Self-Test diagnostics.
- `post baseline` / `postbaseline show` - show the saved POST baseline report.
- `time` / `date` / `lunar` / `dangun` - show LardOS Time ticks, five-digit year dates, native lunar view, and Dangun year.
- `magic command [args]` - predict and run a mistyped safe built-in command.
- `magic dryrun command [args]` - show the Magic prediction without executing it.
- `magic explain` - show why the last Magic prediction executed or stopped.
- `install status|preview|hdd yes|ssd yes|guide` - preview or write the current
  LardOS boot image to an ATA HDD/SSD from inside the OS. `install` is
  raw-control for Magic, so automatic execution requires `magic -f`.
- `media list|format Z|sync all|read Z file|write Z file text|append Z file text|delete Z file` - manage native MDFS device stores for auxiliary SSD/HDD (`Z:`/`S:`), first extra USB-style (`A:`/`U:`), and floppy-style (`Y:`/`F:`) files. Normal `dir`, `type`, `write`, `append`, and `copy` commands can also use those drives; `_:` reads across R/X/Y/Z/A and routes writes visibly to `R:`.
- `megaclip status|list|mode stack|single|order|push text|file path|pull slot|write slot file` - manage moving clipboard history. `pinclip list|set slot text|from fixed-slot [megaclip-slot]|pull slot|write slot file|clear slot|reload` manages fixed shortcut slots that stay put when history moves; `from` freezes latest or chosen MegaClipboard history into a fixed slot. GUI shortcuts: `Ctrl+Y`, `Ctrl+P`, `Ctrl+Space` then `1..9`/`0`, and `Ctrl+Space` then `P` then `1..9`/`0` for fixed slots.
- `dos on|off|status|help|map|log|test` - enter L-DOS mode, a native DOS-style shell layer with visible `_:`/`C:`/`A:`/`Z:`/`U:`/`R:` mapping, case-insensitive `DIR`, `TYPE`, `COPY`, `DEL`, `DEL -F`, `DEL -T`, `RESTORE`, `UNDELETE`, `TOMB`, `REN`, `MD`, `RD`, `CD`, `CLS`, `VER`, `SET`, `ECHO`, `MEM`, and `EXIT`.
- `tomb list|show|hide file|drop file|clear` - inspect, create soft hides, or delete user-owned `DEL -F` hard-delete records from `fsdelete.lardd`.
- `mode [status|probe|real|guard]` - inspect or run the controlled real16/long64 bridge.
- `vm status|limits|selftest|clear` - inspect VM Monitor counters and budgets
  for BOSL, LIL, GASM, Lafillo VM, and OSVM.
- `shrine status|list|info|verify|run|test [file.shrine]` / `lss ...` -
  inspect and run LSS Shrine subsystem wrappers, including BOSL payload
  magic/version verification. `srine` is accepted too.
- `hc file.hc [input]` / `holyc ...` - run LardOS-owned HC source through the native app runner from the shell.
- `gasm file.gasm` - run an in-tree GASM source file from LSH.
- `sram` / `screenram` - use a quiet or selected screen rectangle as scratch RAM.
- `screencheck status|retro|test` - probe or draw the retro visual screen checker.
- `screenshot [file.lshot] [w h]` / `shot ...` / `screencap status|report|test` - capture the visible GUI into a native local `LSHOT` file and keep `screencap.lardd` updated. Defaults: `screen.lshot`, 96x54 RGB565.
- `screenrec status|start [frames] [w h] [file.lrec]|frame|stop|report|test` - record a short visible-screen sequence into a native local `LREC` luma-frame file. Defaults: `screenrec.lrec`, 8 frames, 64x36.
- `wallpaper status|color|pattern|bmp|lrec|use|reload|reset` - choose a user-owned desktop background. `wallpaper lrec screenrec.lrec` uses a native LREC recording as live wallpaper; the visible config stays in `wallpaper.lardd`.
- `sound status|on|off|boot|fx|play|new` - manage native `LSND` vector sound files, boot sound, and short PC-speaker effect sounds. The visible policy lives in `sound.lardd`.
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
- `lfsdoctor scan|repair|show|layout` - filesystem and LPST persistence health report, plus the LFS v2 unbounded-varuint/extent layout check.
- `panic capsule` / `paniccapsule show` - write and view a recovery bundle report; runtime-ready kernel panic paths enter a real16-backed PanicRoom texture screen before halt.
- `panicroom texture` - enter the real16 bridge and draw the default LPR PanicRoom texture.
- `bootmap` / `bootreplay show` / `post baseline` / `postbaseline show` / `devmap draw` / `oldcheck draw` / `awakemon` - boot, POST, device, storage, and Awakening views.
- `ltheme list|show|preview|use name` - native shell theme presets and `.ltheme` files.
- `cfgprof save|load name` / `cfg profile` in the feature map / `userlaw show` - settings profiles and user-right policy.
- `oschat say|send|read` - local OSLink chat-style messages.
- `larsview open|reload|back|actions file` / `larsapp open|form|run` / `notes show|add|clear` - native document/app browser state and notes synced between `notes.lardd` and GUI `notes.txt`.
- `kmo list|reload|show|run|create|command|raw|set|delete` - manage native `.kmo` kernel module files; `COMMAND name`, `SHELL name`, or `BIND name` turns a KMO into a direct shell command without editing LSH, normal KMO routes through visible KModTalk targets, while `kmo raw file.kmo command`, `RAW 1`, or `TARGET raw` explicitly enables direct risky LSH/raw-control execution.
- `lunit run tests.lunit` - run small native feature tests.
- `lar list [archive.lar]`, `extract archive.lar member [password]`, `lar extract [archive.lar] [member] [password]`, and `lar pass out.lar member sourcefile password` - inspect, extract, or create native LAR archives with optional method-1 password-protected entries. `larls`, `larx`, and `larpass` remain aliases.
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
- `LANG HC` in RXE/SYSRXE files selects the HolyC-flavored native app runner. `hello.hc`, `hc_guide.lardd`, and `hc_demo.rxe` are bundled examples.
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
- `screenshot` and `screenrec` store local native capture files (`screen.lshot`, `screenrec.lrec`) plus `screencap.lardd`; they do not use host capture, cloud upload, or external codecs
- `sound` stores local native vector audio as editable `LSND` events plus `sound.lardd`; it does not hide samples in external codecs or require host audio plumbing

## Settings Shell

- `cfgsh` enters the settings-focused `CFG#` prompt; `exitcfg` leaves it
- Inside `CFG#`, use `setting value`: `awake on`, `ltheme night`, `sound off`, `http 3`, `boot 4`, `priority 10`
- `buddy on` / `buddy off`, `bugeye on`, `ltheme night`, and `rollback snap|apply` can also be changed from `CFG#`
- Outside `CFG#`, use `cfg setting value` for one-shot changes
- Number maps: http 1=GET 2=POST 3=HEAD; boot 1=normal 2=safe 3=netoff 4=dev 5=awakening
