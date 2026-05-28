#include "auxkernel.h"

#include "crashlog.h"
#include "cpumode.h"
#include "fs.h"
#include "lardsec.h"
#include "mediafs.h"

#include <stdint.h>

enum {
    AUXK_ACTION_NONE = 0,
    AUXK_ACTION_PANICROOM = 1,
    AUXK_ACTION_LOCKDOWN = 2,
    AUXK_ACTION_KEY_DISCARD = 3,
    AUXK_ACTION_REPORT = 4
};

static auxkernel_info_t s_aux;

static void aux_copy(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "no reason";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t aux_hash_reason(const char* s)
{
    uint32_t h = 2166136261u;
    if (!s) s = "";
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h ? h : 0xA11CE11u;
}

static void aux_record(uint32_t action, const char* reason, int32_t result)
{
    s_aux.active = action != AUXK_ACTION_NONE ? 1u : s_aux.active;
    s_aux.last_action = action;
    s_aux.last_result = result;
    aux_copy(s_aux.last_reason, sizeof(s_aux.last_reason), reason);
}

static void aux_append(char* out, uint32_t cap, uint32_t* pos, const char* s)
{
    if (!out || !pos || !s || *pos >= cap) return;
    while (*s && *pos + 1u < cap) out[(*pos)++] = *s++;
    out[*pos] = '\0';
}

static void aux_append_u32(char* out, uint32_t cap, uint32_t* pos, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    if (v == 0u) {
        aux_append(out, cap, pos, "0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0u) {
        char c[2];
        c[0] = tmp[--n];
        c[1] = '\0';
        aux_append(out, cap, pos, c);
    }
}

static int aux_sync_media(void)
{
    int result = 0;
    uint32_t count = mediafs_count();
    for (uint32_t i = 0; i < count; i++) {
        mediafs_info_t info;
        if (mediafs_info(i, &info) != 0) continue;
        s_aux.media_sync_attempts++;
        if (mediafs_sync(info.drive) != 0) {
            s_aux.media_sync_failures++;
            result = -1;
        }
    }
    return result;
}

void auxkernel_init(void)
{
    s_aux.initialized = 1;
    s_aux.active = 0;
    s_aux.module_independent = 1;
    s_aux.real8_profile = 1;
    s_aux.real8_bridge_ready = cpu_mode_bridge_ready() ? 1u : 0u;
    s_aux.real8_probe_count = 0;
    s_aux.last_real8_ok = 0;
    s_aux.last_real8_marker = 0;
    s_aux.panicroom_entries = 0;
    s_aux.lockdowns = 0;
    s_aux.key_discards = 0;
    s_aux.reports = 0;
    s_aux.media_sync_attempts = 0;
    s_aux.media_sync_failures = 0;
    s_aux.last_action = AUXK_ACTION_NONE;
    s_aux.last_result = 0;
    aux_copy(s_aux.last_reason, sizeof(s_aux.last_reason), "standby");
}

void auxkernel_enter_panicroom(const char* reason)
{
    (void)auxkernel_real8_probe();
    s_aux.panicroom_entries++;
    aux_record(AUXK_ACTION_PANICROOM, reason ? reason : "panicroom", 0);
}

int auxkernel_real8_probe(void)
{
    cpu_mode_info_t mode;
    int r;
    if (!s_aux.initialized) auxkernel_init();
    r = cpu_mode_auxkernel_real8_probe();
    cpu_mode_info(&mode);
    s_aux.real8_bridge_ready = mode.bridge_ready ? 1u : 0u;
    s_aux.real8_probe_count = mode.auxkernel_real8_count;
    s_aux.last_real8_ok = (r == 0 && mode.last_auxkernel_real8_ok) ? 1u : 0u;
    s_aux.last_real8_marker = mode.last_auxkernel_real8_marker;
    return s_aux.last_real8_ok ? 0 : -1;
}

int auxkernel_lockdown(const char* reason)
{
    int result;
    if (!s_aux.initialized) auxkernel_init();
    (void)auxkernel_real8_probe();
    lardsec_enable(1);
    result = aux_sync_media();
    if (lardsec_lock() != 0) result = -2;
    s_aux.lockdowns++;
    aux_record(AUXK_ACTION_LOCKDOWN, reason ? reason : "emergency lockdown", result);
    crashlog_record("auxkernel", result == 0 ? "emergency lockdown" : "emergency lockdown partial");
    (void)auxkernel_report();
    return result;
}

int auxkernel_discard_keys(const char* reason)
{
    int result;
    if (!s_aux.initialized) auxkernel_init();
    result = auxkernel_lockdown(reason ? reason : "emergency key discard");
    if (lardsec_emergency_forget_key(aux_hash_reason(reason)) != 0) result = -3;
    s_aux.key_discards++;
    aux_record(AUXK_ACTION_KEY_DISCARD, reason ? reason : "emergency key discard", result);
    crashlog_record("auxkernel", "volatile media key discarded");
    (void)auxkernel_report();
    return result;
}

int auxkernel_report(void)
{
    char buf[1024];
    uint32_t pos = 0;
    FsWritableFile* w = fs_open_writable("auxkernel.lardd");
    if (!w) return -1;

    aux_append(buf, sizeof(buf), &pos, "LARDD 1\nTITLE AuxKernel Emergency Microkernel\n");
    aux_append(buf, sizeof(buf), &pos, "TEXT Small built-in emergency kernel path. It does not need KMO modules.\n");
    aux_append(buf, sizeof(buf), &pos, "TEXT REAL8 means an 8-bit byte-discipline first responder running inside BIOS real mode.\n");
    aux_append(buf, sizeof(buf), &pos, "TEXT Hardware-damaging self-destruct is not implemented; containment uses lock, sync, and key discard.\n");
    aux_append(buf, sizeof(buf), &pos, "SECTION State\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM initialized "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.initialized); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM active "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.active); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM module_independent "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.module_independent); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM real8_profile "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.real8_profile); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM real8_bridge_ready "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.real8_bridge_ready); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM real8_probe_count "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.real8_probe_count); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM last_real8_ok "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.last_real8_ok); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM last_real8_marker "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.last_real8_marker); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM panicroom_entries "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.panicroom_entries); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM lockdowns "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.lockdowns); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM key_discards "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.key_discards); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM media_sync_attempts "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.media_sync_attempts); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM media_sync_failures "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.media_sync_failures); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM last_action "); aux_append_u32(buf, sizeof(buf), &pos, s_aux.last_action); aux_append(buf, sizeof(buf), &pos, "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM last_result "); aux_append_u32(buf, sizeof(buf), &pos, (uint32_t)(s_aux.last_result < 0 ? -s_aux.last_result : s_aux.last_result)); aux_append(buf, sizeof(buf), &pos, s_aux.last_result < 0 ? " negative\n" : "\n");
    aux_append(buf, sizeof(buf), &pos, "ITEM last_reason "); aux_append(buf, sizeof(buf), &pos, s_aux.last_reason); aux_append(buf, sizeof(buf), &pos, "\nEND\n");

    if (fs_write(w, 0, (const uint8_t*)buf, pos) != pos) return -2;
    s_aux.reports++;
    return 0;
}

void auxkernel_info(auxkernel_info_t* out)
{
    if (!out) return;
    *out = s_aux;
}

int auxkernel_selftest(void)
{
    auxkernel_info_t before;
    auxkernel_info_t after;
    auxkernel_info(&before);
    if (!before.initialized) return -1;
    if (!before.module_independent) return -2;
    if (!before.real8_profile) return -3;
    if (!fs_open_writable("auxkernel.lardd")) return -4;
    if (auxkernel_real8_probe() != 0) return -5;
    auxkernel_info(&after);
    if (after.lockdowns != before.lockdowns || after.key_discards != before.key_discards ||
        after.panicroom_entries != before.panicroom_entries || after.reports != before.reports) {
        return -6;
    }
    if (!after.last_real8_ok || after.real8_probe_count < before.real8_probe_count + 1u) return -7;
    return 0;
}
