#pragma once

#include <stdint.h>

typedef enum {
    EXEXGUI_PANE_GUI = 0,
    EXEXGUI_PANE_TERM = 1,
    EXEXGUI_PANE_INFO = 2,
} exexgui_pane_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} exexgui_rect_t;

typedef struct {
    exexgui_rect_t outer;
    exexgui_rect_t gui;
    exexgui_rect_t term;
    exexgui_rect_t info;
    uint32_t border;
} exexgui_layout_t;

typedef struct {
    uint32_t enabled;
    uint32_t focus;
    uint32_t last_error;
    uint32_t workspace;
    exexgui_layout_t layout;
} exexgui_info_t;

void exexgui_init(void);
int exexgui_enable(int on);
int exexgui_set_focus(const char* name);
void exexgui_focus_next(void);
int exexgui_workspace_select(uint32_t id);
int exexgui_workspace_save(uint32_t id);
int exexgui_workspace_load(uint32_t id);
int exexgui_is_enabled(void);
void exexgui_info(exexgui_info_t* out);
const char* exexgui_focus_name(uint32_t focus);
int exexgui_layout_for(uint32_t screen_w, uint32_t screen_h, exexgui_layout_t* out);
void exexgui_draw_desktop(void);
void exexgui_draw_overlay(void);
int exexgui_selftest(void);
