#pragma once

#include "awake.h"
#include "screencheck.h"

#include <stdint.h>

#define LARDKIT_NAME_MAX 31u
#define LARDKIT_TEXT_MAX 159u
#define LARDKIT_BUGREPLAY_MAX 8u
#define LARDKIT_TRUST_HISTORY_MAX 16u
#define LARDKIT_TRACE_MAX 32u
#define LARDKIT_NETWATCH_MAX 16u
#define LARDKIT_POST_BASELINE_MAX 64u
#define LARDKIT_CFGPROF_MAX 4u

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
    uint32_t seq;
    uint32_t tick;
    char module[LARDKIT_NAME_MAX + 1u];
    char text[LARDKIT_TEXT_MAX + 1u];
    int32_t value;
} lardkit_trace_entry_t;

typedef struct {
    uint32_t enabled;
    uint32_t count;
    uint32_t next_seq;
} lardkit_trace_info_t;

typedef struct {
    uint32_t seq;
    char kind[LARDKIT_NAME_MAX + 1u];
    char detail[LARDKIT_TEXT_MAX + 1u];
    int32_t value;
} lardkit_netwatch_entry_t;

typedef struct {
    uint32_t enabled;
    uint32_t count;
    uint32_t sent;
    uint32_t received;
    uint32_t http;
    uint32_t oslink;
} lardkit_netwatch_info_t;

typedef struct {
    uint32_t has_previous;
    uint32_t previous_count;
    uint32_t current_count;
    uint32_t changes;
    uint32_t regressions;
} lardkit_post_baseline_info_t;

typedef struct {
    uint32_t valid;
    char name[LARDKIT_NAME_MAX + 1u];
    uint32_t buddy_enabled;
    uint32_t http_post;
    int32_t task_default;
    char boot_profile[LARDKIT_NAME_MAX + 1u];
    uint32_t awake_enabled;
    uint32_t theme;
} lardkit_cfg_profile_t;

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
    uint32_t back_count;
    char path[LARDKIT_NAME_MAX + 1u];
    char previous_path[LARDKIT_NAME_MAX + 1u];
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
int lardkit_bugreplay_draw(void);
void lardkit_bugreplay_clear(void);

void lardkit_trace_enable(int on);
void lardkit_trace_event(const char* module, const char* text, int32_t value);
void lardkit_trace_info(lardkit_trace_info_t* out);
uint32_t lardkit_trace_count(void);
int lardkit_trace_at(uint32_t idx, lardkit_trace_entry_t* out);
void lardkit_trace_clear(void);
int lardkit_trace_write(void);

void lardkit_netwatch_enable(int on);
void lardkit_netwatch_record(const char* kind, const char* detail, int32_t value);
void lardkit_netwatch_info(lardkit_netwatch_info_t* out);
uint32_t lardkit_netwatch_count(void);
int lardkit_netwatch_at(uint32_t idx, lardkit_netwatch_entry_t* out);
void lardkit_netwatch_clear(void);
int lardkit_netwatch_write(void);

void lardkit_journal_event(const char* area, const char* text);
int lardkit_journal_clear(void);

void lardkit_post_baseline_begin(void);
void lardkit_post_baseline_observe(const char* name, int ok);
void lardkit_post_baseline_finish(void);
void lardkit_post_baseline_info(lardkit_post_baseline_info_t* out);

int lardkit_bootreplay_write(void);

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
int lardkit_theme_preview_draw(const lardkit_theme_info_t* theme);

int lardkit_cfgprof_save(const char* name);
int lardkit_cfgprof_load(const char* name);
uint32_t lardkit_cfgprof_count(void);
int lardkit_cfgprof_at(uint32_t idx, lardkit_cfg_profile_t* out);
int lardkit_cfgprof_write(void);

void lardkit_magic_record(const char* input, const char* predicted, int executed, const char* reason);
void lardkit_magic_info(lardkit_magic_info_t* out);

void lardkit_awakemon_info(lardkit_awakemon_info_t* out);

int lardkit_larsview_open(const char* path);
int lardkit_larsview_back(void);
void lardkit_larsview_info(lardkit_larsview_info_t* out);

int lardkit_notes_reset(void);
int lardkit_notes_append(const char* text);

int lardkit_panic_capsule_write(void);
int lardkit_lfsdoctor_scan(int repair);
void lardkit_lfsdoctor_info(lardkit_lfsdoctor_info_t* out);

int lardkit_userlaw_reset(void);
int lardkit_userlaw_check(void);

int lardkit_selftest(void);
