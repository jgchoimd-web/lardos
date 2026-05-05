/*
 * LARSH - Lard Animation/Rich Shell
 * 운영체제 전용 Flash+Entry 스타일 포맷
 * 에니메이션, UI, 간단한 프로그램 제작용
 */
#pragma once

#include <stdint.h>

#define LARSH_MAX_OBJ  32
#define LARSH_MAX_KEY  128
#define LARSH_MAX_TEXT 64
#define LARSH_MAX_LMD  256

typedef struct {
    uint8_t type;     /* 0=rect 1=circle 2=line 3=text 4=lmd */
    int16_t x, y;
    int16_t w, h;     /* rect/line: width,height; circle: r in w */
    uint32_t color;
    char text[LARSH_MAX_TEXT];
    char lmd[LARSH_MAX_LMD];  /* LMD content when type==4 */
} larsh_obj_t;

typedef struct {
    uint32_t frame;
    uint8_t obj_id;
    uint8_t prop;     /* 0=x 1=y 2=w 3=h 4=color */
    int32_t value;
} larsh_key_t;

typedef struct {
    uint16_t w, h;
    uint8_t fps;
    uint32_t bg;
    uint8_t n_obj;
    uint8_t n_key;
    uint8_t loop;
    larsh_obj_t obj[LARSH_MAX_OBJ];
    larsh_key_t key[LARSH_MAX_KEY];
} larsh_scene_t;

/* Parse LARSH text. Returns 0 on success. */
int larsh_parse(const char* src, uint32_t len, larsh_scene_t* out);

/* Render frame at given tick to ARGB32 buffer (row-major, w * h pixels). */
void larsh_render_frame(const larsh_scene_t* s, uint32_t tick, uint32_t* pixels, uint16_t buf_w, uint16_t buf_h);
