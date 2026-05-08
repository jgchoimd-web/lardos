# LSH (Lard Shell)

**LSH** is the LardOS shell, available in the GUI LSH tab. Drive letters: X (default), Y (floppy), Z (RAM).

## Commands

- `dir` `type` `ver` `echo` `cls` `bosl` `cd` `X:` etc.
- `post` / `selftest` - rerun the Power-On Self-Test diagnostics.
- `magic command [args]` - predict and run a mistyped safe built-in command.
- `mode [status|probe|real]` - inspect or run the controlled real16/long64 bridge.
- `sram` / `screenram` - use a quiet or selected screen rectangle as scratch RAM.
- `screencheck status|retro|test` - probe or draw the retro visual screen checker.
- `exgui on|off|style|layout|next` - extended desktop and window-manager shell.
- `exexgui on|off|focus|next` - sketch split GUI with GUI, terminal, and status panes.
- `lguilib status|show|use|test [file.lguilib]` - inspect or apply native GUI library themes.
- `awake status|test` - inspect the Awakening fast-boot background loader.
- `oslink status|bus|emit|ping|send|exec|recv|peers` - local/remote OSLink messages and safe remote commands.
- `task list|set|default|run|boost|urgent|drop` - inspect and change task priority.
- `tasktop` - show queued tasks with status and priority bars.
- `nice priority command` - queue a command at a chosen priority.
- `bootprof status|set` - inspect or select normal, safe, netoff, dev, or awakening boot profiles.
- `crashlog show|clear|test` - inspect or write diagnostic crash history.
- `larsform file` / `larsact file index` - list or run LARS form buttons.
- `lpack info|list|install file.lpack` - inspect or install a native LardPack package.
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
- `task pause id` keeps a task visible but skips it until `task resume id`
