#include "post.h"

#include "awake.h"
#include "drfl.h"
#include "bootprof.h"
#include "cpumode.h"
#include "crashlog.h"
#include "exgui.h"
#include "exexgui.h"
#include "fs.h"
#include "gui.h"
#include "img_glyph.h"
#include "lar.h"
#include "lard_doc.h"
#include "lardkit.h"
#include "lardtime.h"
#include "lcontainer.h"
#include "lil.h"
#include "lguilib.h"
#include "lassist.h"
#include "lpack.h"
#include "lsh.h"
#include "lvcs.h"
#include "mem.h"
#include "oslink.h"
#include "pci.h"
#include "screencheck.h"
#include "string.h"
#include "syscall.h"
#include "taskprio.h"
#include "version.h"

#include <stddef.h>
#include <stdint.h>

static void post_check(const char* name, int ok, lard_post_emit_fn emit, void* user,
                       uint32_t* pass, uint32_t* fail)
{
    if (emit) emit(ok ? "PASS" : "FAIL", name, user);
    lardkit_post_baseline_observe(name, ok);
    if (ok) (*pass)++;
    else (*fail)++;
}

static int post_heap_pattern(void)
{
    uint8_t* p = (uint8_t*)kmalloc(96);
    if (!p) return 0;
    for (uint32_t i = 0; i < 96u; i++) p[i] = (uint8_t)(0xA5u ^ i);
    for (uint32_t i = 0; i < 96u; i++) {
        if (p[i] != (uint8_t)(0xA5u ^ i)) {
            kfree(p);
            return 0;
        }
    }
    kfree(p);
    return 1;
}

static int post_doc_parse(const char* name)
{
    const FsFile* f = fs_open(name);
    char out[256];
    if (!f || !f->data || f->size == 0) return 0;
    return lard_doc_to_text((const char*)f->data, f->size, out, sizeof(out)) == 0 && out[0] != '\0';
}

static int post_version_suffix_known(void)
{
    uint32_t i = 0;
    while (LARDOS_VERSION[i]) i++;
    if (i == 0) return 0;
    char suffix = LARDOS_VERSION[i - 1u];
    return suffix == 'a' || suffix == 'b' || suffix == 'p';
}

void lard_post_run(lard_post_emit_fn emit, void* user, lard_post_result_t* out)
{
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t available = 0;
    uint32_t dirty = 0;
    uint32_t lba = 0;
    uint32_t sectors = 0;
    uint32_t generation = 0;
    uint32_t bank_sectors = 0;
    int last = 0;
    const char* driver = NULL;
    const FsFile* bundle = fs_open("bundle.lar");
    const uint8_t hash_data[3] = { 'a', 'b', 'c' };
    int64_t lil_value = 0;
    gui_post_info_t gui_info;
    int gui_ok;
    pci_addr_t pci_addr;

    lardkit_post_baseline_begin();
    fs_persist_info(&available, &dirty, &last, &driver, &lba, &sectors);
    fs_persist_detail(NULL, &generation, &bank_sectors);
    gui_ok = (gui_post_check(&gui_info) == 0);

    post_check("cpu: 64-bit long mode", sizeof(void*) == 8, emit, user, &pass, &fail);
    post_check("cpu: mode bridge ready", cpu_mode_bridge_ready(), emit, user, &pass, &fail);
    post_check("cpu: real/long roundtrip", cpu_mode_roundtrip_probe() == 0, emit, user, &pass, &fail);
    post_check("mem: heap allocator pattern", post_heap_pattern(), emit, user, &pass, &fail);
    post_check("mem: heap free counter", mem_bytes_free() > 1024u * 1024u, emit, user, &pass, &fail);

    post_check("fs: hello.txt", fs_open("hello.txt") != NULL, emit, user, &pass, &fail);
    post_check("fs: lardos.lars", fs_open("lardos.lars") != NULL, emit, user, &pass, &fail);
    post_check("fs: lardd guide", fs_open("lardd_guide.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: lardtime guide", fs_open("lardtime_guide.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: releases", fs_open("releases.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: features.lil", fs_open("features.lil") != NULL, emit, user, &pass, &fail);
    post_check("fs: default.lguilib", fs_open("default.lguilib") != NULL, emit, user, &pass, &fail);
    post_check("fs: default.ltheme", fs_open("default.ltheme") != NULL, emit, user, &pass, &fail);
    post_check("fs: notes writable", fs_open_writable("notes.txt") != NULL, emit, user, &pass, &fail);
    post_check("fs: bugreport writable", fs_open_writable("bugreport.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: bugreplay writable", fs_open_writable("bugreplay.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: panic capsule writable", fs_open_writable("paniccapsule.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: lfs doctor writable", fs_open_writable("lfsdoctor.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: trace writable", fs_open_writable("trace.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: netwatch writable", fs_open_writable("netwatch.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: journal writable", fs_open_writable("journal.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: post baseline writable", fs_open_writable("postbaseline.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: boot replay writable", fs_open_writable("bootreplay.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: cfg profile writable", fs_open_writable("cfgprof.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: user law writable", fs_open_writable("userlaw.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: glyph map writable", fs_open_writable("glyphmap.lardd") != NULL, emit, user, &pass, &fail);
    post_check("fs: writable directory index", fs_writable_count() >= 19u, emit, user, &pass, &fail);
    post_check("fs: lunit tests", fs_open("tests.lunit") != NULL, emit, user, &pass, &fail);

    post_check("doc: LARS renderer", post_doc_parse("lardos.lars"), emit, user, &pass, &fail);
    post_check("doc: LARDD renderer", post_doc_parse("lardd_guide.lardd"), emit, user, &pass, &fail);
    post_check("doc: LARS form actions", lard_doc_selftest() == 0, emit, user, &pass, &fail);
    post_check("lpack: package parser", lpack_selftest() == 0, emit, user, &pass, &fail);
    post_check("lguilib: gui library parser", lguilib_selftest() == 0, emit, user, &pass, &fail);
    post_check("imgglyph: user-editable Unicode slots", img_glyph_selftest() == 0, emit, user, &pass, &fail);
    post_check("lar: bundle directory", bundle && lar_list(bundle->data, bundle->size, NULL, NULL) == 0, emit, user, &pass, &fail);
    post_check("drfl: descriptors", drfl_list(NULL, NULL) >= 2u, emit, user, &pass, &fail);
    post_check("version: suffix policy", post_version_suffix_known(), emit, user, &pass, &fail);
    post_check("time: lardos time calendar", lardtime_selftest() == 0, emit, user, &pass, &fail);
    post_check("pci: rtl8139 option", pci_find_device(0x10ECu, 0x8139u, &pci_addr) == 0, emit, user, &pass, &fail);
    post_check("pci: piix3 ide option", pci_find_device(0x8086u, 0x7010u, &pci_addr) == 0, emit, user, &pass, &fail);

    post_check("gui: framebuffer live", gui_ok && gui_info.width >= 320u && gui_info.height >= 200u, emit, user, &pass, &fail);
    post_check("gui: visible content", gui_ok && gui_info.changed_samples > 8u, emit, user, &pass, &fail);
    post_check("gui: window bounds", gui_ok && gui_info.window_inside, emit, user, &pass, &fail);
    post_check("gui: response view layout", gui_ok && gui_info.response_view_ok, emit, user, &pass, &fail);
    post_check("gui: overlay chrome layout", gui_ok && gui_info.chrome_ok, emit, user, &pass, &fail);
    post_check("gui: screenram scratch", gui_screenram_selftest() == 0, emit, user, &pass, &fail);
    post_check("gui: retro screencheck", screencheck_selftest() == 0, emit, user, &pass, &fail);
    post_check("gui: clickable image glyphs", gui_img_glyph_interaction_selftest() == 0, emit, user, &pass, &fail);
    post_check("gui: unicode cursor slot", gui_unicode_cursor_selftest() == 0, emit, user, &pass, &fail);
    post_check("kit: user feature suite", lardkit_selftest() == 0, emit, user, &pass, &fail);
    post_check("gui: exgui desktop layer", exgui_selftest() == 0, emit, user, &pass, &fail);
    post_check("gui: exexgui split layout", exexgui_selftest() == 0, emit, user, &pass, &fail);
    post_check("gui: lard buddy assistant", lassist_selftest() == 0, emit, user, &pass, &fail);

    post_check("lcnt: defaults", lcontainer_count() >= 3u, emit, user, &pass, &fail);
    post_check("lpst: dual-bank layout", lba == 2752u && sectors == 128u && bank_sectors == 64u, emit, user, &pass, &fail);
    post_check("lpst: driver string", driver && driver[0], emit, user, &pass, &fail);
    post_check("lvcs: hash engine", lvcs_hash(hash_data, sizeof(hash_data)) != 0, emit, user, &pass, &fail);
    post_check("oslink: packet and local bus", oslink_selftest() == 0, emit, user, &pass, &fail);
    post_check("taskprio: priority queue", taskprio_selftest() == 0, emit, user, &pass, &fail);
    post_check("lsh: settings shell grammar", lsh_cfgsh_selftest() == 0, emit, user, &pass, &fail);
    post_check("bootprof: profile flags", bootprof_selftest() == 0, emit, user, &pass, &fail);
    post_check("awake: background boot tracker", awake_selftest() == 0, emit, user, &pass, &fail);
    post_check("crashlog: writable log", crashlog_selftest() == 0, emit, user, &pass, &fail);
    post_check("lcnt: dev profile", (lcontainer_profile_caps("dev") & (SYSCALL_CAP_FS | SYSCALL_CAP_LDLL)) == (SYSCALL_CAP_FS | SYSCALL_CAP_LDLL), emit, user, &pass, &fail);
    post_check("lil: feature forms", lil_eval_int("(begin (assert (eq (pow 2 8) 256)) (assert (eq (clamp 99 0 10) 10)) (gcd 84 30))", &lil_value) == 0 && lil_value == 6, emit, user, &pass, &fail);

    lardkit_post_baseline_finish();
    if (out) {
        out->pass = pass;
        out->fail = fail;
        out->storage_available = available;
        out->storage_dirty = dirty;
        out->storage_last_result = last;
        out->storage_generation = generation;
    }
}
