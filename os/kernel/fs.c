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
    "p The Doc tab can switch HTTP requests between GET and POST; POST reads the address as URL|body.\n"
    "section Full-control starts\n"
    "li Run control in LSH for the system control map.\n"
    "li Run status to inspect version, storage, drivers, and containers.\n"
    "li Use magic before a command when LSH should predict and execute a mistyped safe command.\n"
    "li Run mode probe to enter a controlled real16 window and return to long64.\n"
    "li Use sram on or sram rect x y w h to turn quiet screen pixels into scratch RAM.\n"
    "li Use oslink status, ping, send, exec, recv, and peers for OS-to-OS messages and safe remote commands.\n"
    "li Use oslink emit channel text for LardOS-internal module messages.\n"
    "li Use exgui on and exgui style win linux mac for an extended desktop/window manager layer.\n"
    "li Use exexgui on for the sketch split layout: GUI left, terminal top-right, info bottom-right.\n"
    "li Use task list and task set id prio to inspect and change queued task priority.\n"
    "li Use tasktop to see runnable and paused task queues with priority bars.\n"
    "li Use bootprof set safe or bootprof set netoff to change the next boot profile.\n"
    "li Use crashlog show to inspect panic and diagnostic history.\n"
    "li Use lpack list sample.lpack and lpack install sample.lpack for native package installs.\n"
    "li Use screencheck retro for an old boot/storage-style visual screen scan.\n"
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
    "cmd mode probe\n"
    "cmd sram on\n"
    "cmd oslink status\n"
    "cmd oslink emit shell hello-from-lardos\n"
    "cmd exgui style linux\n"
    "cmd exgui layout tile\n"
    "cmd exexgui on\n"
    "cmd oslink exec 10.0.2.15 status\n"
    "cmd task list\n"
    "cmd tasktop\n"
    "cmd bootprof status\n"
    "cmd crashlog show\n"
    "cmd lpack list sample.lpack\n"
    "cmd screencheck retro\n"
    "cmd post\n"
    "cmd lil features.lil\n"
    "cmd lardd lardd_guide.lardd\n"
    "note Release suffixes: a=official, b=beta-experimental, p=hotpatch.\n"
    "end\n";

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
    { "lardd_guide.lardd", file_lardd_guide, sizeof(file_lardd_guide) - 1 },
    { "releases.lardd", file_releases_lardd, sizeof(file_releases_lardd) - 1 },
    { "features.lil",  file_features_lil,  sizeof(file_features_lil) - 1 },
    { "sample.lpack",  file_sample_lpack,  sizeof(file_sample_lpack) - 1 },
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
    return 6u;
}

static FsWritableFile* writable_at(uint32_t idx)
{
    if (idx == 0) return &ram_notes;
    if (idx == 1) return &ram_lafillo_save;
    if (idx == 2) return &ram_lar_extract;
    if (idx == 3) return &ram_vcs_restore;
    if (idx == 4) return &ram_bootprof;
    if (idx == 5) return &ram_crashlog;
    return NULL;
}

void fs_init(void)
{
    for (uint32_t i = 0; i < sizeof(notes_init) - 1 && i < RAM_FILE_CAP; i++) {
        ram_notes_buf[i] = notes_init[i];
    }
    ram_notes.size = sizeof(notes_init) - 1;
    for (uint32_t i = 0; i < sizeof(bootprof_init) - 1 && i < BOOTPROF_CAP; i++) {
        ram_bootprof_buf[i] = bootprof_init[i];
    }
    ram_bootprof.size = sizeof(bootprof_init) - 1;
    for (uint32_t i = 0; i < sizeof(crashlog_init) - 1 && i < CRASHLOG_CAP; i++) {
        ram_crashlog_buf[i] = crashlog_init[i];
    }
    ram_crashlog.size = sizeof(crashlog_init) - 1;
    lfs_mount(lfs_volume, sizeof(lfs_volume));
    (void)fs_persist_load();
}

const FsFile* fs_open(const char* name)
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
    {
        uint32_t j = 0;
        const char* n1 = "notes.txt";
        while (n1[j] && name[j] && n1[j] == name[j]) j++;
        if (n1[j] == '\0' && name[j] == '\0') {
            g_ram_result.name = ram_notes.name;
            g_ram_result.data = ram_notes.data;
            g_ram_result.size = ram_notes.size;
            return &g_ram_result;
        }
        j = 0;
        const char* n2 = "lafillo_saved.txt";
        while (n2[j] && name[j] && n2[j] == name[j]) j++;
        if (n2[j] == '\0' && name[j] == '\0') {
            g_ram_result.name = ram_lafillo_save.name;
            g_ram_result.data = ram_lafillo_save.data;
            g_ram_result.size = ram_lafillo_save.size;
            return &g_ram_result;
        }
        j = 0;
        const char* n3 = "lar_extract.txt";
        while (n3[j] && name[j] && n3[j] == name[j]) j++;
        if (n3[j] == '\0' && name[j] == '\0') {
            g_ram_result.name = ram_lar_extract.name;
            g_ram_result.data = ram_lar_extract.data;
            g_ram_result.size = ram_lar_extract.size;
            return &g_ram_result;
        }
        j = 0;
        const char* n4 = "vcs_restore.txt";
        while (n4[j] && name[j] && n4[j] == name[j]) j++;
        if (n4[j] == '\0' && name[j] == '\0') {
            g_ram_result.name = ram_vcs_restore.name;
            g_ram_result.data = ram_vcs_restore.data;
            g_ram_result.size = ram_vcs_restore.size;
            return &g_ram_result;
        }
        j = 0;
        const char* n5 = "bootprof.txt";
        while (n5[j] && name[j] && n5[j] == name[j]) j++;
        if (n5[j] == '\0' && name[j] == '\0') {
            g_ram_result.name = ram_bootprof.name;
            g_ram_result.data = ram_bootprof.data;
            g_ram_result.size = ram_bootprof.size;
            return &g_ram_result;
        }
        j = 0;
        const char* n6 = "crashlog.txt";
        while (n6[j] && name[j] && n6[j] == name[j]) j++;
        if (n6[j] == '\0' && name[j] == '\0') {
            g_ram_result.name = ram_crashlog.name;
            g_ram_result.data = ram_crashlog.data;
            g_ram_result.size = ram_crashlog.size;
            return &g_ram_result;
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
    for (uint32_t i = 0; i < FS_FILE_COUNT; i++) {
        cb(FS_FILES[i].name, FS_FILES[i].size, user);
    }
    lfs_list(cb, user);
    cb(ram_notes.name, ram_notes.size, user);
    cb(ram_lafillo_save.name, ram_lafillo_save.size, user);
    cb(ram_lar_extract.name, ram_lar_extract.size, user);
    cb(ram_vcs_restore.name, ram_vcs_restore.size, user);
    cb(ram_bootprof.name, ram_bootprof.size, user);
    cb(ram_crashlog.name, ram_crashlog.size, user);
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
    const char* n1 = "notes.txt";
    uint32_t i = 0;
    while (n1[i] && name[i] && n1[i] == name[i]) i++;
    if (n1[i] == '\0' && name[i] == '\0') return &ram_notes;
    const char* n2 = "lafillo_saved.txt";
    i = 0;
    while (n2[i] && name[i] && n2[i] == name[i]) i++;
    if (n2[i] == '\0' && name[i] == '\0') return &ram_lafillo_save;
    const char* n3 = "lar_extract.txt";
    i = 0;
    while (n3[i] && name[i] && n3[i] == name[i]) i++;
    if (n3[i] == '\0' && name[i] == '\0') return &ram_lar_extract;
    const char* n4 = "vcs_restore.txt";
    i = 0;
    while (n4[i] && name[i] && n4[i] == name[i]) i++;
    if (n4[i] == '\0' && name[i] == '\0') return &ram_vcs_restore;
    const char* n5 = "bootprof.txt";
    i = 0;
    while (n5[i] && name[i] && n5[i] == name[i]) i++;
    if (n5[i] == '\0' && name[i] == '\0') return &ram_bootprof;
    const char* n6 = "crashlog.txt";
    i = 0;
    while (n6[i] && name[i] && n6[i] == name[i]) i++;
    if (n6[i] == '\0' && name[i] == '\0') return &ram_crashlog;
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
