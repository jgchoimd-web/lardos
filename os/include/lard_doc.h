#pragma once

#include <stdint.h>

/*
 * LardOS-native document formats.
 *
 * LARS  - Lard Structured document records.
 * LARDD - Lard Document Draft records, used instead of Markdown for local docs.
 */
int lard_doc_to_text(const char* input, uint32_t input_len, char* out, uint32_t out_cap);

typedef struct {
    char kind[8];
    char label[48];
    char command[128];
} lard_doc_action_t;

int lard_doc_action_count(const char* input, uint32_t input_len);
int lard_doc_action_at(const char* input, uint32_t input_len, uint32_t index, lard_doc_action_t* out);
int lard_doc_selftest(void);
