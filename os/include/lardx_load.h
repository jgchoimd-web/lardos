#pragma once

/*
 * LARDX user executable loader.
 * Loads LARDX (IMAGE_USER) from FS, maps segments, builds argv, runs.
 *
 * Returns 0 on success (does not return until program exits via SYS_EXIT).
 * Returns negative on error (file not found, bad format, etc).
 */
int lardx_run(const char* path, int argc, const char** argv);
int lardx_run_sandbox(const char* path, int argc, const char** argv);
