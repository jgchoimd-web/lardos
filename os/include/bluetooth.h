#pragma once

#include <stdint.h>

#define LBT_ADDR_MAX 17u
#define LBT_NAME_MAX 31u
#define LBT_EVENT_MAX 79u
#define LBT_DEVICE_MAX 8u
#define LBT_LOG_MAX 16u

#define LBT_FLAG_HID      0x00000001u
#define LBT_FLAG_AUDIO    0x00000002u
#define LBT_FLAG_SERIAL   0x00000004u
#define LBT_FLAG_FILE     0x00000008u
#define LBT_FLAG_UNKNOWN  0x80000000u

typedef struct {
    char addr[LBT_ADDR_MAX + 1u];
    char name[LBT_NAME_MAX + 1u];
    uint32_t flags;
    int32_t rssi;
    uint32_t seen;
    uint32_t paired;
    uint32_t trusted;
} lbt_device_t;

typedef struct {
    uint32_t seq;
    char action[15u + 1u];
    char addr[LBT_ADDR_MAX + 1u];
    char detail[LBT_EVENT_MAX + 1u];
} lbt_event_t;

typedef struct {
    uint32_t enabled;
    uint32_t controller_present;
    uint32_t discoverable;
    uint32_t scanning;
    uint32_t hid_enabled;
    uint32_t device_count;
    uint32_t paired_count;
    uint32_t trusted_count;
    uint32_t scan_count;
    uint32_t sent;
    uint32_t received;
    uint32_t log_count;
    uint32_t last_error;
    char controller[LBT_NAME_MAX + 1u];
} lbt_info_t;

void lbt_init(void);
void lbt_controller_attach(const char* name);
void lbt_controller_detach(void);
int lbt_enable(int on);
int lbt_set_discoverable(int on);
int lbt_set_hid(int on);
int lbt_scan(void);
int lbt_add_manual(const char* addr, const char* name, uint32_t flags);
int lbt_pair(const char* addr, const char* pin);
int lbt_unpair(const char* addr);
int lbt_trust(const char* addr, int on);
int lbt_send(const char* addr, const char* text, int force);
int lbt_receive_note(const char* addr, const char* text);
uint32_t lbt_device_count(void);
int lbt_device_at(uint32_t idx, lbt_device_t* out);
uint32_t lbt_log_count(void);
int lbt_log_at(uint32_t idx, lbt_event_t* out);
void lbt_info(lbt_info_t* out);
int lbt_write_report(void);
int lbt_selftest(void);
uint32_t lbt_flags_from_name(const char* name);
void lbt_flags_list(uint32_t flags, char* out, uint32_t cap);
