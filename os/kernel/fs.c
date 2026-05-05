#include "fs.h"
#include "lfs.h"
#include "fs_ldll.inc"
#include "lafillo_demo_data.inc"

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

#define RAM_FILE_CAP 8192u
/* Initial Notes: "Image: " + U+E000 (UTF-8 EE 80 80) - view Gallery sample.bmp first to assign */
static const uint8_t notes_init[] = "Image: \xEE\x80\x80\n";
static uint8_t ram_notes_buf[RAM_FILE_CAP];
static FsWritableFile ram_notes = { "notes.txt", ram_notes_buf, 0, RAM_FILE_CAP };

#define LAFILLO_SAVE_CAP 4096u
static uint8_t ram_lafillo_save_buf[LAFILLO_SAVE_CAP];
static FsWritableFile ram_lafillo_save = { "lafillo_saved.txt", ram_lafillo_save_buf, 0, LAFILLO_SAVE_CAP };

static const uint8_t file_hello_txt[] =
    "Hello from the in-memory filesystem!\n"
    "This data is embedded in the kernel image.\n";

static const uint8_t file_readme_txt[] =
    "This is a tiny RAM-based filesystem used for experimentation.\n";

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

/* demo.larsh - LARSH animation: rect + circle + text + LMD with keyframe */
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
    "obj 3 lmd 8 24 240 160 0xeaeaea \"# LMD Demo\\n- **bold** text\\n- `code` style\\n- List items\"\n"
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
    { "sample.bmp",    file_sample_bmp,    sizeof(file_sample_bmp) },
    { "rtl8139.drfl",  file_rtl8139_drfl,  sizeof(file_rtl8139_drfl) },
#include "fs_ldll_entries.inc"
    { "lafillo_demo.bosx", file_lafillo_demo_bosx, sizeof(file_lafillo_demo_bosx) },
    { "demo.larsh",      file_demo_larsh,      sizeof(file_demo_larsh) - 1 },
};

static const uint32_t FS_FILE_COUNT = sizeof(FS_FILES) / sizeof(FS_FILES[0]);

static FsFile g_lfs_result;
static char g_lfs_name[LFS_MAX_NAME];
static FsFile g_ram_result;

void fs_init(void)
{
    for (uint32_t i = 0; i < sizeof(notes_init) - 1 && i < RAM_FILE_CAP; i++) {
        ram_notes_buf[i] = notes_init[i];
    }
    ram_notes.size = sizeof(notes_init) - 1;
    lfs_mount(lfs_volume, sizeof(lfs_volume));
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
    return NULL;
}

uint32_t fs_write(FsWritableFile* f, uint32_t offset, const uint8_t* buf, uint32_t len)
{
    if (!f || !buf || offset > f->cap) return 0;
    if (len > f->cap - offset) len = f->cap - offset;
    for (uint32_t i = 0; i < len; i++) f->data[offset + i] = buf[i];
    if (offset + len > f->size) f->size = offset + len;
    return len;
}

uint32_t fs_append(FsWritableFile* f, const uint8_t* buf, uint32_t len)
{
    if (!f || !buf) return 0;
    if (f->size + len > f->cap) len = f->cap - f->size;
    for (uint32_t i = 0; i < len; i++) f->data[f->size + i] = buf[i];
    f->size += len;
    return len;
}

