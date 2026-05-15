#pragma once

#include <stdint.h>

#define SYSRXE_MAX_APPS 8
#define SYSRXE_APP_BASE 10
#define SYSRXE_TYPE_TEXT 0
#define SYSRXE_TYPE_GAME 1
#define SYSRXE_GAME_MAX_W 24
#define SYSRXE_GAME_MAX_H 12
#define SYSRXE_UI_MAX_WIDGETS 32
#define SYSRXE_UI_TEXT_MAX 64
#define SYSRXE_UI_ACTION_MAX 96
#define SYSRXE_UI_PANEL 1
#define SYSRXE_UI_LABEL 2
#define SYSRXE_UI_BUTTON 3
#define SYSRXE_UI_INPUT 4
#define SYSRXE_UI_OUTPUT 5
#define SYSRXE_UI_STATUS 6
#define SYSRXE_UI_LIST 7
#define SYSRXE_UI_TOGGLE 8
#define SYSRXE_UI_SLIDER 9
#define SYSRXE_UI_PROGRESS 10
#define SYSRXE_UI_SEPARATOR 11
#define SYSRXE_UI_BADGE 12
#define SYSRXE_UI_ICON 13
#define SYSRXE_UI_TILE 14
#define SYSRXE_UI_CUSTOM 15
#define SYSRXE_CODE_MAX 1536
#define SYSRXE_LANG_LSH 0
#define SYSRXE_LANG_LIL 1
#define SYSRXE_LANG_GASM 2
#define SYSRXE_LANG_BOSL 3
#define SYSRXE_LANG_LAFILLO 4
#define SYSRXE_LANG_OSVM 5
#define SYSRXE_LANG_C 6
#define SYSRXE_LANG_LML 7

typedef struct sysrxe_widget {
    int used;
    int kind;
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
    char style[16];
    char text[SYSRXE_UI_TEXT_MAX];
    char action[SYSRXE_UI_ACTION_MAX];
} sysrxe_widget_t;

typedef struct sysrxe_app {
    int used;
    int type;
    char file[32];
    char id[24];
    char name[24];
    char icon[4];
    char layout[16];
    uint32_t color;
    char input_label[24];
    char button_label[24];
    char body[1024];
    char command[128];
    int lang;
    char code[SYSRXE_CODE_MAX];
    int show_desktop;
    int show_dock;
    uint32_t ui_count;
    sysrxe_widget_t ui[SYSRXE_UI_MAX_WIDGETS];
    char game_kind[16];
    uint32_t game_w;
    uint32_t game_h;
    uint32_t game_rows;
    char game_map[SYSRXE_GAME_MAX_H][SYSRXE_GAME_MAX_W + 1];
    int game_start_x;
    int game_start_y;
    int game_px;
    int game_py;
    int game_goal_x;
    int game_goal_y;
    uint32_t game_moves;
    uint32_t game_wins;
    int game_won;
} sysrxe_app_t;

void sysrxe_reset(void);
uint32_t sysrxe_reload(void);
uint32_t sysrxe_count(void);
const sysrxe_app_t* sysrxe_get(uint32_t index);
const sysrxe_app_t* sysrxe_get_by_app(int app);
int sysrxe_is_app(int app);
int sysrxe_app_id(uint32_t index);
int sysrxe_index_from_app(int app);
int sysrxe_load_file(const char* name);
int sysrxe_format_home(int app, char* out, uint32_t out_cap);
int sysrxe_run(int app, const char* input, char* out, uint32_t out_cap);
int sysrxe_apply_appkit(int app, const char* script);
int sysrxe_is_game(int app);
int sysrxe_selftest(void);
