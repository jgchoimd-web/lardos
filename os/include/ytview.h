#pragma once

#include <stdint.h>

#define YTVIEW_ID_MAX 16u
#define YTVIEW_URL_MAX 192u

typedef struct {
    char id[YTVIEW_ID_MAX];
    char watch_url[YTVIEW_URL_MAX];
    char embed_url[YTVIEW_URL_MAX];
    char thumb_url[YTVIEW_URL_MAX];
    uint32_t shorts;
    uint32_t valid;
} ytview_info_t;

int ytview_parse_url(const char* input, ytview_info_t* out);
int ytview_format_card(const ytview_info_t* info, char* out, uint32_t cap);
int ytview_format_lars(const ytview_info_t* info, char* out, uint32_t cap);
int ytview_selftest(void);
