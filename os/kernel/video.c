#include "video.h"

static volatile uint8_t* const VIDEO_BUFFER = (uint8_t*)0xA0000;

void video_clear(uint8_t color)
{
    for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
        VIDEO_BUFFER[i] = color;
    }
}

void video_putpixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= VIDEO_WIDTH || y < 0 || y >= VIDEO_HEIGHT) {
        return;
    }
    VIDEO_BUFFER[y * VIDEO_WIDTH + x] = color;
}

void video_draw_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            video_putpixel(x + i, y + j, color);
        }
    }
}

