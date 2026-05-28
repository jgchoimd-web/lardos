#include "fs.h"
#include "fstwt.h"
#include "lfs.h"
#include "rxr.h"
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

/* rtl8139.drfl - DRFL 2 source-carrying driver for RTL8139. */
static const uint8_t file_rtl8139_drfl[] =
    "DRFL 2\n"
    "ID rtl8139\n"
    "TYPE net\n"
    "PCI 10EC 8139\n"
    "LANG DRFL-C\n"
    "CODE int drfl_init(void* ctx) {\n"
    "CODE   rtl8139_t* n = (rtl8139_t*)ctx;\n"
    "CODE   pci_find(0x10EC, 0x8139); pci_enable_io_and_busmaster();\n"
    "CODE   n->io_base = pci_bar0_io(); n->irq_line = pci_irq();\n"
    "CODE   outb(n->io_base + 0x52, 0x00); outb(n->io_base + 0x37, 0x10);\n"
    "CODE   wait_until((inb(n->io_base + 0x37) & 0x10) == 0);\n"
    "CODE   read_mac(n->io_base + 0x00, n->mac); set_rx_ring(); set_tx_rings();\n"
    "CODE   outl(n->io_base + 0x44, RCR_APM | RCR_AB | RCR_WRAP);\n"
    "CODE   outb(n->io_base + 0x37, CMD_RX_EN | CMD_TX_EN); mask_interrupts_polling();\n"
    "CODE   return 0;\n"
    "CODE }\n"
    "END\n";

/* piix3ide.drfl - DRFL 2 source-carrying driver for Intel PIIX3 IDE. */
static const uint8_t file_piix3ide_drfl[] =
    "DRFL 2\n"
    "ID ata-pio\n"
    "TYPE block\n"
    "PCI 8086 7010\n"
    "LANG DRFL-C\n"
    "CODE int drfl_init(void* ctx) {\n"
    "CODE   (void)ctx; select_primary_ata_ports(0x1F0, 0x3F6);\n"
    "CODE   outb(0x3F6, 0x02); outb(0x1F6, 0xA0); ata_delay_400ns();\n"
    "CODE   outb(0x1F2, 0); outb(0x1F3, 0); outb(0x1F4, 0); outb(0x1F5, 0);\n"
    "CODE   outb(0x1F7, 0xEC); wait_not_busy(); require_drq();\n"
    "CODE   read_identify_words(0x1F0, 256); publish_sector_count(words[60..61]);\n"
    "CODE   return 0;\n"
    "CODE }\n"
    "END\n";

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
    "ITEM Repair over halt: panic room, auxkernel, lfsdoctor, bugeye, post, and bootmap exist so the user can recover.\n"
    "ITEM User-grantable power: the user may grant priority lev.10 and enter SUM/raw control knowingly.\n"
    "ITEM Keyboard completeness: mouse workflows should also have keyboard and shortcut routes so the full OS can be driven without a mouse.\n"
    "ITEM Native expression: LARS, LARDD, LGUILIB, LTHEME, LPACK, RXR, SYSRXE, RXE, KMO command modules, LFS, and picture Unicode keep the system's surface its own.\n"
    "ITEM Honest releases: a is official, b is beta-experimental, p is hotpatch; hardware profiles name the target.\n"
    "ITEM LTS honesty: only one LTS line is active at a time, and support changes must be visible through release lts.\n"
    "ITEM Communication: OS modules, processes, and other systems should communicate through visible OSLink and KModTalk paths.\n"
    "SECTION Commands\n"
    "ITEM values -> read this law.\n"
    "ITEM userlaw show -> read this law.\n"
    "ITEM userlaw reset -> restore this law.\n"
    "ITEM bleed file -> user-owned last-resort deletion for broken files; it must report the routes it tried.\n"
    "ITEM bleed overflow file -> bounded in-slot wipe before deletion when the user chooses the harsher path.\n"
    "ITEM crash command -> user-owned diagnostic fault triggers must be explicit, visible, and logged.\n"
    "ITEM auxkernel -> emergency containment must be visible, module-independent, and must not damage hardware.\n"
    "ITEM megaclip -> keyboard-first 10-slot clipboard for moving data through the OS.\n"
    "ITEM lconnect -> LardOS computers may share resources over a visible LAN protocol; input sharing and quiet grants live only behind deprecated confirm commands and must remain logged.\n"
    "ITEM trust history, priority history, magic explain, bootreplay show, panic capsule -> audit power after it is used.\n"
    "END\n";
static uint8_t ram_userlaw_buf[USERLAW_CAP];
static FsWritableFile ram_userlaw = { "userlaw.lardd", ram_userlaw_buf, 0, USERLAW_CAP };

#define FSTWTS_CAP 4096u
static const uint8_t fstwts_init[] =
    "FSTWTS 1\n"
    "MODE HYBRID\n"
    "MAIN lardos ROOT TRANSLATE\n"
    "SUB sandbox sbx_ VM\n"
    "# File System Two Way Translator script.\n"
    "# MAIN can choose the default namespace; SUB adds coexisting virtual filesystems.\n"
    "# MAP your/friendly/path/ <=> flat_lardos_prefix_\n"
    "MAP fstwt/demo/ <=> f2wdemo_\n";
static uint8_t ram_fstwts_buf[FSTWTS_CAP];
static FsWritableFile ram_fstwts = { "fstwt.fstwts", ram_fstwts_buf, 0, FSTWTS_CAP };

#define GLYPHMAP_CAP 16384u
static const uint8_t glyphmap_init[] =
    "LARDD 1\n"
    "TITLE Image Glyph Map\n"
    "TEXT No picture Unicode slots have been assigned yet. Use glyph demo or glyph auto sample.bmp sample, then click them in the GUI.\n"
    "END\n";
static uint8_t ram_glyphmap_buf[GLYPHMAP_CAP];
static FsWritableFile ram_glyphmap = { "glyphmap.lardd", ram_glyphmap_buf, 0, GLYPHMAP_CAP };

#define DOSMODE_CAP 2048u
static const uint8_t dosmode_init[] =
    "LARDD 1\n"
    "TITLE DOS Mode State\n"
    "TEXT DOS mode is off by default. Use dos on to enter L-DOS mode.\n"
    "SECTION Events\n";
static uint8_t ram_dosmode_buf[DOSMODE_CAP];
static FsWritableFile ram_dosmode = { "dosmode.lardd", ram_dosmode_buf, 0, DOSMODE_CAP };

#define WALLPAPER_CAP 512u
static const uint8_t wallpaper_init[] =
    "LARDD 1\n"
    "TITLE LardOS Wallpaper\n"
    "MODE grid\n"
    "PATTERN grid\n"
    "COLOR 0xFF10151E\n"
    "COLOR2 0xFF151E29\n"
    "FILE \n"
    "END\n";
static uint8_t ram_wallpaper_buf[WALLPAPER_CAP];
static FsWritableFile ram_wallpaper = { "wallpaper.lardd", ram_wallpaper_buf, 0, WALLPAPER_CAP };

#define DISPLAYFIX_CAP 1024u
static const uint8_t displayfix_init[] =
    "SPFX 1\n"
    "# User-owned subpixel display defect filter.\n"
    "# ON applies rules; OFF keeps the script editable but inactive.\n"
    "OFF\n"
    "# RECT x y w h r% g% b%\n"
    "# Example: darken red subpixels in a too-bright patch.\n"
    "# RECT 100 40 20 12 85 100 100\n"
    "# PIXEL x y r% g% b% fixes one physical pixel.\n"
    "END\n";
static uint8_t ram_displayfix_buf[DISPLAYFIX_CAP];
static FsWritableFile ram_displayfix = { "displayfix.spfx", ram_displayfix_buf, 0, DISPLAYFIX_CAP };

#define SECURITY_CAP 1024u
static const uint8_t security_init[] =
    "LARDD 1\n"
    "TITLE LardSec Policy\n"
    "TEXT Optional, user-owned storage protection. Default is visible and on, not mandatory.\n"
    "TEXT secure key shows the LardLocker-style recovery key.\n"
    "TEXT secure on enables encrypted-at-rest MDFS media writes.\n"
    "TEXT secure seal writes Y:/Z:/A: as LSEC sealed containers with storage ECC records.\n"
    "TEXT secure ecc ram on keeps a volatile in-kernel ECC mirror for CPUs without hardware ECC.\n"
    "TEXT secure ecc storage on stores ECC records in sealed media headers.\n"
    "TEXT secure lock seals then blocks media access until secure unlock KEY.\n"
    "TEXT secure off writes plaintext again because the machine belongs to the user.\n"
    "TEXT auxkernel keydrop confirm discards volatile media keys only after explicit emergency confirmation.\n"
    "END\n";
static uint8_t ram_security_buf[SECURITY_CAP];
static FsWritableFile ram_security = { "security.lardd", ram_security_buf, 0, SECURITY_CAP };

#define MEGACLIP_CAP 1024u
static const uint8_t megaclip_init_doc[] =
    "LARDD 1\n"
    "TITLE MegaClipboard\n"
    "TEXT Keyboard-first 10-slot clipboard for text, file bytes, paths, labels, and future OS objects.\n"
    "TEXT Default mode is stack: newest is slot 1, older entries follow, and slot 0 means the tenth slot.\n"
    "TEXT single mode keeps one current clipboard space; order mode lists copied items by copy order.\n"
    "TEXT Ctrl+Y copies the active editor/response/item, Ctrl+P pastes latest, Ctrl+Space then 1..9/0 pulls a slot.\n"
    "TEXT Commands: megaclip status, list, mode stack/single/order, push text, file path, pull slot, write slot file.\n"
    "END\n";
static uint8_t ram_megaclip_buf[MEGACLIP_CAP];
static FsWritableFile ram_megaclip = { "megaclip.lardd", ram_megaclip_buf, 0, MEGACLIP_CAP };

#define LCONNECT_CAP 2048u
static const uint8_t lconnect_init_doc[] =
    "LARDD 1\n"
    "TITLE LardOS Connect\n"
    "TEXT LardOS Connect shares non-input hardware resources between LardOS computers over LAN cable/IP.\n"
    "TEXT It can advertise MegaClipboard, CPU, GPU, storage, and peripheral resources; keyboard and mouse are intentionally local-only.\n"
    "TEXT Default is off and manual-grant so sharing is user-owned, visible, and reversible.\n"
    "TEXT Use lconnect direct IP MASK when two machines are connected without DHCP.\n"
    "TEXT Commands: lconnect on, off, direct, discover, peers, share all on, mode manual/auto, syncclip ip, request ip resource, grant ip resource, deny ip resource, log.\n"
    "TEXT Deprecated raw-control: deprecated lconnect input on confirm and deprecated lconnect quiet on confirm can expose input or quiet grants, but status/log still show it.\n"
    "END\n";
static uint8_t ram_lconnect_buf[LCONNECT_CAP];
static FsWritableFile ram_lconnect = { "lconnect.lardd", ram_lconnect_buf, 0, LCONNECT_CAP };

#define OFFICE_DOC_CAP 4096u
static const uint8_t office_doc_init[] =
    "LARDD 1\n"
    "TITLE LardWrite Document\n"
    "SECTION Draft\n"
    "TEXT Type a line in LardWrite and press Run to append it here.\n"
    "ITEM Commands: title, section, bullet, quote, code, find, stats.\n";
static uint8_t ram_office_doc_buf[OFFICE_DOC_CAP];
static FsWritableFile ram_office_doc = { "office_doc.lardd", ram_office_doc_buf, 0, OFFICE_DOC_CAP };

#define OFFICE_SHEET_CAP 4096u
static const uint8_t office_sheet_init[] =
    "LSHEET 1\n"
    "TITLE LardSheet Workbook\n"
    "COL Item Value\n"
    "ROW sample 42\n"
    "CELL A1 42\n"
    "CELL B1 8\n";
static uint8_t ram_office_sheet_buf[OFFICE_SHEET_CAP];
static FsWritableFile ram_office_sheet = { "office_sheet.lsheet", ram_office_sheet_buf, 0, OFFICE_SHEET_CAP };

#define OFFICE_DECK_CAP 4096u
static const uint8_t office_deck_init[] =
    "LSHOW 1\n"
    "TITLE LardShow Deck\n"
    "THEME classic\n"
    "SLIDE Welcome | Native LardOS presentation deck.\n"
    "NOTE Use lshow next, prev, slide N, theme, and note.\n"
    "SLIDE Values | Files stay editable, visible, and local.\n";
static uint8_t ram_office_deck_buf[OFFICE_DECK_CAP];
static FsWritableFile ram_office_deck = { "office_deck.lshow", ram_office_deck_buf, 0, OFFICE_DECK_CAP };

#define AUXKERNEL_CAP 1536u
static const uint8_t auxkernel_init[] =
    "LARDD 1\n"
    "TITLE AuxKernel Emergency Microkernel\n"
    "TEXT Built-in emergency control path that does not require KMO modules.\n"
    "TEXT REAL16 is a BIOS 16-bit real-mode first responder for the emergency path.\n"
    "TEXT It supports PanicRoom bridging, media lockdown, reports, and user-confirmed key discard.\n"
    "TEXT It does not implement fan, thermal, or hardware-damaging self-destruct behavior.\n"
    "END\n";
static uint8_t ram_auxkernel_buf[AUXKERNEL_CAP];
static FsWritableFile ram_auxkernel = { "auxkernel.lardd", ram_auxkernel_buf, 0, AUXKERNEL_CAP };

#define FSDELETE_CAP 2048u
static const uint8_t fsdelete_init[] =
    "LARDD 1\n"
    "TITLE Seed/Default Delete Overlay\n"
    "TEXT DEL -F removes built-in seed/default files from the active filesystem; TOMB owns the records.\n"
    "SECTION Tombstones\n";
static uint8_t ram_fsdelete_buf[FSDELETE_CAP];
static FsWritableFile ram_fsdelete = { "fsdelete.lardd", ram_fsdelete_buf, 0, FSDELETE_CAP };

#define USERAPP_SYSRXE_CAP 2048u
static const uint8_t userapp_sysrxe_init[] =
    "SYSRXE 1\n"
    "ID userapp\n"
    "NAME User SYSRXE\n"
    "ICON U\n"
    "LAYOUT responsive\n"
    "COLOR 0xFF5DB7A6\n"
    "INPUT Say:\n"
    "BUTTON Echo\n"
    "USE APPKIT\n"
    "UI PANEL 0 0 190 36 User controls\n"
    "UI BUTTON 8 8 72 20 Echo | echo\n"
    "UI BADGE 120 9 60 18 LOCAL\n"
    "UI CUSTOM chip 190 8 96 20 USER MADE | echo\n"
    "UI INPUT 0 46 260 24 Say:\n"
    "UI OUTPUT 0 84 0 0 User output\n"
    "DESKTOP 1\n"
    "DOCK 0\n"
    "TEXT This writable app proves that new apps can be edited as files.\n"
    "TEXT Open userapp.sysrxe in Edit, change NAME/TEXT/COMMAND, then run sysrxe reload.\n"
    "COMMAND echo user-sysrxe\n";
static uint8_t ram_userapp_sysrxe_buf[USERAPP_SYSRXE_CAP];
static FsWritableFile ram_userapp_sysrxe = { "userapp.sysrxe", ram_userapp_sysrxe_buf, 0, USERAPP_SYSRXE_CAP };

#define KMODTALK_CAP 4096u
static const uint8_t kmodtalk_init[] =
    "LARDD 1\n"
    "TITLE KModTalk\n"
    "TEXT User-to-kernel-module direct messages are logged here.\n"
    "SECTION Messages\n";
static uint8_t ram_kmodtalk_buf[KMODTALK_CAP];
static FsWritableFile ram_kmodtalk = { "kmodtalk.lardd", ram_kmodtalk_buf, 0, KMODTALK_CAP };

#define USER_KMO_CAP 2048u
static const uint8_t user0_kmo_init[] =
    "KMO 1\n"
    "ID user-kmo\n"
    "NAME User KMO\n"
    "COMMAND userkmo\n"
    "TARGET boot\n"
    "HELP User-editable kernel module file. Change TARGET/DEFAULT/TEXT, then run kmo reload.\n"
    "DEFAULT status\n"
    "TEXT This writable .kmo proves kernel modules can be stored, edited, run, deleted, and bound as shell commands from native files.\n";
static uint8_t ram_user0_kmo_buf[USER_KMO_CAP];
static uint8_t ram_user1_kmo_buf[USER_KMO_CAP];
static uint8_t ram_user2_kmo_buf[USER_KMO_CAP];
static uint8_t ram_user3_kmo_buf[USER_KMO_CAP];
static FsWritableFile ram_user0_kmo = { "user0.kmo", ram_user0_kmo_buf, 0, USER_KMO_CAP };
static FsWritableFile ram_user1_kmo = { "user1.kmo", ram_user1_kmo_buf, 0, USER_KMO_CAP };
static FsWritableFile ram_user2_kmo = { "user2.kmo", ram_user2_kmo_buf, 0, USER_KMO_CAP };
static FsWritableFile ram_user3_kmo = { "user3.kmo", ram_user3_kmo_buf, 0, USER_KMO_CAP };

#define RXR_SLOT_CAP 4096u
static uint8_t ram_rxrslot0_buf[RXR_SLOT_CAP];
static uint8_t ram_rxrslot1_buf[RXR_SLOT_CAP];
static uint8_t ram_rxrslot2_buf[RXR_SLOT_CAP];
static uint8_t ram_rxrslot3_buf[RXR_SLOT_CAP];
static FsWritableFile ram_rxrslot0 = { "rxrslot0.dat", ram_rxrslot0_buf, 0, RXR_SLOT_CAP };
static FsWritableFile ram_rxrslot1 = { "rxrslot1.dat", ram_rxrslot1_buf, 0, RXR_SLOT_CAP };
static FsWritableFile ram_rxrslot2 = { "rxrslot2.dat", ram_rxrslot2_buf, 0, RXR_SLOT_CAP };
static FsWritableFile ram_rxrslot3 = { "rxrslot3.dat", ram_rxrslot3_buf, 0, RXR_SLOT_CAP };

#define FS_HIDDEN_READONLY_MAX 32u
static char s_hidden_readonly[FS_HIDDEN_READONLY_MAX][32];
static uint32_t s_hidden_readonly_count;
#define FS_DELETED_READONLY_MAX 32u
static char s_deleted_readonly[FS_DELETED_READONLY_MAX][32];
static uint32_t s_deleted_readonly_count;
static int s_fsdelete_rewriting;

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
    "p The Doc tab can switch HTTP requests between GET, POST, HEAD, PUT, PATCH, DELETE, and OPTIONS; POST/PUT/PATCH read the address as URL|body.\n"
    "link Web Stack Guide | webstack_guide.lardd\n"
    "link FSTWT Guide | fstwt_guide.lardd\n"
    "link Web Demo LARS | webdemo.lars\n"
    "fetch Example HEAD target | https://example.com/\n"
    "section Full-control starts\n"
    "li Run control in LSH for the system control map.\n"
    "li Run status to inspect version, storage, drivers, and containers.\n"
    "li Use magic before a command when LSH should predict and execute a mistyped safe command.\n"
    "li Use magic dryrun statsu to see the prediction without executing it.\n"
    "li Use magic -f bye or magic -f byebye only when you explicitly want Magic to force a raw-control command.\n"
    "li Use magic dryrun -f bye to preview a forced raw-control prediction without running it.\n"
    "li Use bye or byebye to sync RAM files and request a firmware/VM poweroff.\n"
    "li Use restart or reboot to sync RAM files and request a firmware/VM restart.\n"
    "li Use install status for the HDD/SSD installer preview, or install hdd yes / install ssd yes to write LardOS to the ATA target disk.\n"
    "li Use media list, media format Z, dir Z:, type Z:note.txt, and dir _: for merged storage.\n"
    "li Use auxkernel status, auxkernel real16, auxkernel lockdown confirm, or auxkernel keydrop confirm for the KMO-independent REAL16 emergency microkernel path.\n"
    "li Use fstwt status, fstwt fs, fstwt main name, fstwt use file.fstwts, fstwt to path, and fstwt from file to translate or virtualize file-system names.\n"
    "li Run mode probe to enter a controlled real16 window and return to long64.\n"
    "li Run mode guard to verify the bridge restores long64 after a real16 window.\n"
    "li Use sram on or sram rect x y w h to turn quiet screen pixels into scratch RAM.\n"
    "li Use renderfx aa none/antianti/basic/nonlinear, renderfx brightness, renderfx resize stretch/live, renderfx lsb, renderfx vblank, and renderfx subpx for user-owned display filtering.\n"
    "li Use renderfx subpx use displayfix.spfx to apply per-region R/G/B subpixel defect correction from an editable script.\n"
    "li Use wallpaper color 0xRRGGBB, wallpaper pattern grid/stripes/checker, or wallpaper bmp sample.bmp to set the desktop background from user-owned state.\n"
    "li Use Ctrl+Y, Ctrl+P, Ctrl+Space then 1..9/0, or megaclip status/list/mode/push/file/pull/write for the 10-slot MegaClipboard.\n"
    "li Use lconnect on, lconnect direct, lconnect discover, and lconnect share all on to share non-input resources with another LardOS machine over LAN.\n"
    "li Use secure key, secure seal, secure lock, secure ecc ram on, secure ecc storage on, and secure unlock KEY for optional user-owned encrypted-at-rest media stores with software ECC.\n"
    "li Use oslink status, ping, send, exec, recv, and peers for OS-to-OS messages and safe remote commands.\n"
    "li Use oslink emit channel text for LardOS-internal module messages.\n"
    "li Use kmod list and kmod gui/fs/task/oslink/boot/time/vm/sysrxe status to talk directly with kernel modules.\n"
    "li Use kmo list, kmo run user-kmo, kmo create mine.kmo gui status, kmo set mine.kmo text hello, and kmo delete mine.kmo for user-owned .kmo kernel module files.\n"
    "li Use kmo raw rawdoor.kmo sum or set RAW 1 / TARGET raw in a .kmo when you explicitly want dangerous raw-control behavior.\n"
    "li Use liveupdate apply hot.kmo KMO 1\\nID hot\\nCOMMAND hot\\nTARGET boot\\nDEFAULT status\\n to change file-owned code while LardOS is running.\n"
    "li Use ren old.txt new.txt, rename selected NewName, or the desktop Rename button to rename files, apps, and folders.\n"
    "li EXGUI and EXEXGUI were removed so the default GUI can become the single polished desktop surface.\n"
    "li Use cfgsh for the settings shell: awake on, ltheme night, wallpaper grid, http 7, boot 4.\n"
    "li Use dos on for L-DOS mode with _:/C:/A:/Z:/U:/R: mapping and DOS-style file commands.\n"
    "li In L-DOS, DEL -F file removes seed/default built-in files from the active filesystem through fsdelete.lardd, even if that breaks the OS.\n"
    "li Use bleed dryrun file to preview a last-resort delete sweep, or bleed file to try RAM, seed/default hard-delete, and media deletion routes.\n"
    "li Use bleed overflow file when you want a bounded overflow-style wipe of writable/media file slots before deletion.\n"
    "li RESTORE only removes soft TOMB HIDE records; use TOMB DROP file or TOMB CLEAR if you choose to delete hard-delete records too.\n"
    "li Use TOMB LIST, TOMB SHOW, TOMB HIDE file, TOMB DROP file, or TOMB CLEAR when you want to inspect or edit deletion records themselves.\n"
    "li Use buddy on for Lard Buddy, the optional roaming assistant with tips and loose jokes.\n"
    "li Use lguilib show default.lguilib or lguilib use default.lguilib to inspect/apply GUI library themes.\n"
    "li Use time, date, lunar, and dangun for LardOS Time ticks, five-digit years, Dangun year, and the native lunar view.\n"
    "li Use vm status, vm limits, and vm selftest to monitor BOSL, LIL, GASM, Lafillo VM, and OSVM under common step budgets.\n"
    "li Use shrine status, shrine list, shrine verify hello.shrine, and shrine run hello.shrine for the Lard Subsystem for Shrine with BOSL payload validation.\n"
    "li Use glyph demo, glyph auto sample.bmp avatar, glyph move/copy/rename/pixel, glyph live U+E000 on, glyph click U+E000, and glyph insert U+E000 notes.txt to own and edit clickable realtime private-use Unicode picture characters.\n"
    "li The default cursor is the pretty mouse picture at U+E004; use cursor mouse to restore it or cursor set U+E000 to choose another user-owned slot.\n"
    "li Use dir X: for seed/default files, dir R: for RAM, and dir _: for merged storage.\n"
    "li Use task list and task set id prio to inspect and change queued task priority.\n"
    "li Priority lev.10 is urgent work the user can grant with task urgent id, task set id 10, or nice 10 cmd.\n"
    "li Use tasktop to see runnable and paused task queues with priority bars.\n"
    "li Use bootprof set safe or bootprof set netoff to change the next boot profile.\n"
    "li Awakening mode is off by default; use awake on or awake off to choose the next boot path.\n"
    "li Use crashlog show to inspect panic and diagnostic history.\n"
    "li Use crash status, crash dryrun panic, crash log text, or crash panic text for deliberate OS inspection triggers.\n"
    "li Use crash ud2, crash div0, crash page, crash int3, or crash triple only when you explicitly want CPU fault/reset paths.\n"
    "li Use lpack verify sample.lpack before install, and lpack undo last to roll back the last install.\n"
    "li Use rxr verify sample.rxr, rxr list sample.rxr, and rxr install sample.rxr to install an app bundle with its files.\n"
    "li Use sysrxe list, sysrxe reload, sysrxe show userapp.sysrxe, and sysrxe run 0 text for file-defined system executables.\n"
    "li Use rxe list, rxe show demo_game.rxe, and rxe run 0 right to play the normal RXE demo game.\n"
    "li Use rxe show langdemo.rxe and rxe run 1 7 after rxe reload to try app-side C/LardOS language code.\n"
    "li Use kmod history to read kmodtalk.lardd after direct kernel-module messages.\n"
    "li Use screencheck retro for an old boot/storage-style visual screen scan.\n"
    "li Use bugeye scan to catch visible framebuffer/layout bugs and write bugreport.lardd.\n"
    "li Use bugreplay show to review the last BugEye screen-health frames.\n"
    "li Use bugreplay draw to draw the replay frames as a GUI panel.\n"
    "li Use trace on and trace show to inspect LardTrace module and shell events.\n"
    "li Use webstack status, webstack methods, webstack tls, webstack guide, and webstack demo to inspect the native LARS/HTTP/HTTPS stack.\n"
    "li Use netwatch on and netwatch show to inspect readable UDP, OSLink, and HTTP method events.\n"
    "li Use journal show to read the automatic LARDD system journal.\n"
    "li Use rollback snap and rollback last to save and restore user-visible settings, including wallpaper.\n"
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
    "li v1.58.1p hotpatches settings sliders, full-desktop window movement, and fullscreen/restore.\n"
    "li v1.59.0b adds runtime desktop/dock items, folders, draggable/reorderable launchers, per-app windows, and z-order.\n"
    "li v1.59.0a officially promotes the runtime desktop/window-manager model without restoring EXGUI/EXEXGUI.\n"
    "li v1.60.0a officially adds L-DOS mode as a native compatibility shell without external DOS code.\n"
    "li v1.60.1p hotpatches L-DOS DEL -F seed/default tombstones plus RESTORE for user-owned reversibility.\n"
    "li v1.61.0a officially adds user-owned tombstone deletion: TOMB LIST/SHOW/DROP/CLEAR plus DEL -T.\n"
    "li v1.62.0a makes DEL -F a hard delete from the active filesystem while preserving TOMB HIDE for soft tombstones.\n"
    "li v1.63.0a adds the in-OS HDD/SSD installer option using the native stage1/stage2/kernel layout.\n"
    "li v1.63.1p hotpatches the VirtualBox black-screen boot memory layout while preserving the installer feature.\n"
    "li v1.64.0b adds SYSRXE so future simple GUI apps can be described as .sysrxe files.\n"
    "li v1.65.0b adds KModTalk so users can talk directly with kernel modules and audit kmodtalk.lardd.\n"
    "li v1.65.1p hotpatches renaming for writable files, desktop apps, and folders.\n"
    "li v1.66.0b adds KMO, a native .kmo kernel module file format with user create/set/delete/run commands.\n"
    "li v1.66.1b adds explicit KMO raw-control mode so users can choose the dangerous path too.\n"
    "li v1.66.1a officially promotes KMO raw-control without feature loss or value changes.\n"
    "li v1.67.0b adds early RXE game support.\n"
    "li v1.67.1p separates SYSRXE system executables from normal RXE executables and moves the demo game to demo_game.rxe.\n"
    "li v1.67.1a officially promotes the RXE/SYSRXE split without feature loss or value changes.\n"
    "li v1.67.2p hotpatches VirtualBox optical-drive booting with ISO-specific CHS fallback and register-safe stage2 progress output.\n"
    "li v1.67.2a officially promotes the VirtualBox boot hotpatch without feature loss or value changes.\n"
    "li v1.68.0a officially adds APPKIT file-defined/runtime app UI, custom widgets, barcode Unicode fallback, and tail-less cursors without feature loss.\n"
    "li v1.69.0a officially lets RXE/SYSRXE apps carry LANG/CODE for LSH, LIL, GASM, BOSL, LAFILLO, OSVM, C, and LML.\n"
    "li v1.70.0a officially adds RXR app bundles for RXE/SYSRXE apps and required files.\n"
    "li v1.71.0b adds MediaFS/MDFS device stores for SSD/HDD, USB-style, and Y:/F: floppy-style open/save paths.\n"
    "li v1.71.1p hotpatches drive policy: X: main, Y:/F: floppy, Z:/S: auxiliary, A:/U: first extra media, and R: RAM.\n"
    "li v1.71.2a officially makes DRFL 2 .drfl files carry editable driver CODE and adds drivers show for in-OS inspection.\n"
    "li v1.72.0b lets .kmo files bind COMMAND names so new shell commands can live as module files instead of LSH branches.\n"
    "li v1.72.0a officially promotes KMO shell-command bindings without feature loss or philosophy changes.\n"
    "li v1.93.0b adds SPFX subpixel display-defect filter scripts through renderfx subpx and writable displayfix.spfx.\n"
    "li v1.95.0a adds AuxKernel, a tiny KMO-independent emergency microkernel path for PanicRoom bridge, lockdown, reports, and keydrop containment.\n"
    "li v2.0.5b corrects AuxKernel to a REAL16 profile: a BIOS 16-bit real-mode first responder, with real8 kept as a visible compatibility alias.\n"
    "li v2.0.4b introduced the AuxKernel real-mode bridge probe; v2.0.5b supersedes its mistaken REAL8 wording.\n"
    "li v1.96.0b adds software ECC placement controls: secure ecc ram on/off and secure ecc storage on/off.\n"
    "li v1.97.0b adds MegaClipboard: 10 slots, stack/single/order modes, commands, and Ctrl+Y/Ctrl+P/Ctrl+Space slot pull shortcuts.\n"
    "li v1.94.0a officially promotes LardSec/LardLocker media sealing and keeps POST selftests from changing user-visible security counters.\n"
    "li v1.94.0b adds optional LardSec/LardLocker at-rest media sealing with visible recovery keys and ECC.\n"
    "li v1.92.1a officially promotes the native WebStack method/TLS line without feature loss or value changes.\n"
    "li v1.92.1p makes HTTPS visible with webstack tls, LardTLS info, and POST/selftest TLS checks while preserving all v1.92 methods.\n"
    "li v1.92.0b expands HTTP/HTTPS to GET, POST, HEAD, PUT, PATCH, DELETE, and OPTIONS without external web libraries.\n"
    "li v1.91.1p hotpatches GUI resize hit-testing so only the visible bottom-right grip starts window resizing.\n"
    "li v1.91.0a officially promotes user-owned desktop wallpaper settings without feature loss or value changes.\n"
    "li v1.91.0b adds user-owned desktop wallpaper settings through wallpaper.lardd, wallpaper color/pattern/bmp, cfgsh, rollback, and LiveUpdate reload paths.\n"
    "li v1.90.0b lets RXE/SYSRXE apps declare RESIZE fixed and LAYOUTSIZE so APPKIT does not recalculate layout on window-size changes unless the app wants reflow.\n"
    "li v1.89.0a officially promotes the v1.88 GUI stability line: stable stretch resize is default, live reflow remains available, and user control is preserved.\n"
    "li v1.88.2p adds stable stretch resize mode: renderfx resize stretch previews corner resize by stretching the current window image, while resize live keeps old reflow.\n"
    "li v1.88.1p hotpatches desktop windows: inactive windows render from saved app state, APPKIT responsive labels reserve space, and corners resize windows.\n"
    "li v1.88.0b centralizes the active release version in os/VERSION and generates kernel/build version data from it.\n"
    "li v1.87.0b adds LiveUpdate runtime file/code apply for writable overlays, KMO/RXE/SYSRXE reloads, drivers, FSTWT, themes, and future auto-update plumbing.\n"
    "li v1.86.0b adds bootmeta growth readiness: stage2 exposes kernel size, image capacity, high-copy address, and free boot headroom.\n"
    "li v1.85.0b adds APPKIT responsive UI layout and smarter desktop icon placement for bundled/user apps.\n"
    "li v1.84.1p hotpatches PS/2 mouse batching so pointer motion stays responsive under heavier GUI work.\n"
    "li v1.84.0b adds crash, a raw-control diagnostic command for deliberate PanicRoom and CPU fault testing.\n"
    "li v1.83.1b adds bleed overflow, a bounded in-slot wipe-before-delete option for stubborn broken files.\n"
    "li v1.83.0b adds bleed, a last-resort visible delete sweep for broken files across RAM, seed/default delete overlays, and media stores.\n"
    "li v1.82.0b extends FSTWT with MAIN/SUB filesystem virtualization and coexisting namespaces.\n"
    "li v1.81.0b adds FSTWT live two-way filesystem translation scripts before RXR/vpath fallback.\n"
    "li v1.80.0b adds RenderFX beta display modes: no-AA default, antianti/basic/nonlinear AA, multiplicative brightness, ScreenRAM LSB, and VBlank sync.\n"
    "li v1.79.1p hotpatches the SSAV generator so frame data begins at the documented 0x10 offset.\n"
    "li v1.79.0a adds high-memory boot staging: low memory is only a bounce buffer and the full boot image lives at 0x01000000.\n"
    "li v1.78.0a expands boot staging: kernel 0x2000, bootinfo 0x1000, stacks below staging.\n"
    "li v1.77.0a officially promotes _: merged storage from v1.77.0b.\n"
    "li v1.76.1p hotpatches the VirtualBox blank-screen boot layout while preserving v1.76 features.\n"
    "li v1.76.0a officially promotes OS virtual paths without feature loss or philosophy changes.\n"
    "li v1.76.0b generalizes OS virtual paths: folder/inside/address resolves through the kernel FS layer.\n"
    "li v1.75.1b makes rxr/file an OS filesystem namespace path, so the kernel FS layer owns RXR path resolution.\n"
    "li v1.75.0b adds RXR bundle-internal paths for app bundle dependency files.\n"
    "li v1.74.1p removes the site-specific video-view beta surface and keeps WebStack generic: LARS link/fetch plus HTTP/HTTPS GET/POST/HEAD.\n"
    "li Use lunit run tests.lunit for small native feature tests.\n"
    "li Use oschat say text for local OSLink chat-style module messages.\n"
    "li Use larsview open lardos.lars, larsapp form lardos.lars, and notes add text for native document/app browsing and notes.lardd.\n"
    "li LARSView now supports reload, back, and actions; notes add updates both notes.lardd and GUI notes.txt.\n"
    "button System status | status\n"
    "button Task dashboard | tasktop\n"
    "button Crash history | crashlog show\n"
    "input profile normal\n"
    "li Press P during boot for POST, M for the CPU Mode Bridge Test, or I for the HDD/SSD installer.\n"
    "li Use write notes.txt text and append notes.txt text for the RAM FS.\n"
    "li Use vcs status/log/show to inspect the in-OS history layer.\n"
    "li Use lcnt info to inspect syscall-cap containers.\n"
    "li Run lil features.lil to try the native LIL scripting language.\n"
    "li Use sum, peek, poke, and asm_ when you want raw ring-0 control.\n"
    "cmd release\n"
    "cmd install status\n"
    "cmd media list\n"
    "cmd dir _:\n"
    "cmd dir Z:\n"
    "cmd dos status\n"
    "cmd dos map\n"
    "cmd dos test\n"
    "cmd dos run mem\n"
    "cmd magic statsu\n"
    "cmd magic dryrun statsu\n"
    "cmd mode probe\n"
    "cmd mode guard\n"
    "cmd sram on\n"
    "cmd oslink status\n"
    "cmd oslink emit shell hello-from-lardos\n"
    "cmd cfgsh status\n"
    "cmd cfg ltheme night\n"
    "cmd cfg http 7\n"
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
    "cmd rxr verify sample.rxr\n"
    "cmd rxr list sample.rxr\n"
    "cmd rxr path rxr/rxr_data.txt\n"
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
    "cmd lardd lts.lardd\n"
    "cmd lardd office_guide.lardd\n"
    "cmd rxe show lardwrite.rxe\n"
    "cmd rxe show lardsheet.rxe\n"
    "cmd rxe show lardshow.rxe\n"
    "cmd release lts\n"
    "cmd lardd dosmode_guide.lardd\n"
    "note Release suffixes: a=official, b=beta-experimental, p=hotpatch. LTS codenames append after the suffix, like v1.99.0a-tiara.\n"
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
    "WIDGET toggle action\n"
    "WIDGET slider value\n"
    "WIDGET progress value\n"
    "WIDGET badge status\n"
    "WIDGET icon action\n"
    "WIDGET tile launcher\n"
    "WIDGET input text\n"
    "WIDGET output frame\n"
    "WIDGET list scroll\n"
    "WIDGET separator line\n"
    "WIDGET status badge\n"
    "WIDGET responsive flow\n"
    "WIDGET smartgrid placement\n"
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

static const uint8_t file_lts_lardd[] =
    "LARDD 1\n"
    "TITLE LardOS LTS\n"
    "TEXT Current LTS: Tiara.\n"
    "TEXT Active version: v1.99.0a-tiara.\n"
    "TEXT Only one LTS line is active at a time; when the next LTS ships, the old LTS support line ends.\n"
    "TEXT Next planned LTS codename: Mirage.\n"
    "TEXT LTS means long-support promotion, not feature removal or value changes.\n"
    "SECTION Values\n"
    "ITEM User ownership, raw-control visibility, native formats, recovery paths, and keyboard completeness remain part of the supported surface.\n"
    "ITEM Deprecated raw-control paths may exist, but they must remain confirm-gated and auditable.\n"
    "END\n";

static const uint8_t file_office_guide[] =
    "LARDD 1\n"
    "TITLE LardOS Office Apps\n"
    "TEXT LardOS includes three native office-style RXE apps without external office libraries.\n"
    "TEXT The apps stay file-defined RXE programs; the OS supplies reusable native formats and commands.\n"
    "SECTION Apps\n"
    "ITEM LardWrite opens lardwrite.rxe and edits TITLE, SECTION, TEXT, ITEM, QUOTE, and CODE records in office_doc.lardd.\n"
    "ITEM LardSheet opens lardsheet.rxe and edits COL, ROW, CELL, and FORMULA records in office_sheet.lsheet.\n"
    "ITEM LardShow opens lardshow.rxe and edits TITLE, THEME, SLIDE, and NOTE records in office_deck.lshow.\n"
    "SECTION Shell\n"
    "ITEM lword show | add text | title text | section text | bullet text | quote text | code text | find text | stats | new\n"
    "ITEM lsheet show | add label value | cell A1 42 | formula total sum | col name value | csv | sum | find text | new\n"
    "ITEM lshow show | add title | body | play | next | prev | slide N | theme name | note text | new\n"
    "TEXT These are starter replacements for word processor, spreadsheet, and presentation workflows while keeping files native and user-editable.\n"
    "END\n";

static const uint8_t file_glyph_guide[] =
    "LARDD 1\n"
    "TITLE Image Glyphs\n"
    "TEXT LardOS can bind user pictures to unused private-use Unicode codepoints.\n"
    "TEXT Inline text keeps an 8x8 cell so notes, LARS views, and shell output can render them without external libraries.\n"
    "TEXT If a GUI font cannot draw a Unicode character, LardOS renders a narrow grayscale barcode cell instead of losing it as '?'.\n"
    "TEXT The six inner vertical bars encode the codepoint nibbles, so unknown Hangul and future scripts still leave their identity on screen.\n"
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

static const uint8_t file_dosmode_guide[] =
    "LARDD 1\n"
    "TITLE L-DOS Mode\n"
    "TEXT L-DOS mode is a LardOS-native DOS-style compatibility shell layer.\n"
    "TEXT It does not import external DOS code; it maps familiar commands onto LSH, LFS, RAM files, LPST, and LARDD logs.\n"
    "SECTION Commands\n"
    "ITEM dos on -> enter L-DOS mode with an L-DOS C:\\ prompt.\n"
    "ITEM dos off or EXIT -> leave L-DOS mode.\n"
    "ITEM DOS commands are case-insensitive: DIR, TYPE, COPY, DEL, REN, MD, RD, CD, CLS, VER, SET, ECHO, MEM.\n"
    "ITEM DEL -F file -> remove a seed/default built-in or LFS file from the active filesystem using fsdelete.lardd DELETE records.\n"
    "ITEM RESTORE file or UNDELETE file -> remove only a soft TOMB HIDE record; hard deletes remain deleted.\n"
    "ITEM TOMB LIST or TOMB SHOW -> inspect active soft tombstones, hard deletes, or the raw fsdelete.lardd log.\n"
    "ITEM TOMB HIDE file -> create the old reversible soft tombstone without using DEL -F.\n"
    "ITEM TOMB DROP file or DEL -T file -> delete one soft/hard record and make that seed/default file visible.\n"
    "ITEM TOMB CLEAR -> delete every soft/hard record because the user can own even the deletion overlay.\n"
    "ITEM LSH command -> run one native LardOS command while staying in DOS mode.\n"
    "ITEM dos map -> show _:/C:/A:/Z:/U:/R: drive mappings.\n"
    "ITEM dos log -> read dosmode.lardd history.\n"
    "SECTION Drive Map\n"
    "ITEM C: maps to LardOS X: built-in and LFS files.\n"
    "ITEM A: maps to LardOS Y: floppy-style MDFS media store.\n"
    "ITEM Z: maps to LardOS Z: auxiliary SSD/HDD MDFS media store.\n"
    "ITEM U: maps to LardOS A: first extra USB-style MDFS media store.\n"
    "ITEM S: remains a compatibility alias for LardOS Z: auxiliary media.\n"
    "ITEM R: maps to LardOS R: writable RAM files.\n"
    "ITEM _: maps to LardOS _: merged R:/X:/Y:/Z:/A: root.\n"
    "SECTION Philosophy\n"
    "ITEM Directories are virtual navigation labels because LardOS currently keeps the core filesystem flat and visible.\n"
    "ITEM DEL clears writable RAM file contents; DEL -F removes seed/default embedded files from the active filesystem.\n"
    "ITEM REN moves data into an existing writable slot instead of hiding a mutable filename table.\n"
    "ITEM fsdelete.lardd keeps HIDE, SHOW, and DELETE records so force deletes are inspectable and persisted by sync.\n"
    "ITEM TOMB rewrites fsdelete.lardd on user request, preserving the LardOS rule that visible system state remains user-editable.\n"
    "END\n";

static const uint8_t file_installer_guide[] =
    "LARDD 1\n"
    "TITLE HDD/SSD Installer\n"
    "TEXT LardOS can install itself to the primary ATA HDD/SSD target from inside the OS.\n"
    "TEXT The installer is destructive by design: it writes the boot sectors the user chooses to own.\n"
    "SECTION Commands\n"
    "ITEM install status -> show driver, detected sectors, stage layout, kernel size, and the exact destructive command.\n"
    "ITEM install preview -> same as status; no sectors are written.\n"
    "ITEM install hdd yes -> write stage1 to LBA0, stage2 to LBA1..8, and the loaded kernel image to LBA9..\n"
    "ITEM install ssd yes -> same target path, named for users thinking in SSD terms.\n"
    "SECTION Boot Option\n"
    "ITEM Press I at the power-on options screen to open the installer preview before the normal GUI starts.\n"
    "ITEM From that installer screen, Y writes the target disk and N returns to normal boot.\n"
    "SECTION Layout\n"
    "ITEM LBA0 contains LardOS stage1.\n"
    "ITEM LBA1..8 contain stage2.\n"
    "ITEM LBA9.. contain the current loaded LARDX/BOSX kernel image from memory.\n"
    "ITEM LPST writable storage remains reserved at LBA2752 and beyond.\n"
    "END\n";

static const uint8_t file_media_guide[] =
    "LARDD 1\n"
    "TITLE MediaFS Device Stores\n"
    "TEXT MediaFS gives LardOS native open/save paths for SSD/HDD, USB-style, and floppy-style media without external libraries.\n"
    "TEXT The on-device format is MDFS: a tiny visible record table plus fixed file slots written by in-tree C.\n"
    "SECTION Drives\n"
    "ITEM Z: and S: auxiliary SSD/HDD native media store, backed by ATA sectors after the boot image when available.\n"
    "ITEM A: and U: first extra USB-style removable media store, exposed through the same block path until a full USB controller stack exists.\n"
    "ITEM Y: and F: floppy-style media store. On a 1.44M boot image it reports RAM fallback because there are no spare sectors.\n"
    "SECTION Commands\n"
    "ITEM media list -> show each store, persistence, backing driver, LBA range, files, bytes, and dirty state.\n"
    "ITEM media format Z -> create an empty auxiliary MDFS store.\n"
    "ITEM media write Z note.txt hello -> save a file into the auxiliary media store.\n"
    "ITEM media append Z note.txt more -> append text.\n"
    "ITEM media sync all -> write persistent stores to backing sectors.\n"
    "ITEM dir Z: and type Z:note.txt -> open media files from normal LSH.\n"
    "ITEM dir _: -> show RAM, main files, and media stores together.\n"
    "ITEM copy R:notes.txt A:notes.txt -> copy RAM data to a device store.\n"
    "ITEM In L-DOS, _: is merged root, A:=Y:, Z:=Z:, U:=A:, and S: aliases Z:.\n"
    "SECTION Honesty\n"
    "ITEM This is not yet a full FAT/USB-MSC/FDC driver stack. It is a LardOS-native device-store layer on detected block storage.\n"
    "ITEM If no backing sectors exist, the store remains usable as RAM fallback and says so openly.\n"
    "END\n";

static const uint8_t file_webstack_guide[] =
    "LARDD 1\n"
    "TITLE LardOS WebStack\n"
    "TEXT The web stack is native LardOS code: HTTP request building, HTTPS transport, LARS documents, and NetWatch all live inside the tree.\n"
    "TEXT No external browser engine or package manager is required.\n"
    "SECTION Methods\n"
    "ITEM cfgsh http 1 -> GET mode.\n"
    "ITEM cfgsh http 2 -> POST mode. The Doc URL field accepts URL|body and sends application/x-www-form-urlencoded.\n"
    "ITEM cfgsh http 3 -> HEAD mode. It asks for headers without a body and is useful for checking status, redirects, and server metadata.\n"
    "ITEM cfgsh http 4 -> PUT mode. It uses URL|body for replace/create style requests.\n"
    "ITEM cfgsh http 5 -> PATCH mode. It uses URL|body for partial update style requests.\n"
    "ITEM cfgsh http 6 -> DELETE mode. It asks a server to remove a resource without sending a request body.\n"
    "ITEM cfgsh http 7 -> OPTIONS mode. It asks a server which methods or features it supports.\n"
    "SECTION HTTPS/TLS\n"
    "ITEM HTTPS uses the same request builder as HTTP, so GET, POST, HEAD, PUT, PATCH, DELETE, and OPTIONS stay consistent.\n"
    "ITEM LardTLS is native in-tree code: TLS 1.2 client path, SNI, native trust anchors, and RSA/AES-CBC suites.\n"
    "ITEM webstack tls shows the trust-anchor count, RSA limit, SNI limit, supported cipher names, and the TLS selftest.\n"
    "ITEM Unsupported modern-only sites should report the TLS error instead of pretending the OS has a hidden external browser.\n"
    "SECTION LARS Records\n"
    "ITEM link Label | file.lars opens a local native document through larsact.\n"
    "ITEM link Label | https://host/path records a network target for the Doc tab.\n"
    "ITEM fetch Label | https://host/path marks a network fetch target without hiding the URL from the user.\n"
    "ITEM button Label | command still runs explicit shell commands.\n"
    "ITEM input name value keeps local app fields native to LARS.\n"
    "SECTION Commands\n"
    "ITEM webstack status -> show method, document records, HTTPS summary, and request-builder/TLS selftests.\n"
    "ITEM webstack methods -> show the seven native HTTP/HTTPS methods and which ones carry URL|body.\n"
    "ITEM webstack tls -> show native HTTPS/TLS support details.\n"
    "ITEM webstack guide -> open this LARDD guide.\n"
    "ITEM webstack demo -> open webdemo.lars.\n"
    "ITEM larsform webdemo.lars -> list link/fetch/button/input actions.\n"
    "ITEM larsact webdemo.lars 0 -> open the local linked guide.\n"
    "ITEM netwatch on and netwatch show -> inspect readable HTTP/HTTPS method events.\n"
    "SECTION Values\n"
    "ITEM The user can see and change the method, target, local document records, and raw command surface.\n"
    "ITEM HTTPS details are visible because encrypted transport should still be explainable and locally owned.\n"
    "ITEM PUT, PATCH, DELETE, and OPTIONS do not replace GET, POST, or HEAD; they expand the surface without feature loss.\n"
    "END\n";

static const uint8_t file_webdemo_lars[] =
    "LARS 1\n"
    "title WebStack Demo\n"
    "p LARS now carries local links, network fetch targets, buttons, and inputs as native records.\n"
    "link Open guide | webstack_guide.lardd\n"
    "link Local control room | lardos.lars\n"
    "fetch Example headers | https://example.com/\n"
    "button Show status | webstack status\n"
    "input query lardos\n"
    "end\n";

static const uint8_t file_default_fstwts[] =
    "FSTWTS 1\n"
    "MODE HYBRID\n"
    "MAIN lardos ROOT TRANSLATE\n"
    "SUB sandbox sbx_ VM\n"
    "# File System Two Way Translator default map and virtual filesystem table.\n"
    "# MAP external-prefix <=> lardos-prefix\n"
    "MAP fstwt/demo/ <=> f2wdemo_\n";

static const uint8_t file_fstwt_guide[] =
    "LARDD 1\n"
    "TITLE FSTWT File System Two Way Translator\n"
    "TEXT FSTWT lets a file, RXR bundle, media drive, or user script carry path metadata that maps a friendly filesystem view to LardOS flat file names and back again.\n"
    "TEXT It can run as a translator or as a small filesystem virtualization layer where main and sub filesystems coexist through explicit namespaces.\n"
    "TEXT It is not a replacement for RXR, vpath, mediafs, or the merged _ drive. If no rule matches, the old path behavior continues.\n"
    "SECTION Script\n"
    "ITEM FSTWTS 1\n"
    "ITEM MODE TRANSLATE, MODE HYBRID, or MODE VM chooses whether MAP rules, VM-style filesystems, or both are active.\n"
    "ITEM MAIN name prefix VM sets the default filesystem namespace for unqualified paths. MAIN lardos ROOT TRANSLATE keeps the classic LardOS root.\n"
    "ITEM SUB name prefix VM adds a coexisting virtual filesystem reached with name:/path.\n"
    "ITEM MAP external-prefix <=> lardos-prefix\n"
    "ITEM Example: MAP app:/save/ <=> appsave_\n"
    "ITEM Example: SUB sandbox sbx_ VM makes sandbox:/notes/today.txt resolve to sbx_notes_today.txt.\n"
    "ITEM Calling app:/save/Slot 1.lardd writes or opens appsave_slot_1.lardd.\n"
    "ITEM Calling fstwt from appsave_slot_1.lardd shows app:/save/slot_1.lardd.\n"
    "SECTION Commands\n"
    "ITEM fstwt status - show active script, rule count, hits, and source.\n"
    "ITEM fstwt show - print the live script currently used by the module.\n"
    "ITEM fstwt use file.fstwts - load a script from X:, R:, Y:, Z:, A:, _:, or RXR-style paths.\n"
    "ITEM fstwt clear - disable translation until another script is loaded.\n"
    "ITEM fstwt to friendly/path - preview friendly namespace to LardOS filename.\n"
    "ITEM fstwt from lard_filename - preview LardOS filename to friendly namespace.\n"
    "ITEM fstwt fs - list declared main/sub filesystems.\n"
    "ITEM fstwt main name - choose a declared MAIN filesystem at runtime; fstwt main reset returns to the script default.\n"
    "ITEM fstwt sample - print a small starter script.\n"
    "SECTION Ownership\n"
    "ITEM fstwt.fstwts is writable so the user can edit the active mapping policy.\n"
    "ITEM default.fstwts is seed/default reference material; users can overlay it and load the default again if they want.\n"
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

static const uint8_t file_rxr_guide[] =
    "LARDD 1\n"
    "TITLE RXR App Bundle Format\n"
    "TEXT RXR bundles one RXE/SYSRXE app with the files it needs, without an external package manager.\n"
    "TEXT It is for apps and app dependencies; LPACK remains the broader package/install format.\n"
    "SECTION Syntax\n"
    "ITEM RXR 1\n"
    "ITEM NAME bundle-name\n"
    "ITEM APP primary.rxe or primary.sysrxe\n"
    "ITEM FILE name starts a bundled file body.\n"
    "ITEM ENDFILE ends the file body.\n"
    "ITEM END ends the bundle.\n"
    "SECTION Commands\n"
    "ITEM rxr info sample.rxr\n"
    "ITEM rxr list sample.rxr\n"
    "ITEM rxr verify sample.rxr\n"
    "ITEM rxr install sample.rxr\n"
    "ITEM rxr path rxr/rxr_data.txt\n"
    "ITEM rxr undo last\n"
    "SECTION Values\n"
    "ITEM RXR install writes normal user-owned files, then reloads RXE/SYSRXE apps.\n"
    "ITEM LardOS treats rxr/name as an OS filesystem namespace path, for example type rxr/data.txt or open(\"rxr/data.txt\").\n"
    "ITEM The kernel FS layer resolves rxr/name to the installed target before seed/default, writable, create, and rename operations.\n"
    "ITEM The app code stays inside the .rxe/.sysrxe file and can use LardOS languages or C-style app code.\n"
    "ITEM Undo restores the last RXR snapshot and releases newly created RXR slots when possible.\n"
    "END\n";

static const uint8_t file_sample_rxr[] =
    "RXR 1\n"
    "NAME language-demo-bundle\n"
    "APP rxr_app.rxe\n"
    "FILE rxr_app.rxe\n"
    "RXE 1\n"
    "ID rxr-app\n"
    "NAME RXR App\n"
    "ICON R\n"
    "LAYOUT responsive\n"
    "COLOR 0xFF74C7A2\n"
    "INPUT Number:\n"
    "BUTTON Run\n"
    "USE APPKIT\n"
    "UI PANEL 0 0 220 36 RXR bundled app\n"
    "UI BUTTON 8 8 72 20 Run | 7\n"
    "UI BADGE 118 9 70 18 RXR\n"
    "UI INPUT 0 46 220 24 Number:\n"
    "UI OUTPUT 0 84 0 0 Bundle output\n"
    "DESKTOP 1\n"
    "DOCK 1\n"
    "TEXT This app was installed from sample.rxr and reads its data through rxr/rxr_data.txt.\n"
    "LANG C\n"
    "CODE println(input + 70);\n"
    "CODE lsh(\"type rxr/rxr_data.txt\");\n"
    "ENDFILE\n"
    "FILE rxr_data.txt\n"
    "RXR dependency file installed with the app.\n"
    "ENDFILE\n"
    "END\n";

static const uint8_t file_sysrxe_guide[] =
    "LARDD 1\n"
    "TITLE SYSRXE App Format\n"
    "TEXT SYSRXE is the System RXE app format for LardOS GUI apps.\n"
    "TEXT It avoids hardcoding one C branch per future app.\n"
    "SECTION Syntax\n"
    "ITEM SYSRXE 1\n"
    "ITEM ID app-id\n"
    "ITEM NAME App title\n"
    "ITEM ICON one-to-three chars\n"
    "ITEM LAYOUT auto, panel, document, terminal, game, editor, package, responsive, smart, flow, smartui, or autofit\n"
    "ITEM RESIZE reflow is the default; RESIZE fixed/freeze/no-reflow keeps APPKIT layout from recalculating when the window size changes.\n"
    "ITEM LAYOUTSIZE width height, BASESIZE, or DESIGNSIZE chooses the fixed design canvas for RESIZE fixed.\n"
    "ITEM USE APPKIT declares the built-in app GUI widget library.\n"
    "ITEM LAYOUT responsive enables smart APPKIT rows: widgets keep author order, wrap instead of overlap, and OUTPUT/LIST fill remaining space.\n"
    "ITEM UI PANEL x y w h title\n"
    "ITEM UI LABEL x y w h text\n"
    "ITEM UI BUTTON x y w h label | action\n"
    "ITEM UI INPUT x y w h label\n"
    "ITEM UI OUTPUT x y w h label\n"
    "ITEM UI STATUS x y w h text\n"
    "ITEM UI TOGGLE x y w h label | action\n"
    "ITEM UI SLIDER x y w h label | percent\n"
    "ITEM UI PROGRESS x y w h label | percent\n"
    "ITEM UI BADGE x y w h text\n"
    "ITEM UI ICON x y w h mark | action\n"
    "ITEM UI TILE x y w h title | action\n"
    "ITEM UI CUSTOM style x y w h text | action\n"
    "ITEM UI MissingName x y w h text | action becomes a custom widget named MissingName.\n"
    "ITEM UI SEPARATOR x y w h\n"
    "ITEM App output can create UI at runtime with APPKIT UI ... lines.\n"
    "ITEM App output APPKIT CLEAR removes current widgets before rebuilding.\n"
    "ITEM App output APPKIT COLOR 0xAARRGGBB changes the color for later runtime widgets.\n"
    "ITEM App output APPKIT LAYOUT responsive switches the live app UI to smart responsive layout.\n"
    "ITEM App output APPKIT RESIZE fixed or APPKIT RESIZE reflow changes live layout-size behavior.\n"
    "ITEM App output APPKIT LAYOUTSIZE 640 420 changes the fixed design canvas.\n"
    "ITEM COLOR 0xAARRGGBB\n"
    "ITEM INPUT label\n"
    "ITEM BUTTON label\n"
    "ITEM LANG LSH, LIL, GASM, BOSL, LAFILLO, OSVM, C, or LML selects the app code runner.\n"
    "ITEM CODE source-line appends code for the selected language.\n"
    "ITEM LANG C uses the in-kernel C-style app runner: int vars, expressions, print/println, appkit(\"UI ...\"), lsh(\"command\"), and return.\n"
    "ITEM DESKTOP 1 and DOCK 1 decide launcher placement.\n"
    "ITEM TEXT app body line\n"
    "ITEM COMMAND lsh-command optionally receives textbox input when LANG is LSH.\n"
    "SECTION Commands\n"
    "ITEM sysrxe list\n"
    "ITEM sysrxe reload\n"
    "ITEM sysrxe show userapp.sysrxe\n"
    "ITEM edit userapp.sysrxe, save, then sysrxe reload\n"
    "END\n";

static const uint8_t file_rxe_guide[] =
    "LARDD 1\n"
    "TITLE RXE Executable Format\n"
    "TEXT RXE is the normal LardOS executable/app file format. SYSRXE is reserved for system executables.\n"
    "TEXT RXE files use the same native parser family but live under .rxe and the rxe command.\n"
    "SECTION Syntax\n"
    "ITEM RXE 1\n"
    "ITEM ID app-id\n"
    "ITEM NAME App title\n"
    "ITEM ICON one-to-three chars\n"
    "ITEM LAYOUT auto, panel, document, terminal, game, editor, package, responsive, smart, flow, smartui, or autofit\n"
    "ITEM RESIZE reflow is the default; RESIZE fixed/freeze/no-reflow keeps APPKIT layout from recalculating when the window size changes.\n"
    "ITEM LAYOUTSIZE width height, BASESIZE, or DESIGNSIZE chooses the fixed design canvas for RESIZE fixed.\n"
    "ITEM USE APPKIT declares the built-in app GUI widget library.\n"
    "ITEM LAYOUT responsive enables smart APPKIT rows: widgets keep author order, wrap instead of overlap, and OUTPUT/LIST fill remaining space.\n"
    "ITEM UI PANEL x y w h title\n"
    "ITEM UI LABEL x y w h text\n"
    "ITEM UI BUTTON x y w h label | action\n"
    "ITEM UI INPUT x y w h label\n"
    "ITEM UI OUTPUT x y w h label\n"
    "ITEM UI STATUS x y w h text\n"
    "ITEM UI TOGGLE x y w h label | action\n"
    "ITEM UI SLIDER x y w h label | percent\n"
    "ITEM UI PROGRESS x y w h label | percent\n"
    "ITEM UI BADGE x y w h text\n"
    "ITEM UI ICON x y w h mark | action\n"
    "ITEM UI TILE x y w h title | action\n"
    "ITEM UI CUSTOM style x y w h text | action\n"
    "ITEM UI MissingName x y w h text | action becomes a custom widget named MissingName.\n"
    "ITEM UI SEPARATOR x y w h\n"
    "ITEM App output can create UI at runtime with APPKIT UI ... lines.\n"
    "ITEM App output APPKIT CLEAR removes current widgets before rebuilding.\n"
    "ITEM App output APPKIT COLOR 0xAARRGGBB changes the color for later runtime widgets.\n"
    "ITEM App output APPKIT LAYOUT responsive switches the live executable UI to smart responsive layout.\n"
    "ITEM App output APPKIT RESIZE fixed or APPKIT RESIZE reflow changes live layout-size behavior.\n"
    "ITEM App output APPKIT LAYOUTSIZE 640 420 changes the fixed design canvas.\n"
    "ITEM COLOR 0xAARRGGBB\n"
    "ITEM INPUT label\n"
    "ITEM BUTTON label\n"
    "ITEM LANG LSH, LIL, GASM, BOSL, LAFILLO, OSVM, C, or LML selects the executable code runner.\n"
    "ITEM CODE source-line appends code for the selected language.\n"
    "ITEM LANG C uses the in-kernel C-style app runner: int vars, expressions, print/println, appkit(\"UI ...\"), lsh(\"command\"), and return.\n"
    "ITEM DESKTOP 1 and DOCK 1 decide launcher placement.\n"
    "ITEM TYPE GAME turns the executable into a native RXE game.\n"
    "ITEM BOARD width height declares the game map size, up to 24x12.\n"
    "ITEM ROW map-line adds # walls, . floor, @ start, and G goal.\n"
    "ITEM TEXT executable body line\n"
    "ITEM COMMAND lsh-command optionally receives textbox input for LANG LSH text executables.\n"
    "SECTION Commands\n"
    "ITEM rxe list\n"
    "ITEM rxe reload\n"
    "ITEM rxe show demo_game.rxe\n"
    "ITEM rxe run 0 right\n"
    "ITEM rxe show langdemo.rxe\n"
    "ITEM rxe run 1 7\n"
    "ITEM rxe run 0 reset\n"
    "END\n";

static const uint8_t file_kmodtalk_guide[] =
    "LARDD 1\n"
    "TITLE KModTalk Guide\n"
    "TEXT KModTalk is the direct user-to-kernel-module message surface.\n"
    "TEXT It does not hide the request behind Magic or an app-specific screen.\n"
    "SECTION Commands\n"
    "ITEM kmod list\n"
    "ITEM kmod gui status\n"
    "ITEM kmod gui cursor mouse\n"
    "ITEM kmod fs sync\n"
    "ITEM kmod oslink emit shell hello\n"
    "ITEM kmod task default 10\n"
    "ITEM kmod history\n"
    "SECTION Values\n"
    "ITEM Users can ask kernel modules what they are doing.\n"
    "ITEM Module replies are visible immediately and logged to kmodtalk.lardd.\n"
    "ITEM Risky control still stays explicit through named module messages.\n"
    "END\n";

static const uint8_t file_kmo_guide[] =
    "LARDD 1\n"
    "TITLE KMO Kernel Module Files\n"
    "TEXT KMO is the native .kmo file format for file-stored LardOS kernel modules.\n"
    "TEXT A KMO names a KModTalk target and a default message, so users can create, inspect, change, run, and delete module routes as files.\n"
    "TEXT If the user chooses RAW 1 or TARGET raw, the KMO directly executes an LSH/raw-control command instead of using the safer KModTalk path.\n"
    "SECTION Syntax\n"
    "ITEM KMO 1\n"
    "ITEM ID module-id\n"
    "ITEM NAME Human module name\n"
    "ITEM COMMAND optional-shell-command-name\n"
    "ITEM TARGET gui|fs|task|oslink|boot|time|vm|sysrxe|lardkit|raw\n"
    "ITEM RAW 1 turns on dangerous raw-control LSH execution.\n"
    "ITEM HELP short visible description\n"
    "ITEM DEFAULT message sent when kmo run has no message; in raw mode it is the LSH command.\n"
    "ITEM TEXT body line shown by kmo show\n"
    "SECTION Commands\n"
    "ITEM kmo list\n"
    "ITEM kmo show user0.kmo\n"
    "ITEM kmo run user-kmo\n"
    "ITEM kmo create mine.kmo gui status\n"
    "ITEM kmo command mystat.kmo mystat gui status\n"
    "ITEM mystat\n"
    "ITEM kmo raw rawdoor.kmo sum\n"
    "ITEM kmo set mine.kmo command mycmd\n"
    "ITEM kmo set mine.kmo raw 1\n"
    "ITEM kmo set mine.kmo target fs\n"
    "ITEM kmo set mine.kmo default sync\n"
    "ITEM kmo delete mine.kmo\n"
    "SECTION Values\n"
    "ITEM User-created KMO files live in writable RAM/LPST slots.\n"
    "ITEM COMMAND turns a .kmo into a shell command, so new command surfaces no longer require editing the LSH dispatcher.\n"
    "ITEM Built-in KMO files can be changed by kmo set, which writes a user-owned overlay over the seed/default original.\n"
    "ITEM kmo delete removes a KMO from the active registry; writable slots become empty and seed/default samples are removed from the active filesystem view.\n"
    "ITEM Raw-control KMO is intentionally dangerous. It exists because the user owns the machine, not because it is the safest path.\n"
    "END\n";

static const uint8_t file_liveupdate_guide[] =
    "LARDD 1\n"
    "TITLE LiveUpdate Runtime File And Code Updates\n"
    "TEXT LiveUpdate is the runtime update layer for changing LardOS files and file-owned code while the OS is already running.\n"
    "TEXT It is meant as the base for later automatic updates, without hiding power or removing user control.\n"
    "SECTION Commands\n"
    "ITEM liveupdate status\n"
    "ITEM liveupdate file notes.txt text\n"
    "ITEM liveupdate apply module.kmo KMO 1\\nID m\\nCOMMAND m\\nTARGET boot\\nDEFAULT status\\n\n"
    "ITEM liveupdate append app.rxe TEXT more\n"
    "ITEM liveupdate from update.tmp userapp.sysrxe\n"
    "ITEM liveupdate reload kmo|sysrxe|rxe|drivers|fstwt|ltheme|lguilib|wallpaper|all\n"
    "ITEM liveupdate auto on|off\n"
    "SECTION Reload Scopes\n"
    "ITEM .kmo reloads the KMO registry so kernel-module command files can change live.\n"
    "ITEM .sysrxe and .rxe reload app registries and refresh the GUI launchers without rebooting.\n"
    "ITEM .drfl reloads driver descriptors, .fstwts reloads filesystem translation, .ltheme/.lguilib apply display state, and .lwall or wallpaper.lardd reloads the desktop wallpaper.\n"
    "SECTION Ownership\n"
    "ITEM If a target is a built-in seed/default file, LiveUpdate creates a user-owned overlay with the same name.\n"
    "ITEM The original seed/default file stays available underneath the overlay, so rollback/tombstone tools remain visible.\n"
    "ITEM Raw-control KMO and SUM still exist for deliberately dangerous paths; LiveUpdate does not remove them.\n"
    "END\n";

static const uint8_t file_gui_status_kmo[] =
    "KMO 1\n"
    "ID gui-status\n"
    "NAME GUI Status KMO\n"
    "COMMAND guistat\n"
    "TARGET gui\n"
    "HELP Ask the GUI kernel module for cursor and screenram status.\n"
    "DEFAULT status\n"
    "TEXT This built-in sample is a normal .kmo file, not a new hand-coded shell branch.\n"
    "TEXT Use kmo set gui_status.kmo default \"cursor\" to take ownership and change it.\n";

static const uint8_t file_raw_control_kmo[] =
    "KMO 1\n"
    "ID raw-control-demo\n"
    "NAME Raw Control Demo KMO\n"
    "COMMAND rawdemo\n"
    "TARGET raw\n"
    "RAW 1\n"
    "HELP Demonstrates explicit user-chosen raw-control KMO execution.\n"
    "DEFAULT echo raw-kmo-demo\n"
    "TEXT This sample runs an LSH command directly instead of routing through KModTalk.\n"
    "TEXT Change DEFAULT to sum, peek, poke, asm_, bye, or any other explicit command only if you accept the risk.\n";

/*
 * Bundled app source lives in apps/bundled/. The build converts those files
 * into these generated payloads so fs.c registers files without owning app
 * code.
 */
#include "app_hello_sysrxe.inc"
#include "app_langdemo_rxe.inc"
#include "app_demo_game_rxe.inc"
#include "app_lardwrite_rxe.inc"
#include "app_lardsheet_rxe.inc"
#include "app_lardshow_rxe.inc"

static const uint8_t file_tests_lunit[] =
    "LUNIT 1\n"
    "CHECK file lardos.lars\n"
    "CHECK file lardtime_guide.lardd\n"
    "CHECK file default.ltheme\n"
    "CHECK writable journal.lardd\n"
    "CHECK writable userlaw.lardd\n"
    "CHECK writable glyphmap.lardd\n"
    "CHECK writable wallpaper.lardd\n"
    "CHECK command trace\n"
    "CHECK command netwatch\n"
    "CHECK command lconnect\n"
    "CHECK command glyph\n"
    "CHECK command wallpaper\n"
    "CHECK command cursor\n"
    "CHECK command vm\n"
    "CHECK command gasm\n"
    "CHECK command shrine\n"
    "CHECK file hello.shrine\n"
    "CHECK file shrine_guide.lardd\n"
    "CHECK command dos\n"
    "CHECK command install\n"
    "CHECK command media\n"
    "CHECK file media_guide.lardd\n"
    "CHECK command webstack\n"
    "CHECK file webstack_guide.lardd\n"
    "CHECK file webdemo.lars\n"
    "CHECK file fstwt_guide.lardd\n"
    "CHECK file liveupdate_guide.lardd\n"
    "CHECK file default.fstwts\n"
    "CHECK writable fstwt.fstwts\n"
    "CHECK command fstwt\n"
    "CHECK command bleed\n"
    "CHECK command crash\n"
    "CHECK command restore\n"
    "CHECK command tomb\n"
    "CHECK command tombstone\n"
    "CHECK file dosmode_guide.lardd\n"
    "CHECK file installer_guide.lardd\n"
    "CHECK writable dosmode.lardd\n"
    "CHECK writable fsdelete.lardd\n"
    "CHECK file vm_guide.lardd\n"
    "CHECK command time\n"
    "CHECK command lunar\n"
    "CHECK command dangun\n"
    "CHECK command notes\n"
    "CHECK command larsview\n"
    "CHECK command dir\n"
    "CHECK command ren\n"
    "CHECK command rename\n"
    "CHECK command mode\n"
    "CHECK command panicroom\n"
    "CHECK command paniccapsule\n"
    "CHECK command auxkernel\n"
    "CHECK command sysrxe\n"
    "CHECK command rxe\n"
    "CHECK command rxr\n"
    "CHECK command rxrpath\n"
    "CHECK command rxrmap\n"
    "CHECK command vpath\n"
    "CHECK command pathmap\n"
    "CHECK command kmod\n"
    "CHECK command kmo\n"
    "CHECK command liveupdate\n"
    "CHECK command lword\n"
    "CHECK command lsheet\n"
    "CHECK command lshow\n"
    "CHECK file sysrxe_guide.lardd\n"
    "CHECK file rxe_guide.lardd\n"
    "CHECK file rxr_guide.lardd\n"
    "CHECK file office_guide.lardd\n"
    "CHECK file sample.rxr\n"
    "CHECK file kmodtalk_guide.lardd\n"
    "CHECK file kmo_guide.lardd\n"
    "CHECK file hello.sysrxe\n"
    "CHECK file langdemo.rxe\n"
    "CHECK file demo_game.rxe\n"
    "CHECK file lardwrite.rxe\n"
    "CHECK file lardsheet.rxe\n"
    "CHECK file lardshow.rxe\n"
    "CHECK file gui_status.kmo\n"
    "CHECK file raw_control.kmo\n"
    "CHECK writable userapp.sysrxe\n"
    "CHECK writable kmodtalk.lardd\n"
    "CHECK writable user0.kmo\n"
    "CHECK writable displayfix.spfx\n"
    "CHECK writable security.lardd\n"
    "CHECK writable lconnect.lardd\n"
    "CHECK writable office_doc.lardd\n"
    "CHECK writable office_sheet.lsheet\n"
    "CHECK writable office_deck.lshow\n"
    "CHECK writable auxkernel.lardd\n"
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
    { "lts.lardd", file_lts_lardd, sizeof(file_lts_lardd) - 1 },
    { "office_guide.lardd", file_office_guide, sizeof(file_office_guide) - 1 },
    { "glyph_guide.lardd", file_glyph_guide, sizeof(file_glyph_guide) - 1 },
    { "lardtime_guide.lardd", file_lardtime_guide, sizeof(file_lardtime_guide) - 1 },
    { "vm_guide.lardd", file_vm_guide, sizeof(file_vm_guide) - 1 },
    { "shrine_guide.lardd", file_shrine_guide, sizeof(file_shrine_guide) - 1 },
    { "dosmode_guide.lardd", file_dosmode_guide, sizeof(file_dosmode_guide) - 1 },
    { "installer_guide.lardd", file_installer_guide, sizeof(file_installer_guide) - 1 },
    { "media_guide.lardd", file_media_guide, sizeof(file_media_guide) - 1 },
    { "webstack_guide.lardd", file_webstack_guide, sizeof(file_webstack_guide) - 1 },
    { "webdemo.lars", file_webdemo_lars, sizeof(file_webdemo_lars) - 1 },
    { "fstwt_guide.lardd", file_fstwt_guide, sizeof(file_fstwt_guide) - 1 },
    { "liveupdate_guide.lardd", file_liveupdate_guide, sizeof(file_liveupdate_guide) - 1 },
    { "default.fstwts", file_default_fstwts, sizeof(file_default_fstwts) - 1 },
    { "releases.lardd", file_releases_lardd, sizeof(file_releases_lardd) - 1 },
    { "features.lil",  file_features_lil,  sizeof(file_features_lil) - 1 },
    { "sample.lpack",  file_sample_lpack,  sizeof(file_sample_lpack) - 1 },
    { "rxr_guide.lardd", file_rxr_guide, sizeof(file_rxr_guide) - 1 },
    { "sample.rxr",    file_sample_rxr,    sizeof(file_sample_rxr) - 1 },
    { "sysrxe_guide.lardd", file_sysrxe_guide, sizeof(file_sysrxe_guide) - 1 },
    { "rxe_guide.lardd", file_rxe_guide, sizeof(file_rxe_guide) - 1 },
    { "kmodtalk_guide.lardd", file_kmodtalk_guide, sizeof(file_kmodtalk_guide) - 1 },
    { "kmo_guide.lardd", file_kmo_guide, sizeof(file_kmo_guide) - 1 },
    { "hello.sysrxe", file_hello_sysrxe, sizeof(file_hello_sysrxe) },
    { "demo_game.rxe", file_demo_game_rxe, sizeof(file_demo_game_rxe) },
    { "langdemo.rxe", file_langdemo_rxe, sizeof(file_langdemo_rxe) },
    { "lardwrite.rxe", file_lardwrite_rxe, sizeof(file_lardwrite_rxe) },
    { "lardsheet.rxe", file_lardsheet_rxe, sizeof(file_lardsheet_rxe) },
    { "lardshow.rxe", file_lardshow_rxe, sizeof(file_lardshow_rxe) },
    { "gui_status.kmo", file_gui_status_kmo, sizeof(file_gui_status_kmo) - 1 },
    { "raw_control.kmo", file_raw_control_kmo, sizeof(file_raw_control_kmo) - 1 },
    { "tests.lunit",   file_tests_lunit,   sizeof(file_tests_lunit) - 1 },
    { "bundle.lar",    file_bundle_lar,    sizeof(file_bundle_lar) },
    { "sample.bmp",    file_sample_bmp,    sizeof(file_sample_bmp) },
    { "rtl8139.drfl",  file_rtl8139_drfl,  sizeof(file_rtl8139_drfl) - 1 },
    { "piix3ide.drfl", file_piix3ide_drfl, sizeof(file_piix3ide_drfl) - 1 },
#include "fs_ldll_entries.inc"
    { "lafillo_demo.bosx", file_lafillo_demo_bosx, sizeof(file_lafillo_demo_bosx) },
    { "demo.larsh",      file_demo_larsh,      sizeof(file_demo_larsh) - 1 },
};

static const uint32_t FS_FILE_COUNT = sizeof(FS_FILES) / sizeof(FS_FILES[0]);

static FsFile g_lfs_result;
static char g_lfs_name[LFS_MAX_NAME];
static FsFile g_ram_result;

static int fs_name_eq(const char* a, const char* b);
static uint32_t writable_count(void);
static FsWritableFile* writable_at(uint32_t idx);

typedef struct {
    void (*cb)(const char* name, uint32_t size, void* user);
    void* user;
} FsListFilterCtx;

static int fs_writable_name_exists_raw(const char* name)
{
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        if (w && fs_name_eq(w->name, name)) return 1;
    }
    return 0;
}

static void fs_list_lfs_filter_cb(const char* name, uint32_t size, void* u)
{
    FsListFilterCtx* ctx = (FsListFilterCtx*)u;
    if (ctx && ctx->cb && !fs_readonly_hidden(name) && !fs_writable_name_exists_raw(name)) {
        ctx->cb(name, size, ctx->user);
    }
}

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

static int fs_name_eq(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static void fs_copy_name(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char fs_lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int fs_path_sep(char c)
{
    return c == '/' || c == '\\';
}

static uint32_t fs_path_hash(const char* s)
{
    uint32_t h = 2166136261u;
    if (!s) s = "";
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static void fs_append_hex8(char* out, uint32_t cap, uint32_t* n, uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        if (*n + 1u < cap) out[(*n)++] = hex[(v >> shift) & 0xFu];
    }
}

static int fs_flatten_os_path(const char* path, char* out, uint32_t cap)
{
    char tmp[32];
    uint32_t n = 0;
    uint32_t saw_sep = 0;
    uint32_t saw_before = 0;
    uint32_t saw_after = 0;
    uint32_t lossy = 0;
    uint32_t truncated = 0;
    uint32_t last_us = 0;
    const char* p = path ? path : "";
    uint32_t h = fs_path_hash(p);
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    while (*p == ' ' || *p == '\t') {
        p++;
        lossy = 1u;
    }
    while (*p) {
        uint8_t uc = (uint8_t)*p++;
        char c = (char)uc;
        char put = 0;
        if (fs_path_sep(c)) {
            if (saw_before) saw_sep = 1u;
            put = '_';
        } else {
            if (!saw_sep && c > ' ') saw_before = 1u;
            if (saw_sep && c > ' ') saw_after = 1u;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                put = fs_lower_ascii(c);
            } else if (c == '.' || c == '-' || c == '_') {
                put = c;
            } else {
                put = '_';
                lossy = 1u;
            }
        }
        if (put == '_') {
            if (last_us) continue;
            last_us = 1u;
        } else {
            last_us = 0;
        }
        if (n + 1u < sizeof(tmp)) tmp[n++] = put;
        else truncated = 1u;
    }
    while (n > 0 && (tmp[n - 1u] == '_' || tmp[n - 1u] == '.')) n--;
    tmp[n] = '\0';
    if (!saw_sep || !saw_before || !saw_after) return -2;
    if (n == 0) {
        tmp[0] = 'v'; tmp[1] = 'p'; tmp[2] = 'a'; tmp[3] = 't'; tmp[4] = 'h'; tmp[5] = '\0';
        n = 5u;
        lossy = 1u;
    }
    if (!lossy && !truncated && n + 1u <= cap) {
        fs_copy_name(out, cap, tmp);
        return 0;
    }
    {
        uint32_t max_base = cap > 10u ? cap - 10u : 0u;
        uint32_t o = 0;
        if (max_base > 22u) max_base = 22u;
        while (o < n && o < max_base && o + 1u < cap) {
            out[o] = tmp[o];
            o++;
        }
        while (o > 0 && out[o - 1u] == '_') o--;
        if (o == 0 && cap > 6u) {
            out[o++] = 'v'; out[o++] = 'p'; out[o++] = 'a'; out[o++] = 't'; out[o++] = 'h';
        }
        if (o + 1u < cap) out[o++] = '_';
        fs_append_hex8(out, cap, &o, h);
        out[o < cap ? o : cap - 1u] = '\0';
    }
    return 0;
}

int fs_resolve_os_path(const char* path, char* out, uint32_t cap)
{
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (fstwt_translate_to_lard(path, out, cap) == 0) return 0;
    if (rxr_resolve_path(path, out, cap) == 0) return 0;
    return fs_flatten_os_path(path, out, cap);
}

static const char* fs_os_name(const char* name, char* buf, uint32_t cap)
{
    if (!name) return "";
    if (fs_resolve_os_path(name, buf, cap) == 0) return buf;
    return name;
}

static int fs_valid_user_name(const char* name)
{
    uint32_t i = 0;
    if (!name || !name[0]) return 0;
    while (name[i]) {
        char c = name[i];
        if (i >= 31u) return 0;
        if (c <= ' ' || c == '/' || c == '\\' || c == ':' || c == '|') return 0;
        i++;
    }
    return i > 0;
}

int fs_path_selftest(void)
{
    char out[32];
    if (fs_resolve_os_path("docs/readme.lardd", out, sizeof(out)) != 0 ||
        !fs_name_eq(out, "docs_readme.lardd")) return -1;
    if (fs_resolve_os_path("Final Final Release/final fix", out, sizeof(out)) != 0 ||
        !fs_valid_user_name(out)) return -2;
    if (fs_resolve_os_path("plain.txt", out, sizeof(out)) == 0) return -3;
    if (fs_resolve_os_path("rxr/notes.txt", out, sizeof(out)) != 0 ||
        !fs_name_eq(out, "notes.txt")) return -4;
    return 0;
}

static int lpst_entry_name_valid(const uint8_t* entry)
{
    uint32_t i = 0;
    if (!entry || entry[0] == 0) return 0;
    while (i < 32u && entry[i]) {
        char c = (char)entry[i];
        if (c <= ' ' || c == '/' || c == '\\' || c == ':' || c == '|') return 0;
        i++;
    }
    return i > 0 && i < 32u;
}

static void lpst_entry_name_copy(char* dst, uint32_t cap, const uint8_t* entry)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (i + 1u < cap && i < 32u && entry[i]) {
        dst[i] = (char)entry[i];
        i++;
    }
    dst[i] = '\0';
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

static void fs_hidden_clear(void)
{
    for (uint32_t i = 0; i < FS_HIDDEN_READONLY_MAX; i++) s_hidden_readonly[i][0] = '\0';
    s_hidden_readonly_count = 0;
    for (uint32_t i = 0; i < FS_DELETED_READONLY_MAX; i++) s_deleted_readonly[i][0] = '\0';
    s_deleted_readonly_count = 0;
}

static int fs_hidden_index(const char* name)
{
    for (uint32_t i = 0; i < s_hidden_readonly_count; i++) {
        if (fs_name_eq(s_hidden_readonly[i], name)) return (int)i;
    }
    return -1;
}

static int fs_deleted_index(const char* name)
{
    for (uint32_t i = 0; i < s_deleted_readonly_count; i++) {
        if (fs_name_eq(s_deleted_readonly[i], name)) return (int)i;
    }
    return -1;
}

int fs_readonly_hidden(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    return fs_hidden_index(q) >= 0 || fs_deleted_index(q) >= 0;
}

int fs_readonly_deleted(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    return fs_deleted_index(q) >= 0;
}

uint32_t fs_readonly_hidden_count(void)
{
    return s_hidden_readonly_count;
}

const char* fs_readonly_hidden_name(uint32_t index)
{
    if (index >= s_hidden_readonly_count) return NULL;
    return s_hidden_readonly[index];
}

uint32_t fs_readonly_deleted_count(void)
{
    return s_deleted_readonly_count;
}

const char* fs_readonly_deleted_name(uint32_t index)
{
    if (index >= s_deleted_readonly_count) return NULL;
    return s_deleted_readonly[index];
}

static int fs_track_hide_readonly(const char* name)
{
    if (!name || !name[0]) return -1;
    if (fs_deleted_index(name) >= 0) return -3;
    if (fs_hidden_index(name) >= 0) return 1;
    if (s_hidden_readonly_count >= FS_HIDDEN_READONLY_MAX) return -2;
    fs_copy_name(s_hidden_readonly[s_hidden_readonly_count],
                 sizeof(s_hidden_readonly[s_hidden_readonly_count]), name);
    s_hidden_readonly_count++;
    return 0;
}

static int fs_track_show_readonly(const char* name)
{
    int idx = fs_hidden_index(name);
    if (idx < 0) return -1;
    for (uint32_t i = (uint32_t)idx; i + 1u < s_hidden_readonly_count; i++) {
        fs_copy_name(s_hidden_readonly[i], sizeof(s_hidden_readonly[i]), s_hidden_readonly[i + 1u]);
    }
    s_hidden_readonly_count--;
    if (s_hidden_readonly_count < FS_HIDDEN_READONLY_MAX) s_hidden_readonly[s_hidden_readonly_count][0] = '\0';
    return 0;
}

static int fs_track_undelete_readonly(const char* name)
{
    int idx = fs_deleted_index(name);
    if (idx < 0) return -1;
    for (uint32_t i = (uint32_t)idx; i + 1u < s_deleted_readonly_count; i++) {
        fs_copy_name(s_deleted_readonly[i], sizeof(s_deleted_readonly[i]), s_deleted_readonly[i + 1u]);
    }
    s_deleted_readonly_count--;
    if (s_deleted_readonly_count < FS_DELETED_READONLY_MAX) s_deleted_readonly[s_deleted_readonly_count][0] = '\0';
    return 0;
}

static int fs_track_delete_readonly(const char* name)
{
    if (!name || !name[0]) return -1;
    (void)fs_track_show_readonly(name);
    if (fs_deleted_index(name) >= 0) return 1;
    if (s_deleted_readonly_count >= FS_DELETED_READONLY_MAX) return -2;
    fs_copy_name(s_deleted_readonly[s_deleted_readonly_count],
                 sizeof(s_deleted_readonly[s_deleted_readonly_count]), name);
    s_deleted_readonly_count++;
    return 0;
}

static int fs_track_purge_readonly_delete(const char* name)
{
    int soft = fs_track_show_readonly(name);
    int hard = fs_track_undelete_readonly(name);
    return (soft == 0 || hard == 0) ? 0 : -1;
}

static int fs_readonly_physical_exists(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    const uint8_t* data;
    uint32_t sz;
    for (uint32_t i = 0; i < FS_FILE_COUNT; i++) {
        if (fs_name_eq(FS_FILES[i].name, q)) return 1;
    }
    return lfs_lookup(q, &data, &sz) ? 1 : 0;
}

static void fsdelete_rewrite_from_state(void);

static void fsdelete_append_record(const char* op, const char* name)
{
    FsWritableFile* w = &ram_fsdelete;
    uint32_t on = 0;
    uint32_t nn = 0;
    if (w->cap - w->size < 96u && !s_fsdelete_rewriting) {
        fsdelete_rewrite_from_state();
        return;
    }
    while (op && op[on]) on++;
    while (name && name[nn]) nn++;
    (void)fs_append(w, (const uint8_t*)op, on);
    (void)fs_append(w, (const uint8_t*)" ", 1u);
    (void)fs_append(w, (const uint8_t*)name, nn);
    (void)fs_append(w, (const uint8_t*)"\n", 1u);
}

static void fsdelete_rewrite_from_state(void)
{
    FsWritableFile* w = &ram_fsdelete;
    s_fsdelete_rewriting = 1;
    (void)fs_write(w, 0, fsdelete_init, sizeof(fsdelete_init) - 1u);
    for (uint32_t i = 0; i < s_hidden_readonly_count; i++) {
        fsdelete_append_record("HIDE", s_hidden_readonly[i]);
    }
    for (uint32_t i = 0; i < s_deleted_readonly_count; i++) {
        fsdelete_append_record("DELETE", s_deleted_readonly[i]);
    }
    s_fsdelete_rewriting = 0;
}

static void fs_apply_delete_log(void)
{
    uint32_t i = 0;
    fs_hidden_clear();
    while (i < ram_fsdelete.size) {
        char op[8];
        char name[32];
        uint32_t oi = 0;
        uint32_t ni = 0;
        while (i < ram_fsdelete.size && (ram_fsdelete.data[i] == ' ' || ram_fsdelete.data[i] == '\t' ||
               ram_fsdelete.data[i] == '\n' || ram_fsdelete.data[i] == '\r')) i++;
        while (i < ram_fsdelete.size && ram_fsdelete.data[i] != ' ' && ram_fsdelete.data[i] != '\t' &&
               ram_fsdelete.data[i] != '\n' && ram_fsdelete.data[i] != '\r' && oi + 1u < sizeof(op)) {
            op[oi++] = (char)ram_fsdelete.data[i++];
        }
        op[oi] = '\0';
        while (i < ram_fsdelete.size && (ram_fsdelete.data[i] == ' ' || ram_fsdelete.data[i] == '\t')) i++;
        while (i < ram_fsdelete.size && ram_fsdelete.data[i] != ' ' && ram_fsdelete.data[i] != '\t' &&
               ram_fsdelete.data[i] != '\n' && ram_fsdelete.data[i] != '\r' && ni + 1u < sizeof(name)) {
            name[ni++] = (char)ram_fsdelete.data[i++];
        }
        name[ni] = '\0';
        while (i < ram_fsdelete.size && ram_fsdelete.data[i] != '\n' && ram_fsdelete.data[i] != '\r') i++;
        if (fs_name_eq(op, "HIDE") && name[0]) {
            (void)fs_track_hide_readonly(name);
        } else if (fs_name_eq(op, "SHOW") && name[0]) {
            (void)fs_track_show_readonly(name);
        } else if ((fs_name_eq(op, "DELETE") || fs_name_eq(op, "KILL") || fs_name_eq(op, "DESTROY")) && name[0]) {
            (void)fs_track_delete_readonly(name);
        } else if ((fs_name_eq(op, "PURGE") || fs_name_eq(op, "DROP")) && name[0]) {
            (void)fs_track_purge_readonly_delete(name);
        }
    }
}

int fs_hide_readonly(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    int r;
    if (!fs_readonly_physical_exists(q)) return -1;
    r = fs_track_hide_readonly(q);
    if (r < 0) return r;
    fsdelete_append_record("HIDE", q);
    return r;
}

int fs_delete_readonly(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    int r;
    if (!fs_readonly_physical_exists(q)) return -1;
    r = fs_track_delete_readonly(q);
    if (r < 0) return r;
    fsdelete_append_record("DELETE", q);
    return r;
}

int fs_unhide_readonly(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    int r;
    if (!fs_readonly_physical_exists(q)) return -1;
    if (fs_readonly_deleted(q)) return -2;
    r = fs_track_show_readonly(q);
    fsdelete_append_record("SHOW", q);
    return r == 0 ? 0 : 1;
}

int fs_purge_readonly_tombstone(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    int r;
    if (!name || !name[0]) return -1;
    r = fs_track_purge_readonly_delete(q);
    fsdelete_rewrite_from_state();
    return r == 0 ? 0 : 1;
}

int fs_purge_all_readonly_tombstones(void)
{
    int count = (int)(s_hidden_readonly_count + s_deleted_readonly_count);
    fs_hidden_clear();
    fsdelete_rewrite_from_state();
    return count;
}

int fs_delete_overlay_selftest(void)
{
    if (!fs_readonly_physical_exists("hello.txt")) return -1;
    if (FS_HIDDEN_READONLY_MAX < 8u) return -2;
    if (!fs_open_writable("fsdelete.lardd")) return -3;
    if (FS_DELETED_READONLY_MAX < 8u) return -4;
    return 0;
}

int fs_rename_selftest(void)
{
    const char* old_name = "notes.txt";
    const char* tmp_name = "renameprobe.tmp";
    uint32_t dirty_before = s_fs_dirty;
    int r;
    if (!fs_open_writable(old_name)) return -1;
    if (fs_open(tmp_name)) return -2;
    r = fs_rename_writable(old_name, tmp_name);
    if (r != 0) return -3;
    if (fs_open_writable(old_name) || !fs_open_writable(tmp_name)) {
        (void)fs_rename_writable(tmp_name, old_name);
        s_fs_dirty = dirty_before;
        return -4;
    }
    r = fs_rename_writable(tmp_name, old_name);
    if (r != 0) {
        s_fs_dirty = dirty_before;
        return -5;
    }
    if (!fs_open_writable(old_name) || fs_open_writable(tmp_name)) {
        s_fs_dirty = dirty_before;
        return -6;
    }
    s_fs_dirty = dirty_before;
    return 0;
}

static uint32_t writable_count(void)
{
    return 41u;
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
    if (idx == 19) return &ram_dosmode;
    if (idx == 20) return &ram_fsdelete;
    if (idx == 21) return &ram_userapp_sysrxe;
    if (idx == 22) return &ram_kmodtalk;
    if (idx == 23) return &ram_user0_kmo;
    if (idx == 24) return &ram_user1_kmo;
    if (idx == 25) return &ram_user2_kmo;
    if (idx == 26) return &ram_user3_kmo;
    if (idx == 27) return &ram_rxrslot0;
    if (idx == 28) return &ram_rxrslot1;
    if (idx == 29) return &ram_rxrslot2;
    if (idx == 30) return &ram_rxrslot3;
    if (idx == 31) return &ram_fstwts;
    if (idx == 32) return &ram_wallpaper;
    if (idx == 33) return &ram_displayfix;
    if (idx == 34) return &ram_security;
    if (idx == 35) return &ram_megaclip;
    if (idx == 36) return &ram_lconnect;
    if (idx == 37) return &ram_office_doc;
    if (idx == 38) return &ram_office_sheet;
    if (idx == 39) return &ram_office_deck;
    if (idx == 40) return &ram_auxkernel;
    return NULL;
}

void fs_init(void)
{
    fs_hidden_clear();
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
    for (uint32_t i = 0; i < sizeof(fstwts_init) - 1 && i < FSTWTS_CAP; i++) {
        ram_fstwts_buf[i] = fstwts_init[i];
    }
    ram_fstwts.size = sizeof(fstwts_init) - 1;
    for (uint32_t i = 0; i < sizeof(glyphmap_init) - 1 && i < GLYPHMAP_CAP; i++) {
        ram_glyphmap_buf[i] = glyphmap_init[i];
    }
    ram_glyphmap.size = sizeof(glyphmap_init) - 1;
    for (uint32_t i = 0; i < sizeof(dosmode_init) - 1 && i < DOSMODE_CAP; i++) {
        ram_dosmode_buf[i] = dosmode_init[i];
    }
    ram_dosmode.size = sizeof(dosmode_init) - 1;
    for (uint32_t i = 0; i < sizeof(wallpaper_init) - 1 && i < WALLPAPER_CAP; i++) {
        ram_wallpaper_buf[i] = wallpaper_init[i];
    }
    ram_wallpaper.size = sizeof(wallpaper_init) - 1;
    for (uint32_t i = 0; i < sizeof(displayfix_init) - 1 && i < DISPLAYFIX_CAP; i++) {
        ram_displayfix_buf[i] = displayfix_init[i];
    }
    ram_displayfix.size = sizeof(displayfix_init) - 1;
    for (uint32_t i = 0; i < sizeof(security_init) - 1 && i < SECURITY_CAP; i++) {
        ram_security_buf[i] = security_init[i];
    }
    ram_security.size = sizeof(security_init) - 1;
    for (uint32_t i = 0; i < sizeof(megaclip_init_doc) - 1 && i < MEGACLIP_CAP; i++) {
        ram_megaclip_buf[i] = megaclip_init_doc[i];
    }
    ram_megaclip.size = sizeof(megaclip_init_doc) - 1;
    for (uint32_t i = 0; i < sizeof(lconnect_init_doc) - 1 && i < LCONNECT_CAP; i++) {
        ram_lconnect_buf[i] = lconnect_init_doc[i];
    }
    ram_lconnect.size = sizeof(lconnect_init_doc) - 1;
    for (uint32_t i = 0; i < sizeof(office_doc_init) - 1 && i < OFFICE_DOC_CAP; i++) {
        ram_office_doc_buf[i] = office_doc_init[i];
    }
    ram_office_doc.size = sizeof(office_doc_init) - 1;
    for (uint32_t i = 0; i < sizeof(office_sheet_init) - 1 && i < OFFICE_SHEET_CAP; i++) {
        ram_office_sheet_buf[i] = office_sheet_init[i];
    }
    ram_office_sheet.size = sizeof(office_sheet_init) - 1;
    for (uint32_t i = 0; i < sizeof(office_deck_init) - 1 && i < OFFICE_DECK_CAP; i++) {
        ram_office_deck_buf[i] = office_deck_init[i];
    }
    ram_office_deck.size = sizeof(office_deck_init) - 1;
    for (uint32_t i = 0; i < sizeof(auxkernel_init) - 1 && i < AUXKERNEL_CAP; i++) {
        ram_auxkernel_buf[i] = auxkernel_init[i];
    }
    ram_auxkernel.size = sizeof(auxkernel_init) - 1;
    for (uint32_t i = 0; i < sizeof(fsdelete_init) - 1 && i < FSDELETE_CAP; i++) {
        ram_fsdelete_buf[i] = fsdelete_init[i];
    }
    ram_fsdelete.size = sizeof(fsdelete_init) - 1;
    for (uint32_t i = 0; i < sizeof(userapp_sysrxe_init) - 1 && i < USERAPP_SYSRXE_CAP; i++) {
        ram_userapp_sysrxe_buf[i] = userapp_sysrxe_init[i];
    }
    ram_userapp_sysrxe.size = sizeof(userapp_sysrxe_init) - 1;
    for (uint32_t i = 0; i < sizeof(kmodtalk_init) - 1 && i < KMODTALK_CAP; i++) {
        ram_kmodtalk_buf[i] = kmodtalk_init[i];
    }
    ram_kmodtalk.size = sizeof(kmodtalk_init) - 1;
    for (uint32_t i = 0; i < sizeof(user0_kmo_init) - 1 && i < USER_KMO_CAP; i++) {
        ram_user0_kmo_buf[i] = user0_kmo_init[i];
    }
    ram_user0_kmo.size = sizeof(user0_kmo_init) - 1;
    ram_user1_kmo.size = 0;
    ram_user2_kmo.size = 0;
    ram_user3_kmo.size = 0;
    lfs_mount(lfs_volume, sizeof(lfs_volume));
    (void)fs_persist_load();
    if (fstwt_load_script(ram_fstwts.data, ram_fstwts.size, "fstwt.fstwts") != 0) fstwt_init();
    fs_apply_delete_log();
}

const FsFile* fs_open(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        const char* a = w ? w->name : "";
        const char* b = q;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') {
            g_ram_result.name = w->name;
            g_ram_result.data = w->data;
            g_ram_result.size = w->size;
            return &g_ram_result;
        }
    }
    {
        const FsFile* f = fs_open_readonly(q);
        if (f) return f;
    }
    return 0;
}

const FsFile* fs_open_readonly(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    if (fs_readonly_hidden(q)) return 0;
    for (uint32_t i = 0; i < FS_FILE_COUNT; i++) {
        if (fs_name_eq(FS_FILES[i].name, q)) {
            return &FS_FILES[i];
        }
    }
    {
        const uint8_t* data;
        uint32_t sz;
        if (lfs_lookup(q, &data, &sz)) {
            uint32_t j = 0;
            while (q[j] && j < LFS_MAX_NAME - 1) { g_lfs_name[j] = q[j]; j++; }
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
    fs_list_writable(cb, user);
    fs_list_readonly(cb, user);
}

void fs_list_readonly(void (*cb)(const char* name, uint32_t size, void* user), void* user)
{
    if (!cb) return;
    for (uint32_t i = 0; i < FS_FILE_COUNT; i++) {
        if (!fs_readonly_hidden(FS_FILES[i].name) &&
            !fs_writable_name_exists_raw(FS_FILES[i].name)) {
            cb(FS_FILES[i].name, FS_FILES[i].size, user);
        }
    }
    {
        FsListFilterCtx ctx;
        ctx.cb = cb;
        ctx.user = user;
        lfs_list(fs_list_lfs_filter_cb, &ctx);
    }
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

        if (count == writable_count()) {
            w = writable_at(i);
        } else {
            for (uint32_t wi = 0; wi < writable_count(); wi++) {
                FsWritableFile* cand = writable_at(wi);
                if (cand && lpst_name_equals(entry, cand->name)) {
                    w = cand;
                    break;
                }
            }
        }
        if (!w) continue;
        if (cap != w->cap || size > w->cap || data_off > total || size > total - data_off) continue;
        if (lpst_hash(s_lpstore + data_off, size) != hash) continue;
        if (count == writable_count() && lpst_entry_name_valid(entry)) {
            lpst_entry_name_copy(w->name, sizeof(w->name), entry);
        }
        for (uint32_t j = 0; j < size; j++) w->data[j] = s_lpstore[data_off + j];
        w->size = size;
    }

    s_fs_dirty = 0;
    s_persist_active_bank = best_bank;
    s_persist_generation = best_generation;
    s_persist_last_result = 0;
    fs_apply_delete_log();
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
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        const char* a = w ? w->name : "";
        const char* b = q;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') return w;
    }
    return NULL;
}

static int fs_is_empty_rxr_slot(const FsWritableFile* w)
{
    static const char prefix[] = "rxrslot";
    uint32_t i = 0;
    if (!w || w->size != 0) return 0;
    while (prefix[i]) {
        if (w->name[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static FsWritableFile* fs_find_empty_rxr_slot(void)
{
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        if (fs_is_empty_rxr_slot(w)) return w;
    }
    return NULL;
}

uint32_t fs_creatable_writable_slots(void)
{
    uint32_t count = 0;
    for (uint32_t wi = 0; wi < writable_count(); wi++) {
        FsWritableFile* w = writable_at(wi);
        if (fs_is_empty_rxr_slot(w)) count++;
    }
    return count;
}

int fs_can_create_writable(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    if (!fs_valid_user_name(q)) return 0;
    if (fs_open_writable(q)) return 1;
    return fs_find_empty_rxr_slot() ? 1 : 0;
}

uint32_t fs_writable_capacity_for(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    FsWritableFile* w = fs_open_writable(q);
    if (w) return w->cap;
    if (!fs_valid_user_name(q)) return 0;
    w = fs_find_empty_rxr_slot();
    return w ? w->cap : 0;
}

FsWritableFile* fs_open_or_create_writable(const char* name)
{
    char resolved[32];
    const char* q = fs_os_name(name, resolved, sizeof(resolved));
    FsWritableFile* w = fs_open_writable(q);
    if (w) return w;
    if (!fs_valid_user_name(q)) return NULL;
    w = fs_find_empty_rxr_slot();
    if (!w) return NULL;
    fs_copy_name(w->name, sizeof(w->name), q);
    w->size = 0;
    s_fs_dirty = 1;
    return w;
}

int fs_rename_writable(const char* old_name, const char* new_name)
{
    FsWritableFile* w;
    char old_resolved[32];
    char new_resolved[32];
    const char* old_q = fs_os_name(old_name, old_resolved, sizeof(old_resolved));
    const char* new_q = fs_os_name(new_name, new_resolved, sizeof(new_resolved));
    if (!fs_valid_user_name(old_q) || !fs_valid_user_name(new_q)) return -2;
    w = fs_open_writable(old_q);
    if (!w) return -1;
    if (fs_name_eq(old_q, new_q)) return 0;
    if (fs_open_writable(new_q)) return -3;
    fs_copy_name(w->name, sizeof(w->name), new_q);
    s_fs_dirty = 1;
    return 0;
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
