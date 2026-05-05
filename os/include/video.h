#pragma once

#include <stdint.h>

#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 200

void video_clear(uint8_t color);
void video_putpixel(int x, int y, uint8_t color);
void video_draw_rect(int x, int y, int w, int h, uint8_t color);

