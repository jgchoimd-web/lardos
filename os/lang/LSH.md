# LSH (Lard Shell)

**LSH** is the LardOS shell, available in the GUI LSH tab. Drive letters: X (default), Y (floppy), Z (RAM).

## Commands

- `dir` `type` `ver` `echo` `cls` `bosl` `cd` `X:` etc.
- `post` / `selftest` - rerun the Power-On Self-Test diagnostics.
- `magic command [args]` - predict and run a mistyped safe built-in command.
- `mode [status|probe|real]` - inspect or run the controlled real16/long64 bridge.
- `sram` / `screenram` - use a quiet or selected screen rectangle as scratch RAM.
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
- Commands run one per `gui_tick`
- Queue limit: 4 commands
