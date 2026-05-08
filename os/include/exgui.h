#pragma once

#include <stdint.h>

typedef enum {
    EXGUI_STYLE_WIN = 0,
    EXGUI_STYLE_LINUX = 1,
    EXGUI_STYLE_MAC = 2,
} exgui_style_t;

typedef enum {
    EXGUI_LAYOUT_FLOAT = 0,
    EXGUI_LAYOUT_TILE = 1,
    EXGUI_LAYOUT_STACK = 2,
} exgui_layout_t;

typedef struct {
    uint32_t enabled;
    uint32_t style;
    uint32_t layout;
    uint32_t focused;
    uint32_t window_count;
    uint32_t last_error;
} exgui_info_t;

void exgui_init(void);
int exgui_enable(int on);
int exgui_set_style(const char* name);
int exgui_set_layout(const char* name);
void exgui_focus_next(void);
void exgui_info(exgui_info_t* out);
const char* exgui_style_name(uint32_t style);
const char* exgui_layout_name(uint32_t layout);
void exgui_draw_desktop(void);
void exgui_draw_overlay(void);
int exgui_selftest(void);
