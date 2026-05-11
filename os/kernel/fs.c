#include "fs.h"
#include "lfs.h"
#include "storage.h"
#include "fs_ldll.inc"
#include "lafillo_demo_data.inc"
#include "releases_lardd.inc"
#include <stddef.h>

/* Hybrid: built-in table + LFS volume + writable RAM files. */

/* LFS volume: "lfs_info.txt" = "LFS (Lard File System) - custom format.\n" */
static const uint8_t lfs_volume[73] = {
    'L','F','S',0, 1, 0,0,0,  /* magic, version */
    1, 0,                      /* file_count = 1 */
    12, 'l','f','s','_','i','n','f','o','.','t','x','t',  /* name_len, name */
    0x1F,0x00,0x00,0x00,       /* offset = 31 */
    0x2A,0x00,0x00,0x00,       /* size = 42 */
    /* data at offset 31 */
    'L','F','S',' ','(','L','a','r','d',' ','F','i','l','e',' ','S','y','s','t','e','m',')',
    ' ','-',' ','c','u','s','t','o','m',' ','f','o','r','m','a','t','.','\n'
};

/* rtl8139.drfl - DRFL entry for RTL8139 (vendor 0x10EC, device 0x8139) */
static const uint8_t file_rtl8139_drfl[23] = {
    'D', 'R', 'F', 'L', 1, 0, 0, 0,   /* magic, version, reserved */
    1, 0,                              /* entry_count = 1 */
    0xEC, 0x10, 0x39, 0x81,            /* vendor 0x10EC, device 0x8139 */
    0, 7,                              /* type=net, name_len=7 */
    'r', 't', 'l', '8', '1', '3', '9'
};

/* piix3ide.drfl - DRFL entry for Intel PIIX3 IDE (vendor 0x8086, device 0x7010) */
static const uint8_t file_piix3ide_drfl[23] = {
    'D', 'R', 'F', 'L', 1, 0, 0, 0,    /* magic, version, reserved */
    1, 0,                              /* entry_count = 1 */
    0x86, 0x80, 0x10, 0x70,            /* vendor 0x8086, device 0x7010 */
    1, 7,                              /* type=block, name_len=7 */
    'a', 't', 'a', '-', 'p', 'i', 'o'
};

#define RAM_FILE_CAP 8192u
/* Initial Notes: "Image: " + U+E000 (UTF-8 EE 80 80) - view Gallery sample.bmp first to assign */
static const uint8_t notes_init[] = "Image: \xEE\x80\x80\n";
static uint8_t ram_notes_buf[RAM_FILE_CAP];
static FsWritableFile ram_notes = { "notes.txt", ram_notes_buf, 0, RAM_FILE_CAP };

#define LARDD_NOTES_CAP 2048u
static const uint8_t lardd_notes_init[] = "LARDD 1\nTITLE LardOS Notes\n";
static uint8_t ram_lardd_notes_buf[LARDD_NOTES_CAP];
static FsWritableFile ram_lardd_notes = { "notes.lardd", ram_lardd_notes_buf, 0, LARDD_NOTES_CAP };

#define LAFILLO_SAVE_CAP 4096u
static uint8_t ram_lafillo_save_buf[LAFILLO_SAVE_CAP];
static FsWritableFile ram_lafillo_save = { "lafillo_saved.txt", ram_lafillo_save_buf, 0, LAFILLO_SAVE_CAP };

#define LAR_EXTRACT_CAP 2048u
static uint8_t ram_lar_extract_buf[LAR_EXTRACT_CAP];
static FsWritableFile ram_lar_extract = { "lar_extract.txt", ram_lar_extract_buf, 0, LAR_EXTRACT_CAP };

#define VCS_RESTORE_CAP 4096u
static uint8_t ram_vcs_restore_buf[VCS_RESTORE_CAP];
static FsWritableFile ram_vcs_restore = { "vcs_restore.txt", ram_vcs_restore_buf, 0, VCS_RESTORE_CAP };

#define BOOTPROF_CAP 64u
static const uint8_t bootprof_init[] = "normal\n";
static uint8_t ram_bootprof_buf[BOOTPROF_CAP];
static FsWritableFile ram_bootprof = { "bootprof.txt", ram_bootprof_buf, 0, BOOTPROF_CAP };

#define CRASHLOG_CAP 2048u
static const uint8_t crashlog_init[] = "LardOS crashlog\n";
static uint8_t ram_crashlog_buf[CRASHLOG_CAP];
static FsWritableFile ram_crashlog = { "crashlog.txt", ram_crashlog_buf, 0, CRASHLOG_CAP };

#define BUGREPORT_CAP 4096u
static const uint8_t bugreport_init[] =
    "LARDD 1\n"
    "TITLE BugEye Report\n"
    "TEXT No BugEye scan has run yet.\n";
static uint8_t ram_bugreport_buf[BUGREPORT_CAP];
static FsWritableFile ram_bugreport = { "bugreport.lardd", ram_bugreport_buf, 0, BUGREPORT_CAP };

#define BUGREPLAY_CAP 2048u
static const uint8_t bugreplay_init[] =
    "LARDD 1\n"
    "TITLE Bug Replay\n"
    "TEXT No BugEye replay frames yet.\n";
static uint8_t ram_bugreplay_buf[BUGREPLAY_CAP];
static FsWritableFile ram_bugreplay = { "bugreplay.lardd", ram_bugreplay_buf, 0, BUGREPLAY_CAP };

#define PANIC_CAPSULE_CAP 2048u
static const uint8_t panic_capsule_init[] =
    "LARDD 1\n"
    "TITLE Panic Capsule\n"
    "TEXT No capsule has been generated yet.\n";
static uint8_t ram_panic_capsule_buf[PANIC_CAPSULE_CAP];
static FsWritableFile ram_panic_capsule = { "paniccapsule.lardd", ram_panic_capsule_buf, 0, PANIC_CAPSULE_CAP };

#define LFSDOCTOR_CAP 1024u
static const uint8_t lfsdoctor_init[] =
    "LARDD 1\n"
    "TITLE LFS Doctor\n"
    "TEXT No filesystem doctor scan has run yet.\n";
static uint8_t ram_lfsdoctor_buf[LFSDOCTOR_CAP];
static FsWritableFile ram_lfsdoctor = { "lfsdoctor.lardd", ram_lfsdoctor_buf, 0, LFSDOCTOR_CAP };

#define TRACE_CAP 4096u
static const uint8_t trace_init[] =
    "LARDD 1\n"
    "TITLE LardTrace\n"
    "TEXT Trace is off. Use trace on.\n";
static uint8_t ram_trace_buf[TRACE_CAP];
static FsWritableFile ram_trace = { "trace.lardd", ram_trace_buf, 0, TRACE_CAP };

#define NETWATCH_CAP 2048u
static const uint8_t netwatch_init[] =
    "LARDD 1\n"
    "TITLE NetWatch\n"
    "TEXT NetWatch is off. Use netwatch on.\n";
static uint8_t ram_netwatch_buf[NETWATCH_CAP];
static FsWritableFile ram_netwatch = { "netwatch.lardd", ram_netwatch_buf, 0, NETWATCH_CAP };

#define JOURNAL_CAP 4096u
static const uint8_t journal_init[] =
    "LARDD 1\n"
    "TITLE LardOS Journal\n"
    "SECTION Events\n";
static uint8_t ram_journal_buf[JOURNAL_CAP];
static FsWritableFile ram_journal = { "journal.lardd", ram_journal_buf, 0, JOURNAL_CAP };

#define POSTBASELINE_CAP 4096u
static const uint8_t postbaseline_init[] =
    "LARDD 1\n"
    "TITLE POST Baseline\n"
    "TEXT No POST baseline has been stored yet.\n";
static uint8_t ram_postbaseline_buf[POSTBASELINE_CAP];
static FsWritableFile ram_postbaseline = { "postbaseline.lardd", ram_postbaseline_buf, 0, POSTBASELINE_CAP };

#define BOOTREPLAY_CAP 2048u
static const uint8_t bootreplay_init[] =
    "LARDD 1\n"
    "TITLE Boot Replay\n"
    "TEXT No boot replay has been captured yet.\n";
static uint8_t ram_bootreplay_buf[BOOTREPLAY_CAP];
static FsWritableFile ram_bootreplay = { "bootreplay.lardd", ram_bootreplay_buf, 0, BOOTREPLAY_CAP };

#define CFGPROF_CAP 2048u
static const uint8_t cfgprof_init[] =
    "LARDD 1\n"
    "TITLE CFG Profiles\n"
    "TEXT No settings profile has been saved yet.\n";
static uint8_t ram_cfgprof_buf[CFGPROF_CAP];
static FsWritableFile ram_cfgprof = { "cfgprof.lardd", ram_cfgprof_buf, 0, CFGPROF_CAP };

#define USERLAW_CAP 4096u
static const uint8_t userlaw_init[] =
    "LARDD 1\n"
    "TITLE LardOS User Law\n"
    "TEXT LardOS is a user-owned, inspectable, self-hosted-feeling operating system.\n"
    "TEXT The system should give control first, then explain risks in plain local files.\n"
    "SECTION Core Values\n"
    "ITEM User ownership: the user may inspect, change, override, repair, and replace OS behavior.\n"
    "ITEM Visibility: powerful actions, recovery state, boot state, permissions, and automatic choices must be visible.\n"
    "ITEM Local self-reliance: OS features use in-tree C, native file formats, and LardOS languages before outside dependencies.\n"
    "ITEM Explainable automation: magic may execute predicted commands, but magic explain must say why.\n"
    "ITEM Reversibility: settings, packages, and risky changes should have rollback, history, or capsule trails.\n"
    "ITEM Repair over halt: panic room, lfsdoctor, bugeye, post, and bootmap exist so the user can recover.\n"
    "ITEM User-grantable power: the user may grant priority lev.10 and enter SUM/raw control knowingly.\n"
    "ITEM Native expression: LARS, LARDD, LGUILIB, LTHEME, LPACK, LFS, and picture Unicode keep the system's surface its own.\n"
    "ITEM Honest releases: a is official, b is beta-experimental, p is hotpatch; hardware profiles name the target.\n"
    "ITEM Communication: OS modules, processes, and other systems should communicate through visible OSLink paths.\n"
    "SECTION Commands\n"
    "ITEM values -> read this law.\n"
    "ITEM userlaw show -> read this law.\n"
    "ITEM userlaw reset -> restore this law.\n"
    "ITEM trust history, priority history, magic explain, bootreplay show, panic capsule -> audit power after it is used.\n"
    "END\n";
static uint8_t ram_userlaw_buf[USERLAW_CAP];
static FsWritableFile ram_userlaw = { "userlaw.lardd", ram_userlaw_buf, 0, USERLAW_CAP };

#define GLYPHMAP_CAP 16384u
static const uint8_t glyphmap_init[] =
    "LARDD 1\n"
    "TITLE Image Glyph Map\n"
    "TEXT No picture Unicode slots have been assigned yet. Use glyph demo or glyph auto sample.bmp sample, then click them in the GUI.\n"
    "END\n";
static uint8_t ram_glyphmap_buf[GLYPHMAP_CAP];
static FsWritableFile ram_glyphmap = { "glyphmap.lardd", ram_glyphmap_buf, 0, GLYPHMAP_CAP };

#define LPST_MAGIC       0x5453504Cu  /* "LPST" LE */
#define LPST_VERSION     2u
#define LPST_START_LBA   2752u
#define LPST_BANKS       2u
#define LPST_BANK_SECTORS 64u
#define LPST_SECTORS     (LPST_BANKS * LPST_BANK_SECTORS)
#define LPST_BANK_BYTES  (LPST_BANK_SECTORS * STORAGE_SECTOR_SIZE)
#define LPST_HEADER_SIZE 24u
#define LPST_V1_HEADER_SIZE 20u
#define LPST_ENTRY_SIZE  48u

static uint8_t s_lpstore[LPST_BANK_BYTES];
static uint32_t s_fs_dirty;
static int s_persist_last_result = -9;
static uint32_t s_persist_generation;
static uint32_t s_persist_active_bank = 0xFFFFFFFFu;

static const uint8_t file_hello_txt[] =
    "Hello from the in-memory filesystem!\n"
    "This data is embedded in the kernel image.\n";

static const uint8_t file_readme_txt[] =
    "This is a tiny RAM-based filesystem used for experimentation.\n";

static const uint8_t file_lardos_lars[] =
    "LARS 1\n"
    "title LardOS Control Room\n"
    "p A TempleOS-inspired, user-owned system built from C, in-tree tools, native filesystems, and LardOS languages.\n"
    "p LardOS local documents use LARS instead of HTML so the system owns its own document surface.\n"
    "p Core value: the user owns the machine, and the OS must keep power visible, editable, explainable, and recoverable.\n"
    "p A GUI overlay chrome layer now draws clearer titles, safer tabs, button feedback, and output frames above the classic GUI.\n"
    "p LGUILIB files are native GUI library records that theme the overlay without external libraries.\n"
    "p The Doc tab can switch HTTP requests between GET and POST; POST reads the address as URL|body.\n"
    "section Full-control starts\n"
    "li Run control in LSH for the system control map.\n"
    "li Run status to inspect version, storage, drivers, and containers.\n"
    "li Use magic before a command when LSH should predict and execute a mistyped safe command.\n"
    "li Use magic dryrun statsu to see the prediction without executing it.\n"
    "li Use magic -f bye or magic -f byebye only when you explicitly want Magic to force a raw-control command.\n"
    "li Use magic dryrun -f bye to preview a forced raw-control prediction without running it.\n"
    "li Use bye or byebye to sync RAM files and request a firmware/VM poweroff.\n"
    "li Use restart or reboot to sync RAM files and request a firmware/VM restart.\n"
    "li Run mode probe to enter a controlled real16 window and return to long64.\n"
    "li Run mode guard to verify the bridge restores long64 after a real16 window.\n"
    "li Use sram on or sram rect x y w h to turn quiet screen pixels into scratch RAM.\n"
    "li Use oslink status, ping, send, exec, recv, and peers for OS-to-OS messages and safe remote commands.\n"
    "li Use oslink emit channel text for LardOS-internal module messages.\n"
    "li EXGUI and EXEXGUI were removed so the default GUI can become the single polished desktop surface.\n"
    "li Use cfgsh for the settings shell: awake on, ltheme night, http 2, boot 4.\n"
    "li Use buddy on for Lard Buddy, the optional roaming assistant with tips and loose jokes.\n"
    "li Use lguilib show default.lguilib or lguilib use default.lguilib to inspect/apply GUI library themes.\n"
    "li Use time, date, lunar, and dangun for LardOS Time ticks, five-digit years, Dangun year, and the native lunar view.\n"
    "li Use vm status, vm limits, and vm selftest to monitor BOSL, LIL, GASM, Lafillo VM, and OSVM under common step budgets.\n"
    "li Use shrine status, shrine list, shrine verify hello.shrine, and shrine run hello.shrine for the Lard Subsystem for Shrine with BOSL payload validation.\n"
    "li Use glyph demo, glyph auto sample.bmp avatar, glyph move/copy/rename/pixel, glyph live U+E000 on, glyph click U+E000, and glyph insert U+E000 notes.txt to own and edit clickable realtime private-use Unicode picture characters.\n"
    "li The default cursor is the pretty mouse picture at U+E004; use cursor mouse to restore it or cursor set U+E000 to choose another user-owned slot.\n"
    "li Use dir X: for read-only system files and dir Z: for every writable RAM file; the two listings are no longer mixed together.\n"
    "li Use task list and task set id prio to inspect and change queued task priority.\n"
    "li Priority lev.10 is urgent work the user can grant with task urgent id, task set id 10, or nice 10 cmd.\n"
    "li Use tasktop to see runnable and paused task queues with priority bars.\n"
    "li Use bootprof set safe or bootprof set netoff to change the next boot profile.\n"
    "li Awakening mode is off by default; use awake on or awake off to choose the next boot path.\n"
    "li Use crashlog show to inspect panic and diagnostic history.\n"
    "li Use lpack verify sample.lpack before install, and lpack undo last to roll back the last install.\n"
    "li Use screencheck retro for an old boot/storage-style visual screen scan.\n"
    "li Use bugeye scan to catch visible framebuffer/layout bugs and write bugreport.lardd.\n"
    "li Use bugreplay show to review the last BugEye screen-health frames.\n"
    "li Use bugreplay draw to draw the replay frames as a GUI panel.\n"
    "li Use trace on and trace show to inspect LardTrace module and shell events.\n"
    "li Use netwatch on and netwatch show to inspect readable UDP, OSLink, and HTTP GET/POST events.\n"
    "li Use journal show to read the automatic LARDD system journal.\n"
    "li Use rollback snap and rollback last to save and restore user-visible settings.\n"
    "li Use priority history to audit who granted priority lev.10.\n"
    "li Use trust list and trust history to inspect the user-owned permission policy map.\n"
    "li Use lfsdoctor scan or lfsdoctor repair to inspect and repair LPST-backed writable files.\n"
    "li Use panic capsule to bundle crashlog, BugEye, boot, trust, priority, and filesystem state.\n"
    "li v1.41.0b makes PanicRoom real16-backed: panicroom texture draws the iconic LPR default texture from the real-mode bridge before long64 recovery.\n"
    "li v1.44.0a promotes the PanicRoom texture and editable Unicode cursor/glyph track to the official channel.\n"
    "li v1.49.1p hotpatches AMI/Rufus/VirtualBox boot rough edges and makes the default cursor an arrow.\n"
    "li v1.50.0b adds hardware release profiles: universal, seabios, ami, vbox, usb, and realpc.\n"
    "li v1.51.0b makes the default GUI cursor a user-editable Unicode mouse glyph at U+E004.\n"
    "li Use bootmap, bootreplay show, postbaseline show, devmap draw, oldcheck draw, and awakemon to see boot, POST, device, storage, and Awakening progress.\n"
    "li Use ltheme preview default.ltheme and ltheme use night for native shell theme presets.\n"
    "li Use cfgprof save safe-ui and cfgprof load safe-ui for settings profiles.\n"
    "li Use userlaw show to inspect the OS policy principles that protect user control.\n"
    "li Use values to reread the LardOS user-law values from inside the OS.\n"
    "li v1.52.0a officially promotes the LardOS value statement and values command.\n"
    "li v1.52.1p hotpatches the Unicode cursor art and input/output text spacing.\n"
    "li v1.52.2p hotpatches mouse wheel scrolling and visible scrollbar ratios.\n"
    "li v1.52.3p aligns GUI overlay frames, cleans the Unicode cursor, and quiets non-overflow scrollbars.\n"
    "li v1.52.4p fixes edge cursor clipping, keeps screenram from hiding the cursor, and restores visible disabled scrollbars.\n"
    "li v1.52.5p makes the response scrollbar always recognizable with arrows, track, and thumb.\n"
    "li v1.52.6p restores the quieter disabled rail while keeping active proportional scrolling.\n"
    "li v1.53.0a officially adds bye for user-requested poweroff with sync, trace, journal, VM/firmware poweroff, and safe halt fallback.\n"
    "li v1.54.0a officially adds magic -f for explicit raw-control prediction override with dryrun and explain audit records.\n"
    "li v1.55.0a officially adds restart and reboot for user-requested restart with sync, trace, journal, reset ports, and triple-fault fallback.\n"
    "li v1.56.0a officially adds byebye as a friendlier explicit alias for bye while keeping Magic raw-control safety.\n"
    "li v1.57.0b removes EXGUI and EXEXGUI so the default GUI can become the single polished desktop surface.\n"
    "li v1.58.0a officially makes the default GUI a desktop with a top bar, app icons, dock, and hideable app window.\n"
    "li Use lunit run tests.lunit for small native feature tests.\n"
    "li Use oschat say text for local OSLink chat-style module messages.\n"
    "li Use larsview open lardos.lars, larsapp form lardos.lars, and notes add text for native document/app browsing and notes.lardd.\n"
    "li LARSView now supports reload, back, and actions; notes add updates both notes.lardd and GUI notes.txt.\n"
    "button System status | status\n"
    "button Task dashboard | tasktop\n"
    "button Crash history | crashlog show\n"
    "input profile normal\n"
    "li Press P during boot for POST, or M for the CPU Mode Bridge Test.\n"
    "li Use write notes.txt text and append notes.txt text for the RAM FS.\n"
    "li Use vcs status/log/show to inspect the in-OS history layer.\n"
    "li Use lcnt info to inspect syscall-cap containers.\n"
    "li Run lil features.lil to try the native LIL scripting language.\n"
    "li Use sum, peek, poke, and asm_ when you want raw ring-0 control.\n"
    "cmd release\n"
    "cmd magic statsu\n"
    "cmd magic dryrun statsu\n"
    "cmd mode probe\n"
    "cmd mode guard\n"
    "cmd sram on\n"
    "cmd oslink status\n"
    "cmd oslink emit shell hello-from-lardos\n"
    "cmd cfgsh status\n"
    "cmd cfg ltheme night\n"
    "cmd cfg http 2\n"
    "cmd buddy joke\n"
    "cmd lguilib show default.lguilib\n"
    "cmd time\n"
    "cmd lunar\n"
    "cmd dangun\n"
    "cmd vm status\n"
    "cmd vm selftest\n"
    "cmd shrine status\n"
    "cmd shrine verify hello.shrine\n"
    "cmd glyph demo\n"
    "cmd glyph list\n"
    "cmd glyph show U+E000\n"
    "cmd glyph pixel U+E000 0 0 ff00ff\n"
    "cmd glyph rename U+E000 cursor-face\n"
    "cmd glyph live U+E000 on\n"
    "cmd glyph click U+E000\n"
    "cmd cursor set U+E000\n"
    "cmd cursor status\n"
    "cmd glyph write\n"
    "cmd oslink exec 10.0.2.15 status\n"
    "cmd task list\n"
    "cmd tasktop\n"
    "cmd bootprof status\n"
    "cmd awake status\n"
    "cmd awake on\n"
    "cmd awake off\n"
    "cmd crashlog show\n"
    "cmd lpack verify sample.lpack\n"
    "cmd lpack list sample.lpack\n"
    "cmd screencheck retro\n"
    "cmd bugeye scan\n"
    "cmd bugreplay show\n"
    "cmd bugreplay draw\n"
    "cmd trace on\n"
    "cmd trace show\n"
    "cmd netwatch on\n"
    "cmd netwatch show\n"
    "cmd journal show\n"
    "cmd type bugreport.lardd\n"
    "cmd rollback snap demo\n"
    "cmd priority history\n"
    "cmd trust list\n"
    "cmd trust history\n"
    "cmd bootmap\n"
    "cmd oldcheck draw\n"
    "cmd bootreplay show\n"
    "cmd postbaseline show\n"
    "cmd devmap draw\n"
    "cmd lfsdoctor scan\n"
    "cmd panic capsule\n"
    "cmd panicroom texture\n"
    "cmd awakemon\n"
    "cmd ltheme list\n"
    "cmd ltheme preview default.ltheme\n"
    "cmd ltheme show default.ltheme\n"
    "cmd cfgprof save safe-ui\n"
    "cmd userlaw show\n"
    "cmd values\n"
    "cmd lunit run tests.lunit\n"
    "cmd larsapp form lardos.lars\n"
    "cmd oschat say hello-from-lardkit\n"
    "cmd notes add hello-from-lardos\n"
    "cmd type notes.txt\n"
    "cmd larsview open notes.lardd\n"
    "cmd larsview actions lardos.lars\n"
    "cmd larsview back\n"
    "cmd post\n"
    "cmd lil features.lil\n"
    "cmd lardd lardd_guide.lardd\n"
    "note Release suffixes: a=official, b=beta-experimental, p=hotpatch. Hardware profiles: universal, seabios, ami, vbox, usb, realpc.\n"
    "end\n";

static const uint8_t file_default_lguilib[] =
    "LGUILIB 1\n"
    "NAME lardos-overlay\n"
    "COLOR title_bg 0x304060\n"
    "COLOR title_fg 0xffffff\n"
    "COLOR title_accent 0xe7f0ff\n"
    "COLOR panel_bg 0x202840\n"
    "COLOR border 0x05070c\n"
    "COLOR tab_active 0x3e5f82\n"
    "COLOR tab_idle 0x282838\n"
    "COLOR tab_hover 0x343a50\n"
    "COLOR tab_accent 0x72d6ff\n"
    "COLOR button_border 0x203060\n"
    "COLOR button_hover 0xd5e5ff\n"
    "COLOR button_inner 0x6680a0ff\n"
    "COLOR output_frame 0x5a6b86\n"
    "COLOR hint_fg 0xafc2d8\n"
    "COLOR shadow 0x33000000\n"
    "WIDGET title chrome\n"
    "WIDGET tab compact\n"
    "WIDGET button feedback\n"
    "WIDGET output frame\n"
    "WIDGET status badge\n"
    "END\n";

static const uint8_t file_default_ltheme[] =
    "LTHEME 1\n"
    "NAME lardos-night\n"
    "FG 0xddebff\n"
    "BG 0x080c18\n"
    "ACCENT 0x4de1c1\n"
    "STYLE linux\n"
    "END\n";

static const uint8_t file_lardd_guide[] =
    "LARDD 1\n"
    "TITLE LARDD Format\n"
    "TEXT LARDD replaces Markdown for LardOS-authored local documents.\n"
    "TEXT It is record based, easy to parse in freestanding C, and readable without a renderer.\n"
    "SECTION Records\n"
    "ITEM TITLE text -> document title.\n"
    "ITEM SECTION text -> section heading.\n"
    "ITEM TEXT text -> paragraph line.\n"
    "ITEM ITEM text -> list item.\n"
    "ITEM QUOTE text -> quoted note.\n"
    "ITEM CODE / ENDCODE -> verbatim code block.\n"
    "SECTION Example\n"
    "CODE\n"
    "LARDD 1\n"
    "TITLE Notes\n"
    "TEXT A local LardOS document.\n"
    "ITEM One record per line.\n"
    "ENDCODE\n"
    "END\n";

static const uint8_t file_glyph_guide[] =
    "LARDD 1\n"
    "TITLE Image Glyphs\n"
    "TEXT LardOS can bind user pictures to unused private-use Unicode codepoints.\n"
    "TEXT Inline text keeps an 8x8 cell so notes, LARS views, and shell output can render them without external libraries.\n"
    "SECTION Commands\n"
    "ITEM glyph demo -> seed U+E000..U+E004 with built-in picture characters, including the mouse cursor glyph at U+E004.\n"
    "ITEM glyph load U+E000 sample.bmp avatar -> bind a BMP to a chosen slot.\n"
    "ITEM glyph auto sample.bmp avatar -> bind a BMP to the next free slot.\n"
    "ITEM glyph move U+E000 U+E010 -> change the assigned Unicode codepoint; the cursor follows if it used the old slot.\n"
    "ITEM glyph copy U+E000 U+E011 -> duplicate an assigned picture Unicode slot.\n"
    "ITEM glyph rename U+E000 name -> change the user-visible slot label.\n"
    "ITEM glyph pixel U+E000 x y RRGGBB -> edit one 8x8 cell pixel in-place without external tools.\n"
    "ITEM glyph live U+E000 on/off -> toggle realtime hover/click rendering.\n"
    "ITEM glyph click U+E000 -> record the same click event the GUI writes when the picture is clicked.\n"
    "ITEM glyph insert U+E000 notes.txt -> append the actual UTF-8 private-use character.\n"
    "ITEM glyph write -> refresh glyphmap.lardd.\n"
    "SECTION Slots\n"
    "ITEM Range U+E000..U+E0FF is reserved for user-owned image glyphs; U+E004 starts as the default mouse cursor but remains editable.\n"
    "ITEM glyphmap.lardd records source size, average color, name, revision, live state, and click count.\n"
    "END\n";

static const uint8_t file_lardtime_guide[] =
    "LARDD 1\n"
    "TITLE LardOS Time\n"
    "TEXT LardOS Time is native time, not Unix epoch seconds.\n"
    "TEXT It counts ticks from 00000-01-01 00:00:00 and prints years with at least five digits.\n"
    "SECTION Commands\n"
    "ITEM time -> show ticks, solar date, Dangun year, and Lard lunar date.\n"
    "ITEM date -> show the five-digit solar date.\n"
    "ITEM lunar -> show the native lunar date estimate.\n"
    "ITEM dangun -> show CE+2333 as a five-digit Dangun year.\n"
    "ITEM time raw -> show LardOS Time ticks only.\n"
    "ITEM time explain -> show the policy.\n"
    "SECTION Notes\n"
    "ITEM RTC Unix seconds remain only as an internal compatibility input for hardware and TLS checks.\n"
    "ITEM SYS_GET_TIME, LIL time, and BOSL time now expose LardOS Time ticks.\n"
    "END\n";

static const uint8_t file_vm_guide[] =
    "LARDD 1\n"
    "TITLE VM Monitor\n"
    "TEXT VM Monitor keeps LardOS language runtimes visible and bounded without external libraries.\n"
    "TEXT BOSL, LIL, GASM, Lafillo VM, and OSVM report runs, failures, budget hits, last steps, max steps, and return code.\n"
    "SECTION Commands\n"
    "ITEM vm status -> show per-VM counters and the active step budget.\n"
    "ITEM vm limits -> list the current step budgets.\n"
    "ITEM vm selftest -> smoke-test BOSL, LIL, GASM, Lafillo VM, OSVM, and the monitor itself.\n"
    "ITEM vm clear -> reset counters without changing user files.\n"
    "ITEM gasm file.gasm -> run an in-tree GASM source file from LSH.\n"
    "SECTION Safety\n"
    "ITEM GASM and Lafillo VM now stop runaway programs through VM Monitor budgets.\n"
    "ITEM BOSL JIT falls back to the budgeted interpreter for branchy programs so loops stay interruptible.\n"
    "END\n";

static const uint8_t file_shrine_guide[] =
    "LARDD 1\n"
    "TITLE Shrine Subsystem\n"
    "TEXT LSS is the Lard Subsystem for Shrine: a local compatibility wrapper for Shrine-style programs.\n"
    "TEXT It keeps the code in LardOS-owned formats and runs the current .shrine payload through BOSL.\n"
    "SECTION Commands\n"
    "ITEM shrine status -> show LSS readiness, runs, failures, verified files, and last file.\n"
    "ITEM shrine list -> list .shrine files visible to the filesystem.\n"
    "ITEM shrine verify hello.shrine -> check wrapper magic/type plus BOSL payload magic/version without executing.\n"
    "ITEM shrine run hello.shrine -> run the Shrine wrapper and show its output.\n"
    "ITEM shrine test -> run the subsystem selftest.\n"
    "SECTION Format\n"
    "ITEM .shrine starts with LSS\\0, then a one-byte type.\n"
    "ITEM Type 0 is a BOSL payload. Type 1 is reserved for future native Shrine binaries.\n"
    "ITEM srine is accepted as a typo-friendly alias for shrine.\n"
    "END\n";

static const uint8_t file_features_lil[] =
    "; LIL feature tour: assert, condition helpers, repeat, stepped for, and math helpers\n"
    "(begin\n"
    "  (assert (eq (pow 2 8) 256))\n"
    "  (assert (eq (gcd 84 30) 6))\n"
    "  (print (clamp 99 0 10))\n"
    "  (print (between 5 1 5))\n"
    "  (print (within 5 1 5))\n"
    "  (when (eq 1 1) (print 111))\n"
    "  (unless 0 (print 222))\n"
    "  (repeat 4 (printn it) (emit 32))\n"
    "  (emit 10)\n"
    "  (for i 5 -1 -2 (begin (printn i) (emit 32)))\n"
    "  (emit 10)\n"
    "  (print (lcm 6 14 21)))\n";

static const uint8_t file_sample_lpack[] =
    "LPACK 1\n"
    "NAME starter\n"
    "FILE notes.txt\n"
    "Installed by LardPack.\n"
    "Use type notes.txt after install.\n"
    "ENDFILE\n"
    "END\n";

static const uint8_t file_tests_lunit[] =
    "LUNIT 1\n"
    "CHECK file lardos.lars\n"
    "CHECK file lardtime_guide.lardd\n"
    "CHECK file default.ltheme\n"
    "CHECK writable journal.lardd\n"
    "CHECK writable userlaw.lardd\n"
    "CHECK writable glyphmap.lardd\n"
    "CHECK command trace\n"
    "CHECK command netwatch\n"
    "CHECK command glyph\n"
    "CHECK command cursor\n"
    "CHECK command vm\n"
    "CHECK command gasm\n"
    "CHECK command shrine\n"
    "CHECK file hello.shrine\n"
    "CHECK file shrine_guide.lardd\n"
    "CHECK file vm_guide.lardd\n"
    "CHECK command time\n"
    "CHECK command lunar\n"
    "CHECK command dangun\n"
    "CHECK command notes\n"
    "CHECK command larsview\n"
    "CHECK command dir\n"
    "CHECK command mode\n"
    "CHECK command panicroom\n"
    "CHECK command paniccapsule\n"
    "END\n";

/* bundle.lar - native LAR1 multi-file archive, method 0 = stored. */
static const uint8_t file_bundle_lar[166] = {
    'L','A','R','1', 0x03,0x00, 0x4D,0x00,
    0x09,0x00,0x00,0x00, 0x55,0x00,0x00,0x00, 0x16,0x00,0x00,0x00, 0x16,0x00,0x00,0x00,
    'h','e','l','l','o','.','t','x','t',
    0x0A,0x00,0x00,0x00, 0x6B,0x00,0x00,0x00, 0x2D,0x00,0x00,0x00, 0x2D,0x00,0x00,0x00,
    'r','e','a','d','m','e','.','t','x','t',
    0x0A,0x00,0x00,0x00, 0x98,0x00,0x00,0x00, 0x0E,0x00,0x00,0x00, 0x0E,0x00,0x00,0x00,
    's','c','r','i','p','t','.','l','s','h',
    'H','e','l','l','o',' ','f','r','o','m',' ','b','u','n','d','l','e','.','l','a','r','\n',
    'L','A','R',' ','s','t','o','r','e','s',' ','m','a','n','y',' ','f','i','l','e','s',' ',
    'i','n',' ','o','n','e',' ','n','a','t','i','v','e',' ','a','r','c','h','i','v','e','.','\n',
    'e','c','h','o',' ','f','r','o','m','-','l','a','r','\n'
};

/* 8x8 24-bit BMP (red/blue gradient) */
static const uint8_t file_sample_bmp[246] = {
    'B', 'M',
    0xF6, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,  /* file size 246, px offset 54 */
    40, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 1, 0, 24, 0,
    0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 8 rows, 24 bytes each (8*3 BGR), padded to 4 */
    0, 0, 255, 0, 0, 255, 64, 0, 192, 64, 0, 192, 128, 0, 128, 128, 0, 128, 192, 0, 64, 192, 0, 64,
    255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0,
    0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255,
    255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0,
    0, 0, 255, 128, 0, 128, 192, 0, 64, 255, 0, 0, 255, 0, 0, 192, 0, 64, 128, 0, 128, 0, 0, 255,
    255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0,
    0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255,
    255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0,
};

/* demo.larsh - LARSH animation: rect + circle + text + LARDD with keyframe */
static const uint8_t file_demo_larsh[] =
    "LARSH 1\n"
    "w 256\n"
    "h 192\n"
    "fps 12\n"
    "bg 0x1a1a2e\n"
    "loop\n"
    "\n"
    "obj 0 rect 20 80 60 40 0xe94560\n"
    "obj 1 circle 200 96 25 0x0f3460\n"
    "obj 2 text 80 4 \"Hello LardOS\" 0xeaeaea\n"
    "obj 3 lardd 8 24 240 160 0xeaeaea \"LARDD 1\n"
    "TITLE LARSH LARDD Demo\n"
    "TEXT Animated scenes can carry native LardOS documents.\n"
    "ITEM No Markdown surface is needed.\n"
    "ITEM LARDD is parsed by kernel/lard_doc.c.\n"
    "END\"\n"
    "\n"
    "key 0 0 x 20\n"
    "key 60 0 x 180\n"
    "key 120 0 x 20\n"
    "key 0 1 x 200\n"
    "key 60 1 x 56\n"
    "key 120 1 x 200\n";

/* hello.shrine - LSS format (LSS\0 + type 0 BOSL) + BOSL: print "Hello from LSS (Shrine)!\n" */
static const uint8_t file_hello_shrine[65] = {
    'L','S','S',0, 0,  /* LSS magic, type=BOSL */
    /* BOSL: 1 const, code size 7 */
    'B','O','S','L', 0x01,0x00, 0x00,0x00, 0x01,0x00,0x00,0x00, 0x07,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    /* Const 0: str len 25 */
    0x01,0x00,0x00,0x00, 0x19,0x00,0x00,0x00,
    'H','e','l','l','o',' ','f','r','o','m',' ','L','S','S',' ','(','S','h','r','i','n','e',')','!','\n',
    /* Code: pushc 0, print, halt */
    0x02, 0x00,0x00,0x00,0x00, 0x20, 0xFF
};

static const FsFile FS_FILES[] = {
    { "hello.shrine",  file_hello_shrine,  sizeof(file_hello_shrine) },
    { "hello.txt",     file_hello_txt,     sizeof(file_hello_txt) - 1 },
    { "readme.txt",    file_readme_txt,    sizeof(file_readme_txt) - 1 },
    { "lardos.lars",   file_lardos_lars,   sizeof(file_lardos_lars) - 1 },
    { "default.lguilib", file_default_lguilib, sizeof(file_default_lguilib) - 1 },
    { "default.ltheme", file_default_ltheme, sizeof(file_default_ltheme) - 1 },
    { "lardd_guide.lardd", file_lardd_guide, sizeof(file_lardd_guide) - 1 },
    { "glyph_guide.lardd", file_glyph_guide, sizeof(file_glyph_guide) - 1 },
    { "lardtime_guide.lardd", file_lardtime_guide, sizeof(file_lardtime_guide) - 1 },
    { "vm_guide.lardd", file_vm_guide, sizeof(file_vm_guide) - 1 },
    { "shrine_guide.lardd", file_shrine_guide, sizeof(file_shrine_guide) - 1 },
    { "releases.lardd", file_releases_lardd, sizeof(file_releases_lardd) - 1 },
    { "features.lil",  file_features_lil,  sizeof(file_features_lil) - 1 },
    { "sample.lpack",  file_sample_lpack,  sizeof(file_sample_lpack) - 1 },
    { "tests.lunit",   file_tests_lunit,   sizeof(file_tests_lunit) - 1 },
    { "bundle.lar",    file_bundle_lar,    sizeof(file_bundle_lar) },
    { "sample.bmp",    file_sample_bmp,    sizeof(file_sample_bmp) },
    { "rtl8139.drfl",  file_rtl8139_drfl,  sizeof(file_rtl8139_drfl) },
    { "piix3ide.drfl", file_piix3ide_drfl, sizeof(file_piix3ide_drfl) },
#include "fs_ldll_entries.inc"
    { "lafillo_demo.bosx", file_lafillo_demo_bosx, sizeof(file_lafillo_demo_bosx) },
    { "demo.larsh",      file_demo_larsh,      sizeof(file_demo_larsh) - 1 },
};

static const uint32_t FS_FILE_COUNT = sizeof(FS_FILES) / sizeof(FS_FILES[0]);

static FsFile g_lfs_result;
static char g_lfs_name[LFS_MAX_NAME];
static FsFile g_ram_result;

static uint32_t lpst_read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void lpst_write32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t lpst_hash(const uint8_t* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static void lpst_zero(void)
{
    for (uint32_t i = 0; i < LPST_BANK_BYTES; i++) s_lpstore[i] = 0;
}

static int lpst_validate_bank(const uint8_t* store, uint32_t* header_size,
                              uint32_t* count, uint32_t* total, uint32_t* generation)
{
    uint32_t version;
    uint32_t hsz;
    uint32_t c;
    uint32_t t;
    uint32_t checksum;

    if (lpst_read32(store + 0) != LPST_MAGIC) return -1;
    version = lpst_read32(store + 4);
    if (version == 1u) {
        hsz = LPST_V1_HEADER_SIZE;
        if (generation) *generation = 0;
    } else if (version == LPST_VERSION) {
        hsz = LPST_HEADER_SIZE;
        if (generation) *generation = lpst_read32(store + 20);
    } else {
        return -2;
    }

    c = lpst_read32(store + 8);
    t = lpst_read32(store + 12);
    checksum = lpst_read32(store + 16);
    if (c > 32u || t < hsz || t > LPST_BANK_BYTES || hsz + c * LPST_ENTRY_SIZE > t) return -3;
    if (lpst_hash(store + hsz, t - hsz) != checksum) return -4;

    if (header_size) *header_size = hsz;
    if (count) *count = c;
    if (total) *total = t;
    return 0;
}

static uint32_t writable_count(void)
{
    return 19u;
}

static FsWritableFile* writable_at(uint32_t idx)
{
    if (idx == 0) return &ram_notes;
    if (idx == 1) return &ram_lardd_notes;
    if (idx == 2) return &ram_lafillo_save;
    if (idx == 3) return &ram_lar_extract;
    if (idx == 4) return &ram_vcs_restore;
    if (idx == 5) return &ram_bootprof;
    if (idx == 6) return &ram_crashlog;
    if (idx == 7) return &ram_bugreport;
    if (idx == 8) return &ram_bugreplay;
    if (idx == 9) return &ram_panic_capsule;
    if (idx == 10) return &ram_lfsdoctor;
    if (idx == 11) return &ram_trace;
    if (idx == 12) return &ram_netwatch;
    if (idx == 13) return &ram_journal;
    if (idx == 14) return &ram_postbaseline;
    if (idx == 15) return &ram_bootreplay;
    if (idx == 16) return &ram_cfgprof;
    if (idx == 17) return &ram_userlaw;
    if (idx == 18) return &ram_glyphmap;
    return NULL;
}

void fs_init(void)
{
    for (uint32_t i = 0; i < sizeof(notes_init) - 1 && i < RAM_FILE_CAP; i++) {
        ram_notes_buf[i] = notes_init[i];
    }
    ram_notes.size = sizeof(notes_init) - 1;
    for (uint32_t i = 0; i < sizeof(lardd_notes_init) - 1 && i < LARDD_NOTES_CAP; i++) {
        ram_lardd_notes_buf[i] = lardd_notes_init[i];
    }
    ram_lardd_notes.size = sizeof(lardd_notes_init) - 1;
    for (uint32_t i = 0; i < sizeof(bootprof_init) - 1 && i < BOOTPROF_CAP; i++) {
        ram_bootprof_buf[i] = bootprof_init[i];
    }
    ram_bootprof.size = sizeof(bootprof_init) - 1;
    for (uint32_t i = 0; i < sizeof(crashlog_init) - 1 && i < CRASHLOG_CAP; i++) {
        ram_crashlog_buf[i] = crashlog_init[i];
    }
    ram_crashlog.size = sizeof(crashlog_init) - 1;
    for (uint32_t i = 0; i < sizeof(bugreport_init) - 1 && i < BUGREPORT_CAP; i++) {
        ram_bugreport_buf[i] = bugreport_init[i];
    }
    ram_bugreport.size = sizeof(bugreport_init) - 1;
    for (uint32_t i = 0; i < sizeof(bugreplay_init) - 1 && i < BUGREPLAY_CAP; i++) {
        ram_bugreplay_buf[i] = bugreplay_init[i];
    }
    ram_bugreplay.size = sizeof(bugreplay_init) - 1;
    for (uint32_t i = 0; i < sizeof(panic_capsule_init) - 1 && i < PANIC_CAPSULE_CAP; i++) {
        ram_panic_capsule_buf[i] = panic_capsule_init[i];
    }
    ram_panic_capsule.size = sizeof(panic_capsule_init) - 1;
    for (uint32_t i = 0; i < sizeof(lfsdoctor_init) - 1 && i < LFSDOCTOR_CAP; i++) {
        ram_lfsdoctor_buf[i] = lfsdoctor_init[i];
    }
    ram_lfsdoctor.size = sizeof(lfsdoctor_init) - 1;
    for (uint32_t i = 0; i < sizeof(trace_init) - 1 && i < TRACE_CAP; i++) {
        ram_trace_buf[i] = trace_init[i];
    }
    ram_trace.size = sizeof(trace_init) - 1;
    for (uint32_t i = 0; i < sizeof(netwatch_init) - 1 && i < NETWATCH_CAP; i++) {
        ram_netwatch_buf[i] = netwatch_init[i];
    }
    ram_netwatch.size = sizeof(netwatch_init) - 1;
    for (uint32_t i = 0; i < sizeof(journal_init) - 1 && i < JOURNAL_CAP; i++) {
        ram_journal_buf[i] = journal_init[i];
    }
    ram_journal.size = sizeof(journal_init) - 1;
    for (uint32_t i = 0; i < sizeof(postbaseline_init) - 1 && i < POSTBASELINE_CAP; i++) {
        ram_postbaseline_buf[i] = postbaseline_init[i];
    }
    ram_postbaseline.size = sizeof(postbaseline_init) - 1;
    for (uint32_t i = 0; i < sizeof(bootreplay_init) - 1 && i < BOOTREPLAY_CAP; i++) {
        ram_bootreplay_buf[i] = bootreplay_init[i];
    }
    ram_bootreplay.size = sizeof(bootreplay_init) - 1;
    for (uint32_t i = 0; i < sizeof(cfgprof_init) - 1 && i < CFGPROF_CAP; i++) {
        ram_cfgprof_buf[i] = cfgprof_init[i];
    }
    ram_cfgprof.size = sizeof(cfgprof_init) - 1;
    for (uint32_t i = 0; i < sizeof(userlaw_init) - 1 && i < USERLAW_CAP; i++) {
        ram_userlaw_buf[i] = userlaw_init[i];
    }
    ram_userlaw.size = sizeof(userlaw_init) - 1;
    for (uint32_t i = 0; i < sizeof(glyphmap_init) - 1 && i < GLYPHMAP_CAP; i++) {
        ram_glyphmap_buf[i] = glyphmap_init[i];
    }
    ram_glyphmap.size = sizeof(glyphmap_init) - 1;
    lfs_mount(lfs_volume, sizeof(lfs_volume));
    (void)fs_persist_load();
}

const FsFile* fs_open(const char* name)
{
    const FsFile* f = fs_open_readonly(name);
    if (f) return f;
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        const char* a = w ? w->name : "";
        const char* b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') {
            g_ram_result.name = w->name;
            g_ram_result.data = w->data;
            g_ram_result.size = w->size;
            return &g_ram_result;
        }
    }
    return 0;
}

const FsFile* fs_open_readonly(const char* name)
{
    for (uint32_t i = 0; i < FS_FILE_COUNT; i++) {
        const char* a = FS_FILES[i].name;
        const char* b = name;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return &FS_FILES[i];
        }
    }
    {
        const uint8_t* data;
        uint32_t sz;
        if (lfs_lookup(name, &data, &sz)) {
            uint32_t j = 0;
            while (name[j] && j < LFS_MAX_NAME - 1) { g_lfs_name[j] = name[j]; j++; }
            g_lfs_name[j] = '\0';
            g_lfs_result.name = g_lfs_name;
            g_lfs_result.data = data;
            g_lfs_result.size = sz;
            return &g_lfs_result;
        }
    }
    return 0;
}

uint32_t fs_read(const FsFile* file, uint32_t offset, uint8_t* buf, uint32_t len)
{
    if (!file) {
        return 0;
    }
    if (offset >= file->size) {
        return 0;
    }
    uint32_t remaining = file->size - offset;
    if (len > remaining) {
        len = remaining;
    }
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = file->data[offset + i];
    }
    return len;
}

void fs_list(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    if (!cb) return;
    fs_list_readonly(cb, user);
    fs_list_writable(cb, user);
}

void fs_list_readonly(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    if (!cb) return;
    for (uint32_t i = 0; i < FS_FILE_COUNT; i++) {
        cb(FS_FILES[i].name, FS_FILES[i].size, user);
    }
    lfs_list(cb, user);
}

void fs_list_writable(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    if (!cb) return;
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        if (w) cb(w->name, w->size, user);
    }
}

uint32_t fs_writable_count(void)
{
    return writable_count();
}

static int lpst_name_equals(const uint8_t* fixed_name, const char* name)
{
    for (uint32_t i = 0; i < 32u; i++) {
        char a = (char)fixed_name[i];
        char b = name[i];
        if (a != b) return 0;
        if (a == '\0') return 1;
    }
    return 0;
}

void fs_mark_dirty(void)
{
    s_fs_dirty = 1;
}

int fs_persist_save(void)
{
    uint32_t count = writable_count();
    uint32_t data_off = LPST_HEADER_SIZE + count * LPST_ENTRY_SIZE;
    uint32_t target_bank;
    uint32_t next_generation;

    if (storage_init() != 0) {
        s_persist_last_result = -1;
        return -1;
    }
    if (data_off > LPST_BANK_BYTES) {
        s_persist_last_result = -2;
        return -2;
    }

    target_bank = (s_persist_active_bank < LPST_BANKS) ? (1u - s_persist_active_bank) : 0u;
    next_generation = s_persist_generation + 1u;
    if (next_generation == 0) next_generation = 1;

    lpst_zero();
    lpst_write32(s_lpstore + 0, LPST_MAGIC);
    lpst_write32(s_lpstore + 4, LPST_VERSION);
    lpst_write32(s_lpstore + 8, count);
    lpst_write32(s_lpstore + 20, next_generation);

    for (uint32_t i = 0; i < count; i++) {
        FsWritableFile* w = writable_at(i);
        uint8_t* entry = s_lpstore + LPST_HEADER_SIZE + i * LPST_ENTRY_SIZE;
        uint32_t n = 0;

        if (!w || w->size > w->cap || w->size > LPST_BANK_BYTES - data_off) {
            s_persist_last_result = -3;
            return -3;
        }
        while (w->name[n] && n < 31u) {
            entry[n] = (uint8_t)w->name[n];
            n++;
        }
        entry[n] = 0;
        lpst_write32(entry + 32, w->size);
        lpst_write32(entry + 36, data_off);
        lpst_write32(entry + 40, w->cap);
        lpst_write32(entry + 44, lpst_hash(w->data, w->size));
        for (uint32_t j = 0; j < w->size; j++) s_lpstore[data_off + j] = w->data[j];
        data_off += w->size;
    }

    lpst_write32(s_lpstore + 12, data_off);
    lpst_write32(s_lpstore + 16, lpst_hash(s_lpstore + LPST_HEADER_SIZE, data_off - LPST_HEADER_SIZE));

    for (uint32_t sector = 0; sector < LPST_BANK_SECTORS; sector++) {
        uint32_t lba = LPST_START_LBA + target_bank * LPST_BANK_SECTORS + sector;
        if (storage_write_sector(lba, s_lpstore + sector * STORAGE_SECTOR_SIZE) != 0) {
            s_persist_last_result = -4;
            return -4;
        }
    }

    s_fs_dirty = 0;
    s_persist_active_bank = target_bank;
    s_persist_generation = next_generation;
    s_persist_last_result = 0;
    return 0;
}

int fs_persist_load(void)
{
    uint32_t best_bank = 0xFFFFFFFFu;
    uint32_t best_generation = 0;
    uint32_t count;
    uint32_t total;
    uint32_t header_size;

    if (storage_init() != 0) {
        s_persist_last_result = -1;
        return -1;
    }

    for (uint32_t bank = 0; bank < LPST_BANKS; bank++) {
        uint32_t generation = 0;
        int read_ok = 1;
        for (uint32_t sector = 0; sector < LPST_BANK_SECTORS; sector++) {
            uint32_t lba = LPST_START_LBA + bank * LPST_BANK_SECTORS + sector;
            if (storage_read_sector(lba, s_lpstore + sector * STORAGE_SECTOR_SIZE) != 0) {
                read_ok = 0;
                break;
            }
        }
        if (!read_ok) continue;
        if (lpst_validate_bank(s_lpstore, NULL, NULL, NULL, &generation) == 0) {
            if (best_bank == 0xFFFFFFFFu || generation >= best_generation) {
                best_bank = bank;
                best_generation = generation;
            }
        }
    }

    if (best_bank == 0xFFFFFFFFu) {
        s_persist_last_result = -3;
        return -3;
    }

    for (uint32_t sector = 0; sector < LPST_BANK_SECTORS; sector++) {
        uint32_t lba = LPST_START_LBA + best_bank * LPST_BANK_SECTORS + sector;
        if (storage_read_sector(lba, s_lpstore + sector * STORAGE_SECTOR_SIZE) != 0) {
            s_persist_last_result = -2;
            return -2;
        }
    }
    if (lpst_validate_bank(s_lpstore, &header_size, &count, &total, &best_generation) != 0) {
        s_persist_last_result = -5;
        return -5;
    }

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* entry = s_lpstore + header_size + i * LPST_ENTRY_SIZE;
        uint32_t size = lpst_read32(entry + 32);
        uint32_t data_off = lpst_read32(entry + 36);
        uint32_t cap = lpst_read32(entry + 40);
        uint32_t hash = lpst_read32(entry + 44);
        FsWritableFile* w = NULL;

        for (uint32_t wi = 0; wi < writable_count(); wi++) {
            FsWritableFile* cand = writable_at(wi);
            if (cand && lpst_name_equals(entry, cand->name)) {
                w = cand;
                break;
            }
        }
        if (!w) continue;
        if (cap != w->cap || size > w->cap || data_off > total || size > total - data_off) continue;
        if (lpst_hash(s_lpstore + data_off, size) != hash) continue;
        for (uint32_t j = 0; j < size; j++) w->data[j] = s_lpstore[data_off + j];
        w->size = size;
    }

    s_fs_dirty = 0;
    s_persist_active_bank = best_bank;
    s_persist_generation = best_generation;
    s_persist_last_result = 0;
    return 0;
}

void fs_persist_info(uint32_t* available, uint32_t* dirty, int* last_result,
                     const char** driver, uint32_t* lba, uint32_t* sectors)
{
    if (available) *available = storage_available() ? 1u : 0u;
    if (dirty) *dirty = s_fs_dirty;
    if (last_result) *last_result = s_persist_last_result;
    if (driver) *driver = storage_driver_name();
    if (lba) *lba = LPST_START_LBA;
    if (sectors) *sectors = LPST_SECTORS;
}

void fs_persist_detail(uint32_t* active_bank, uint32_t* generation, uint32_t* bank_sectors)
{
    if (active_bank) *active_bank = s_persist_active_bank;
    if (generation) *generation = s_persist_generation;
    if (bank_sectors) *bank_sectors = LPST_BANK_SECTORS;
}

FsWritableFile* fs_open_writable(const char* name)
{
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        const char* a = w ? w->name : "";
        const char* b = name ? name : "";
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') return w;
    }
    return NULL;
}

uint32_t fs_write(FsWritableFile* f, uint32_t offset, const uint8_t* buf, uint32_t len)
{
    if (!f || !buf || offset > f->cap) return 0;
    if (len > f->cap - offset) len = f->cap - offset;
    for (uint32_t i = 0; i < len; i++) f->data[offset + i] = buf[i];
    if (offset == 0 || offset + len > f->size) f->size = offset + len;
    s_fs_dirty = 1;
    return len;
}

uint32_t fs_append(FsWritableFile* f, const uint8_t* buf, uint32_t len)
{
    if (!f || !buf) return 0;
    if (f->size + len > f->cap) len = f->cap - f->size;
    for (uint32_t i = 0; i < len; i++) f->data[f->size + i] = buf[i];
    f->size += len;
    if (len) s_fs_dirty = 1;
    return len;
}
