#pragma once

#include <stdint.h>

int ps2_init(void);
int ps2_mouse_init(void);
int ps2_mouse_poll(int* out_dx, int* out_dy, int* out_buttons);

#define PS2_MOUSE_BUTTON_MASK 0x07
#define PS2_MOUSE_WHEEL_SHIFT 8

typedef enum {
    PS2K_NONE = 0,
    PS2K_ASCII = 1,
    PS2K_LEFT,
    PS2K_RIGHT,
    PS2K_UP,
    PS2K_DOWN,
    PS2K_HOME,
    PS2K_END,
    PS2K_PGUP,
    PS2K_PGDN,
    PS2K_DEL,
    PS2K_F10,
    PS2K_CTRL_Y,
    PS2K_CTRL_P,
    PS2K_CTRL_SPACE,
    PS2K_CTRL_H,
} ps2_key_kind_t;

typedef struct {
    ps2_key_kind_t kind;
    char ch; // valid if kind==PS2K_ASCII
} ps2_key_t;

// Returns 0 and sets *out on a keypress.
// Returns 1 if no key available. Negative on error.
int ps2_kbd_poll(ps2_key_t* out);
