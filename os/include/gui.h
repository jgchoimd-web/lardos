#pragma once

#include <stdint.h>

int gui_init(void);
void gui_handle_mouse(int dx, int dy, int buttons);
void gui_handle_key(char ch);
void gui_handle_key_nav(int kind);
void gui_tick(void);
void gui_render(void);
void gui_activity(void);  /* reset idle timer (call on mouse/key) */
int gui_screensaver_active(void);  /* 1 if screensaver is showing */
void gui_demo(void);

// GUI -> kernel integration for "fetch" demo
#define GUI_HTTP_URL_MAX 256u
#define GUI_HTTP_METHOD_MAX 8u
#define GUI_HTTP_BODY_MAX 1024u

typedef struct {
    char url[GUI_HTTP_URL_MAX];
    char method[GUI_HTTP_METHOD_MAX];
    char body[GUI_HTTP_BODY_MAX];
    uint32_t body_len;
} gui_http_request_t;

int gui_take_submit(gui_http_request_t* out); // returns 1 if got submit
void gui_set_response(const char* text);
void gui_set_loading(int on);
void gui_http_set_post_mode(int on);
int gui_http_post_mode(void);
void gui_http_set_method(int method);
int gui_http_method(void); /* 0=GET, 1=POST, 2=HEAD */
void gui_reload_sysrxe_apps(void);

#define GUI_AA_NONE 0
#define GUI_AA_UNAA 1
#define GUI_AA_BASIC 2
#define GUI_AA_NONLINEAR 3
#define GUI_RESIZE_LIVE 0
#define GUI_RESIZE_STRETCH 1
#define GUI_WALLPAPER_PLAIN 0
#define GUI_WALLPAPER_GRID 1
#define GUI_WALLPAPER_STRIPES 2
#define GUI_WALLPAPER_CHECKER 3
#define GUI_WALLPAPER_BMP 4
#define GUI_WALLPAPER_NAME_MAX 31u

typedef struct {
    uint32_t aa_mode;
    uint32_t brightness;
    uint32_t quality;
    uint32_t resize_mode;
    uint32_t screenram_lsb;
    uint32_t vblank_mode;
    uint32_t vblank_frames;
    uint32_t vblank_hits;
    uint32_t vblank_misses;
    uint32_t vblank_last;
} gui_render_info_t;

typedef struct {
    uint32_t mode;
    char name[GUI_WALLPAPER_NAME_MAX + 1u];
    char file[GUI_WALLPAPER_NAME_MAX + 1u];
    uint32_t color1;
    uint32_t color2;
    uint32_t bmp_w;
    uint32_t bmp_h;
    uint32_t last_error;
} gui_wallpaper_info_t;

int gui_render_set_aa_mode(int mode);
int gui_render_aa_mode(void);
int gui_render_set_brightness(int percent);
int gui_render_brightness(void);
int gui_resize_set_mode(int mode);
int gui_resize_mode(void);
int gui_screenram_lsb_enable(int on);
int gui_screenram_lsb_mode(void);
int gui_vblank_enable(int on);
int gui_vblank_mode(void);
void gui_render_info(gui_render_info_t* out);
int gui_render_effects_selftest(void);
int gui_wallpaper_set_color(uint32_t argb);
int gui_wallpaper_set_pattern(const char* pattern, uint32_t color1, uint32_t color2);
int gui_wallpaper_set_bmp(const char* file);
int gui_wallpaper_load_config_file(const char* file);
int gui_wallpaper_reload(void);
int gui_wallpaper_reset(void);
void gui_wallpaper_info(gui_wallpaper_info_t* out);
int gui_wallpaper_selftest(void);

#define GUI_RENAME_ANY 0
#define GUI_RENAME_APP 1
#define GUI_RENAME_FOLDER 2

int gui_rename_selected_label(const char* new_name);
int gui_rename_item_label(const char* old_name, const char* new_name, int kind_filter);

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t changed_samples;
    int window_inside;
    int response_view_ok;
    int chrome_ok;
} gui_post_info_t;

/* POST-visible screen sanity checks. Returns 0 when a framebuffer is present. */
int gui_post_check(gui_post_info_t* out);
int gui_img_glyph_interaction_selftest(void);
int gui_desktop_interaction_selftest(void);

typedef struct {
    uint32_t enabled;
    uint32_t cp;
    uint32_t assigned;
    uint32_t render_count;
    uint32_t fallback_count;
    uint32_t last_error;
} gui_cursor_info_t;

int gui_cursor_set_unicode(uint32_t cp);
void gui_cursor_disable(void);
void gui_cursor_info(gui_cursor_info_t* out);
int gui_unicode_cursor_selftest(void);

typedef struct {
    uint32_t enabled;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t capacity;
    uint32_t used;
    uint32_t max_capacity;
    uint32_t last_error;
    uint32_t lsb_mode;
} gui_screenram_info_t;

/* Screen RAM stores bytes in a reserved framebuffer/backbuffer rectangle. */
int gui_screenram_enable(int on);
int gui_screenram_set_corner(const char* corner, uint32_t w, uint32_t h);
int gui_screenram_set_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int gui_screenram_write(uint32_t offset, const uint8_t* data, uint32_t len);
int gui_screenram_read(uint32_t offset, uint8_t* data, uint32_t len);
void gui_screenram_clear(void);
void gui_screenram_info(gui_screenram_info_t* out);
int gui_screenram_selftest(void);

// Lafillo tab: set extracted + raw content (for View Source)
void gui_lafillo_set_content(const char* extracted, const char* raw);

// LARSH: load and play .larsh file (switches to Gallery tab)
void gui_larsh_play(const char* path);

// Global keyboard shortcut: Fn+0 on many keyboards reports as F10.
void gui_activate_ring0_shortcut(void);

// GUI syscalls (for user programs via LDLL)
void gui_syscall_put_pixel(uint16_t x, uint16_t y, uint32_t argb);
void gui_syscall_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t argb);
void gui_syscall_draw_text(uint16_t x, uint16_t y, const char* s, uint32_t fg, uint32_t bg);
void gui_syscall_clear(uint32_t argb);
uint16_t gui_syscall_get_width(void);
uint16_t gui_syscall_get_height(void);
