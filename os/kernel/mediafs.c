#include "mediafs.h"

#include "installer.h"
#include "storage.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

#define MEDIAFS_MAGIC      0x5346444Du /* "MDFS" */
#define MEDIAFS_VERSION    1u
#define MEDIAFS_SECTORS    32u
#define MEDIAFS_BYTES      (MEDIAFS_SECTORS * STORAGE_SECTOR_SIZE)
#define MEDIAFS_MAX_FILES  6u
#define MEDIAFS_FILE_CAP   2048u
#define MEDIAFS_HEADER_LEN 512u
#define MEDIAFS_ENTRY_OFF  32u
#define MEDIAFS_ENTRY_SIZE 48u
#define MEDIAFS_DATA_OFF   MEDIAFS_HEADER_LEN

#define MEDIAFS_SSD_LBA     LARD_INSTALL_IMAGE_SECTORS
#define MEDIAFS_USB_LBA     (MEDIAFS_SSD_LBA + MEDIAFS_SECTORS)
#define MEDIAFS_FLOPPY_LBA  (MEDIAFS_USB_LBA + MEDIAFS_SECTORS)

typedef struct {
    char name[32];
    uint32_t size;
    uint32_t offset;
    uint32_t cap;
    uint32_t hash;
} media_file_t;

typedef struct {
    char drive;
    const char* name;
    const char* label;
    uint32_t base_lba;
    uint32_t sectors;
    uint8_t image[MEDIAFS_BYTES];
    media_file_t files[MEDIAFS_MAX_FILES];
    uint32_t file_count;
    uint32_t mounted;
    uint32_t persistent;
    uint32_t dirty;
    uint32_t generation;
    int last_error;
} media_dev_t;

static media_dev_t s_media[] = {
    { .drive = 'S', .name = "ssd", .label = "SSD/HDD native media store", .base_lba = MEDIAFS_SSD_LBA, .sectors = MEDIAFS_SECTORS },
    { .drive = 'U', .name = "usb", .label = "USB-style removable store", .base_lba = MEDIAFS_USB_LBA, .sectors = MEDIAFS_SECTORS },
    { .drive = 'Y', .name = "floppy", .label = "Floppy-style media store", .base_lba = MEDIAFS_FLOPPY_LBA, .sectors = MEDIAFS_SECTORS },
};

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static char upper_drive(char d)
{
    if (d >= 'a' && d <= 'z') return (char)(d - ('a' - 'A'));
    return d;
}

static uint32_t media_hash(const uint8_t* data, uint32_t size)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static media_dev_t* media_by_drive(char drive)
{
    drive = upper_drive(drive);
    if (drive == 'F') drive = 'Y';
    for (uint32_t i = 0; i < mediafs_count(); i++) {
        if (s_media[i].drive == drive) return &s_media[i];
    }
    return NULL;
}

static void media_zero(uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

static void media_copy(uint8_t* dst, const uint8_t* src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static int media_valid_name(const char* name)
{
    uint32_t n = 0;
    if (!name || !name[0]) return 0;
    while (name[n]) {
        char c = name[n];
        if (n >= 31u) return 0;
        if (c <= ' ' || c == ':' || c == '/' || c == '\\') return 0;
        n++;
    }
    return 1;
}

static int media_name_eq(const char* a, const char* b)
{
    uint32_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static void media_write_header(media_dev_t* dev)
{
    media_zero(dev->image, MEDIAFS_HEADER_LEN);
    wr32(dev->image + 0, MEDIAFS_MAGIC);
    wr32(dev->image + 4, MEDIAFS_VERSION);
    dev->image[8] = (uint8_t)dev->drive;
    wr32(dev->image + 12, dev->file_count);
    wr32(dev->image + 16, dev->generation);
    wr32(dev->image + 20, dev->sectors);
    wr32(dev->image + 24, MEDIAFS_FILE_CAP);
    for (uint32_t i = 0; i < MEDIAFS_MAX_FILES; i++) {
        uint8_t* e = dev->image + MEDIAFS_ENTRY_OFF + i * MEDIAFS_ENTRY_SIZE;
        for (uint32_t j = 0; j < 32u; j++) e[j] = (uint8_t)dev->files[i].name[j];
        wr32(e + 32, dev->files[i].size);
        wr32(e + 36, dev->files[i].offset);
        wr32(e + 40, dev->files[i].cap);
        wr32(e + 44, dev->files[i].hash);
    }
}

static int media_parse_header(media_dev_t* dev)
{
    uint32_t file_count;
    if (rd32(dev->image + 0) != MEDIAFS_MAGIC) return -1;
    if (rd32(dev->image + 4) != MEDIAFS_VERSION) return -2;
    if ((char)dev->image[8] != dev->drive) return -3;
    file_count = rd32(dev->image + 12);
    if (file_count > MEDIAFS_MAX_FILES) return -4;
    dev->file_count = 0;
    dev->generation = rd32(dev->image + 16);
    for (uint32_t i = 0; i < MEDIAFS_MAX_FILES; i++) {
        const uint8_t* e = dev->image + MEDIAFS_ENTRY_OFF + i * MEDIAFS_ENTRY_SIZE;
        media_file_t f;
        media_zero((uint8_t*)&f, sizeof(f));
        for (uint32_t j = 0; j < 31u && e[j]; j++) f.name[j] = (char)e[j];
        f.size = rd32(e + 32);
        f.offset = rd32(e + 36);
        f.cap = rd32(e + 40);
        f.hash = rd32(e + 44);
        if (!f.name[0]) continue;
        if (f.cap == 0 || f.cap > MEDIAFS_FILE_CAP) return -5;
        if (f.size > f.cap) return -6;
        if (f.offset < MEDIAFS_DATA_OFF || f.offset + f.cap > MEDIAFS_BYTES) return -7;
        if (dev->file_count < MEDIAFS_MAX_FILES) {
            uint32_t compact_offset = MEDIAFS_DATA_OFF + dev->file_count * MEDIAFS_FILE_CAP;
            if (f.offset != compact_offset) {
                media_copy(dev->image + compact_offset, dev->image + f.offset, f.size);
                media_zero(dev->image + f.offset, f.cap);
                f.offset = compact_offset;
            }
            dev->files[dev->file_count++] = f;
        }
    }
    if (dev->file_count != file_count) {
        if (dev->file_count > file_count) return -8;
    }
    return 0;
}

static void media_format_memory(media_dev_t* dev)
{
    media_zero(dev->image, sizeof(dev->image));
    media_zero((uint8_t*)dev->files, sizeof(dev->files));
    dev->file_count = 0;
    dev->generation++;
    media_write_header(dev);
}

static int media_can_persist(const media_dev_t* dev)
{
    uint32_t count;
    if (!storage_available()) return 0;
    count = storage_sector_count();
    if (count == 0) return 0;
    return count >= dev->base_lba + dev->sectors;
}

static int media_load(media_dev_t* dev)
{
    int invalid;
    if (!dev) return -1;
    if (dev->mounted) return 0;
    dev->persistent = media_can_persist(dev);
    dev->dirty = 0;
    dev->last_error = 0;
    if (dev->persistent) {
        for (uint32_t i = 0; i < dev->sectors; i++) {
            int r = storage_read_sector(dev->base_lba + i, dev->image + i * STORAGE_SECTOR_SIZE);
            if (r != 0) {
                dev->last_error = r;
                dev->persistent = 0;
                media_format_memory(dev);
                dev->mounted = 1;
                return r;
            }
        }
        invalid = media_parse_header(dev);
        if (invalid != 0) {
            dev->last_error = invalid;
            media_format_memory(dev);
        }
    } else {
        media_format_memory(dev);
    }
    dev->mounted = 1;
    return 0;
}

static int media_find_file(media_dev_t* dev, const char* name)
{
    for (uint32_t i = 0; i < dev->file_count; i++) {
        if (media_name_eq(dev->files[i].name, name)) return (int)i;
    }
    return -1;
}

void mediafs_init(void)
{
    for (uint32_t i = 0; i < mediafs_count(); i++) (void)media_load(&s_media[i]);
}

uint32_t mediafs_count(void)
{
    return sizeof(s_media) / sizeof(s_media[0]);
}

int mediafs_info(uint32_t idx, mediafs_info_t* out)
{
    media_dev_t* dev;
    uint32_t bytes = 0;
    if (!out || idx >= mediafs_count()) return -1;
    dev = &s_media[idx];
    (void)media_load(dev);
    for (uint32_t i = 0; i < dev->file_count; i++) bytes += dev->files[i].size;
    out->drive = dev->drive;
    out->name = dev->name;
    out->label = dev->label;
    out->driver = dev->persistent ? storage_driver_name() : "ram-fallback";
    out->present = 1;
    out->persistent = dev->persistent;
    out->dirty = dev->dirty;
    out->files = dev->file_count;
    out->bytes = bytes;
    out->lba = dev->base_lba;
    out->sectors = dev->sectors;
    out->last_error = dev->last_error;
    return 0;
}

int mediafs_drive_supported(char drive)
{
    return media_by_drive(drive) ? 1 : 0;
}

int mediafs_list(char drive, mediafs_list_cb cb, void* user)
{
    media_dev_t* dev = media_by_drive(drive);
    if (!dev) return -1;
    (void)media_load(dev);
    for (uint32_t i = 0; i < dev->file_count; i++) {
        if (cb) cb(dev->files[i].name, dev->files[i].size, user);
    }
    return (int)dev->file_count;
}

int mediafs_read(char drive, const char* name, const uint8_t** data, uint32_t* size)
{
    media_dev_t* dev = media_by_drive(drive);
    int idx;
    if (!dev || !data || !size || !media_valid_name(name)) return -1;
    (void)media_load(dev);
    idx = media_find_file(dev, name);
    if (idx < 0) return -2;
    *data = dev->image + dev->files[idx].offset;
    *size = dev->files[idx].size;
    return 0;
}

int mediafs_sync(char drive)
{
    media_dev_t* dev = media_by_drive(drive);
    if (!dev) return -1;
    (void)media_load(dev);
    if (!dev->persistent) return -2;
    media_write_header(dev);
    for (uint32_t i = 0; i < dev->sectors; i++) {
        int r = storage_write_sector(dev->base_lba + i, dev->image + i * STORAGE_SECTOR_SIZE);
        if (r != 0) {
            dev->last_error = r;
            return r;
        }
    }
    dev->dirty = 0;
    dev->last_error = 0;
    return 0;
}

int mediafs_write(char drive, const char* name, const uint8_t* data, uint32_t size, int append)
{
    media_dev_t* dev = media_by_drive(drive);
    media_file_t* f;
    int idx;
    uint32_t start;
    if (!dev || !media_valid_name(name)) return -1;
    if (size && !data) return -2;
    (void)media_load(dev);
    idx = media_find_file(dev, name);
    if (idx < 0) {
        if (dev->file_count >= MEDIAFS_MAX_FILES) return -3;
        idx = (int)dev->file_count++;
        f = &dev->files[idx];
        media_zero((uint8_t*)f, sizeof(*f));
        for (uint32_t i = 0; name[i] && i < 31u; i++) f->name[i] = name[i];
        f->offset = MEDIAFS_DATA_OFF + (uint32_t)idx * MEDIAFS_FILE_CAP;
        f->cap = MEDIAFS_FILE_CAP;
        f->size = 0;
    }
    f = &dev->files[idx];
    start = append ? f->size : 0u;
    if (start + size > f->cap) return -4;
    if (!append) f->size = 0;
    if (size) media_copy(dev->image + f->offset + start, data, size);
    f->size = start + size;
    f->hash = media_hash(dev->image + f->offset, f->size);
    dev->generation++;
    dev->dirty = 1;
    media_write_header(dev);
    if (dev->persistent) (void)mediafs_sync(dev->drive);
    return (int)size;
}

int mediafs_delete(char drive, const char* name)
{
    media_dev_t* dev = media_by_drive(drive);
    int idx;
    if (!dev || !media_valid_name(name)) return -1;
    (void)media_load(dev);
    idx = media_find_file(dev, name);
    if (idx < 0) return -2;
    media_zero(dev->image + dev->files[idx].offset, dev->files[idx].cap);
    for (uint32_t i = (uint32_t)idx; i + 1u < dev->file_count; i++) {
        media_file_t moved = dev->files[i + 1u];
        uint32_t new_offset = MEDIAFS_DATA_OFF + i * MEDIAFS_FILE_CAP;
        if (moved.offset != new_offset) {
            media_copy(dev->image + new_offset, dev->image + moved.offset, moved.size);
            media_zero(dev->image + moved.offset, moved.cap);
            moved.offset = new_offset;
        }
        dev->files[i] = moved;
    }
    media_zero((uint8_t*)&dev->files[dev->file_count - 1u], sizeof(dev->files[0]));
    dev->file_count--;
    dev->generation++;
    dev->dirty = 1;
    media_write_header(dev);
    if (dev->persistent) (void)mediafs_sync(dev->drive);
    return 0;
}

int mediafs_format(char drive)
{
    media_dev_t* dev = media_by_drive(drive);
    if (!dev) return -1;
    (void)media_load(dev);
    media_format_memory(dev);
    dev->dirty = 1;
    if (dev->persistent) return mediafs_sync(dev->drive);
    return 0;
}

int mediafs_selftest(void)
{
    if (mediafs_count() != 3u) return -1;
    if (!mediafs_drive_supported('S') || !mediafs_drive_supported('U') ||
        !mediafs_drive_supported('Y') || !mediafs_drive_supported('F')) return -2;
    if (MEDIAFS_FILE_CAP < 512u || MEDIAFS_MAX_FILES < 3u) return -3;
    if (MEDIAFS_DATA_OFF + MEDIAFS_MAX_FILES * MEDIAFS_FILE_CAP > MEDIAFS_BYTES) return -4;
    if (MEDIAFS_SSD_LBA != LARD_INSTALL_IMAGE_SECTORS) return -5;
    return 0;
}
