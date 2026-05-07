#pragma once

#include <stdint.h>

/*
 * LardOS-native document formats.
 *
 * LARS  - Lard Structured document records.
 * LARDD - Lard Document Draft records, used instead of Markdown for local docs.
 */
int lard_doc_to_text(const char* input, uint32_t input_len, char* out, uint32_t out_cap);

