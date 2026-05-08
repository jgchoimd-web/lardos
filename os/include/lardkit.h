#pragma once

#include "awake.h"
#include "screencheck.h"

#include <stdint.h>

#define LARDKIT_NAME_MAX 31u
#define LARDKIT_TEXT_MAX 159u
#define LARDKIT_BUGREPLAY_MAX 8u
#define LARDKIT_TRUST_HISTORY_MAX 16u

#define LARDKIT_TRUST_FS       0x01u
#define LARDKIT_TRUST_SCREEN   0x02u
#define LARDKIT_TRUST_NET      0x04u
#define LARDKIT_TRUST_OSLINK   0x08u
#define LARDKIT_TRUST_RAW      0x10u

typedef struct {
    uint32_t enabled;
    uint32_t scans;
    uint32_t bug_count;
    uint32_t last_error;
    screencheck_info_t screen;
} lardkit_bugeye_info_t;

typedef struct {
    uint32_t valid;
    uint32_t snapshots;
    uint32_t applied;
    char label[LARDKIT_NAME_MAX + 1u];
    uint32_t exgui_enabled;
    uint32_t exgui_style;
    uint32_t exgui_layout;
    uint32_t exexgui_enabled;
    uint32_t exexgui_focus;
    uint32_t buddy_enabled;
    uint32_t http_post;
    int32_t task_default;
    char boot_profile[LARDKIT_NAME_MAX + 1u];
    uint32_t awake_enabled;
    uint32_t theme;
} lardkit_rollback_info_t;

typedef struct {
    char subject[LARDKIT_NAME_MAX + 1u];
    uint32_t caps;
} lardkit_trust_entry_t;

typedef struct {
    uint32_t seq;
    uint32_t scan;
    uint32_t width;
    uint32_t height;
    uint32_t changed_samples;
    uint32_t bad_tiles;
    uint32_t last_error;
} lardkit_bugreplay_frame_t;

typedef struct {
    uint32_t seq;
    char subject[LARDKIT_NAME_MAX + 1u];
    uint32_t cap;
    uint32_t allowed;
    uint32_t caps_after;
} lardkit_trust_history_entry_t;

typedef struct {
    uint32_t count;
    uint32_t dirty;
    uint32_t available;
    uint32_t generation;
    uint32_t last_error;
} lardkit_oldcheck_info_t;

typedef struct {
    char name[LARDKIT_NAME_MAX + 1u];
    uint32_t fg;
    uint32_t bg;
    uint32_t accent;
    uint32_t style_hint;
} lardkit_theme_info_t;

typedef struct {
    uint32_t has_record;
    uint32_t executed;
    char input[LARDKIT_NAME_MAX + 1u];
    char predicted[LARDKIT_NAME_MAX + 1u];
    char reason[LARDKIT_TEXT_MAX + 1u];
} lardkit_magic_info_t;

typedef struct {
    uint32_t phase_count;
    uint32_t current_phase;
    uint32_t done;
    uint32_t percent;
    char current[LARDKIT_NAME_MAX + 1u];
} lardkit_awakemon_info_t;

typedef struct {
    uint32_t opened;
    uint32_t size;
    uint32_t last_error;
    char path[LARDKIT_NAME_MAX + 1u];
} lardkit_larsview_info_t;

typedef struct {
    uint32_t files;
    uint32_t storage_available;
    uint32_t dirty;
    uint32_t generation;
    int32_t last_result;
    uint32_t repairs;
    int32_t last_repair;
} lardkit_lfsdoctor_info_t;

void lardkit_init(void);

void lardkit_bugeye_enable(int on);
int lardkit_bugeye_scan(void);
void lardkit_bugeye_info(lardkit_bugeye_info_t* out);
int lardkit_bugeye_write_report(void);
uint32_t lardkit_bugreplay_count(void);
int lardkit_bugreplay_at(uint32_t idx, lardkit_bugreplay_frame_t* out);
int lardkit_bugreplay_write(void);
void lardkit_bugreplay_clear(void);

int lardkit_snapshot(const char* label);
int lardkit_rollback_apply(void);
void lardkit_rollback_info(lardkit_rollback_info_t* out);

uint32_t lardkit_trust_count(void);
int lardkit_trust_at(uint32_t idx, lardkit_trust_entry_t* out);
int lardkit_trust_set(const char* subject, uint32_t cap, int allow);
uint32_t lardkit_trust_caps(const char* subject);
uint32_t lardkit_trust_history_count(void);
int lardkit_trust_history_at(uint32_t idx, lardkit_trust_history_entry_t* out);
void lardkit_trust_history_clear(void);

uint32_t lardkit_bootmap_count(void);
const char* lardkit_bootmap_phase(uint32_t idx);

void lardkit_panicroom_enter(void);
void lardkit_panicroom_exit(void);
uint32_t lardkit_panicroom_active(void);
uint32_t lardkit_panicroom_entries(void);

int lardkit_oldcheck_run(int draw);
void lardkit_oldcheck_info(lardkit_oldcheck_info_t* out);

uint32_t lardkit_theme_count(void);
int lardkit_theme_at(uint32_t idx, lardkit_theme_info_t* out);
int lardkit_theme_use(const char* name);
int lardkit_theme_parse(const uint8_t* data, uint32_t len, lardkit_theme_info_t* out);
int lardkit_theme_use_data(const uint8_t* data, uint32_t len);
void lardkit_theme_info(lardkit_theme_info_t* out);

void lardkit_magic_record(const char* input, const char* predicted, int executed, const char* reason);
void lardkit_magic_info(lardkit_magic_info_t* out);

void lardkit_awakemon_info(lardkit_awakemon_info_t* out);

int lardkit_larsview_open(const char* path);
void lardkit_larsview_info(lardkit_larsview_info_t* out);

int lardkit_notes_reset(void);
int lardkit_notes_append(const char* text);

int lardkit_panic_capsule_write(void);
int lardkit_lfsdoctor_scan(int repair);
void lardkit_lfsdoctor_info(lardkit_lfsdoctor_info_t* out);

int lardkit_selftest(void);
