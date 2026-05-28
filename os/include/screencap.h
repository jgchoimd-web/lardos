#pragma once

#include <stdint.h>

#define SCREENCAP_SHOT_DEFAULT "screen.lshot"
#define SCREENCAP_REC_DEFAULT "screenrec.lrec"
#define SCREENCAP_REPORT_DEFAULT "screencap.lardd"

typedef struct {
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t last_shot_width;
    uint32_t last_shot_height;
    uint32_t last_shot_bytes;
    uint32_t shot_count;
    uint32_t rec_active;
    uint32_t rec_width;
    uint32_t rec_height;
    uint32_t rec_frames;
    uint32_t rec_max_frames;
    uint32_t rec_bytes;
    int32_t last_error;
    char last_file[32];
} screencap_info_t;

int screencap_screenshot(const char* file, uint32_t out_w, uint32_t out_h);
int screencap_record_start(const char* file, uint32_t max_frames, uint32_t out_w, uint32_t out_h);
int screencap_record_stop(void);
int screencap_record_frame(void);
void screencap_after_render(void);
void screencap_info(screencap_info_t* out);
int screencap_report(void);
int screencap_selftest(void);
