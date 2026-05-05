#pragma once

/* Run Korean BASIC-like source. Output to out (null-terminated). Returns 0 on success. */
int kr_basic_run(const char* src, char* out, unsigned out_cap);
