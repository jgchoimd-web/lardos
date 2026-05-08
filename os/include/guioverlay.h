#pragma once

#include <stdint.h>

typedef struct {
    uint32_t win_x;
    uint32_t win_y;
    uint32_t win_w;
    uint32_t win_h;
    uint32_t mouse_x;
    uint32_t mouse_y;
    uint32_t app_id;
    uint32_t settings_open;
    uint32_t button_pressed;
    uint32_t textbox_focused;
    uint32_t loading;
    uint32_t http_post_mode;
    uint32_t user_sandbox;
} guioverlay_state_t;

void guioverlay_draw(const guioverlay_state_t* state);
int guioverlay_selftest(void);
