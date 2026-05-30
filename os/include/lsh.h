/*
 * LSH - Lard Shell
 *
 * Drive letters:
 *   X: = default (main FS: built-in + LFS)
 *   Y:/F: = floppy-style native MediaFS store
 *   Z:/S: = auxiliary SSD/HDD native MediaFS store
 *   A:/U: = first extra USB-style native MediaFS store
 *   R: = RAM (writable: notes.txt etc.)
 *   B~W except F/R/S/U/Y/Z: = future visible extra-drive letters;
 *       until backed, they remain visible main-FS aliases to avoid feature loss
 *
 * Custom commands: .lsh files (LSH\0 + name + type 0=BOSL + payload)
 */
#pragma once

#include <stdint.h>

#define LSH_MAX_LINE   256
#define LSH_MAX_OUTPUT 4096
#define LSH_MAX_ENV    32
#define LSH_MAX_VAR_LEN 64
#define LSH_PIPE_BUF   2048
#define LSH_MAX_PIPE_SEG 2
#define LSH_MAX_BG     8

/* Init LSH. Call after fs_init. */
void lsh_init(void);

/* Execute command line. Output appended to internal buffer. */
void lsh_exec(const char* line);

/* Get accumulated output (for display). Does not clear. */
const char* lsh_get_output(void);

/* Clear output buffer. */
void lsh_clear_output(void);

/* Get current drive (0='X', 1='Y', etc). */
char lsh_get_drive(void);

/* Set current drive by letter. */
void lsh_set_drive(char letter);

/* Get pipe stdin (when running as cmd2 in cmd1|cmd2). Returns 0 if none. */
const char* lsh_stdin(void);

/* Poll and run one background command. Returns 1 if a command was run. */
int lsh_poll_background(void);

/* True when in SUM (Super User Mode, ring 0). */
int lsh_in_sum_mode(void);

/* Global shortcut entry for SUM/ring-0 mode. */
void lsh_enter_sum_shortcut(void);

/* Non-mutating parser selftest for the settings shell. */
int lsh_cfgsh_selftest(void);

/* Non-mutating parser selftest for DOS mode command aliases. */
int lsh_dosmode_selftest(void);

/* Non-mutating output rollover selftest for long help text. */
int lsh_output_selftest(void);

/* Non-mutating command language selftest for ASCII case and Korean aliases. */
int lsh_language_selftest(void);
