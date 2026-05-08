/*
 * mkiso - Build a bootable El Torito ISO from a LardOS raw floppy image.
 *
 * This deliberately avoids host ISO tools. The output is a minimal ISO-9660
 * image with a boot catalog and a 1.2M/1.44M/2.88M floppy-emulation boot image.
 *
 * Usage: mkiso -o lardos.iso -i lardos.img [-v VOLUME_ID]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISO_SECTOR_SIZE 2048u
#define ISO_SYSTEM_SECTORS 16u
#define ISO_PVD_SECTOR 16u
#define ISO_BOOT_SECTOR 17u
#define ISO_TERM_SECTOR 18u
#define ISO_PATH_LE_SECTOR 19u
#define ISO_PATH_BE_SECTOR 20u
#define ISO_ROOT_SECTOR 21u
#define ISO_BOOTCAT_SECTOR 22u
#define ISO_BOOTIMG_SECTOR 23u
#define ROOT_DIR_SIZE ISO_SECTOR_SIZE
#define BOOTCAT_SIZE ISO_SECTOR_SIZE

static int read_file(const char* path, uint8_t** data, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return -1;
    }
    *data = (uint8_t*)malloc((size_t)n);
    if (!*data) {
        fclose(f);
        return -1;
    }
    if (fread(*data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(*data);
        *data = 0;
        return -1;
    }
    fclose(f);
    *len = (size_t)n;
    return 0;
}

static void put_le16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_be16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put_le32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static void put_723(uint8_t* p, uint16_t v)
{
    put_le16(p, v);
    put_be16(p + 2, v);
}

static void put_733(uint8_t* p, uint32_t v)
{
    put_le32(p, v);
    put_be32(p + 4, v);
}

static void pad_text(uint8_t* p, size_t n, const char* s)
{
    size_t i = 0;
    while (i < n && s && s[i]) {
        p[i] = (uint8_t)s[i];
        i++;
    }
    while (i < n) {
        p[i++] = ' ';
    }
}

static void put_date7(uint8_t* p)
{
    p[0] = (uint8_t)(2026 - 1900);
    p[1] = 5;
    p[2] = 7;
    p[3] = 0;
    p[4] = 0;
    p[5] = 0;
    p[6] = 0;
}

static void put_date17(uint8_t* p)
{
    memcpy(p, "2026050700000000", 16);
    p[16] = 0;
}

static uint32_t dir_record(uint8_t* p, uint32_t extent, uint32_t size, uint8_t flags, const uint8_t* name, uint8_t name_len)
{
    uint32_t len = 33u + (uint32_t)name_len + ((name_len & 1u) ? 0u : 1u);
    p[0] = (uint8_t)len;
    p[1] = 0;
    put_733(p + 2, extent);
    put_733(p + 10, size);
    put_date7(p + 18);
    p[25] = flags;
    p[26] = 0;
    p[27] = 0;
    put_723(p + 28, 1);
    p[32] = name_len;
    memcpy(p + 33, name, name_len);
    if ((name_len & 1u) == 0) {
        p[33 + name_len] = 0;
    }
    return len;
}

static void write_path_table(uint8_t* p, int big_endian)
{
    p[0] = 1;
    p[1] = 0;
    if (big_endian) {
        put_be32(p + 2, ISO_ROOT_SECTOR);
        put_be16(p + 6, 1);
    } else {
        put_le32(p + 2, ISO_ROOT_SECTOR);
        put_le16(p + 6, 1);
    }
    p[8] = 0;
    p[9] = 0;
}

static void write_pvd(uint8_t* p, uint32_t total_sectors, const char* volume_id)
{
    static const uint8_t root_name[1] = { 0 };
    p[0] = 1;
    memcpy(p + 1, "CD001", 5);
    p[6] = 1;
    pad_text(p + 8, 32, "LARDOS");
    pad_text(p + 40, 32, volume_id);
    put_733(p + 80, total_sectors);
    put_723(p + 120, 1);
    put_723(p + 124, 1);
    put_723(p + 128, ISO_SECTOR_SIZE);
    put_733(p + 132, 10);
    put_le32(p + 140, ISO_PATH_LE_SECTOR);
    put_le32(p + 144, 0);
    put_be32(p + 148, ISO_PATH_BE_SECTOR);
    put_be32(p + 152, 0);
    dir_record(p + 156, ISO_ROOT_SECTOR, ROOT_DIR_SIZE, 2, root_name, 1);
    pad_text(p + 190, 128, volume_id);
    pad_text(p + 318, 128, "LARDOS");
    pad_text(p + 446, 128, "LARDOS MKISO");
    pad_text(p + 574, 128, "LARDOS");
    put_date17(p + 813);
    put_date17(p + 830);
    put_date17(p + 864);
    p[881] = 1;
}

static void write_boot_record(uint8_t* p)
{
    p[0] = 0;
    memcpy(p + 1, "CD001", 5);
    p[6] = 1;
    memcpy(p + 7, "EL TORITO SPECIFICATION", 23);
    put_le32(p + 71, ISO_BOOTCAT_SECTOR);
}

static void write_terminator(uint8_t* p)
{
    p[0] = 255;
    memcpy(p + 1, "CD001", 5);
    p[6] = 1;
}

static void write_boot_catalog(uint8_t* p, uint8_t media_type)
{
    p[0] = 1;
    p[1] = 0;
    pad_text(p + 4, 24, "LardOS");
    p[30] = 0x55;
    p[31] = 0xAA;

    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += (uint16_t)(p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8));
    }
    put_le16(p + 28, (uint16_t)(0u - sum));

    p[32] = 0x88;
    p[33] = media_type;
    put_le16(p + 34, 0);
    p[36] = (media_type == 4) ? 0x80 : 0;
    p[37] = 0;
    put_le16(p + 38, 1);
    put_le32(p + 40, ISO_BOOTIMG_SECTOR);
}

static void write_root_dir(uint8_t* p, size_t img_len)
{
    static const uint8_t self_name[1] = { 0 };
    static const uint8_t parent_name[1] = { 1 };
    static const uint8_t bootcat_name[] = "BOOT.CAT;1";
    static const uint8_t img_name[] = "LARDOS.IMG;1";
    uint32_t off = 0;
    off += dir_record(p + off, ISO_ROOT_SECTOR, ROOT_DIR_SIZE, 2, self_name, 1);
    off += dir_record(p + off, ISO_ROOT_SECTOR, ROOT_DIR_SIZE, 2, parent_name, 1);
    off += dir_record(p + off, ISO_BOOTCAT_SECTOR, BOOTCAT_SIZE, 0, bootcat_name, (uint8_t)(sizeof(bootcat_name) - 1));
    dir_record(p + off, ISO_BOOTIMG_SECTOR, (uint32_t)img_len, 0, img_name, (uint8_t)(sizeof(img_name) - 1));
}

int main(int argc, char** argv)
{
    const char* out_path = 0;
    const char* img_path = 0;
    const char* volume_id = "LARDOS";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            img_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            volume_id = argv[++i];
        }
    }

    if (!out_path || !img_path) {
        fprintf(stderr, "Usage: mkiso -o <out.iso> -i <boot.img> [-v VOLUME_ID]\n");
        return 1;
    }

    uint8_t* img = 0;
    size_t img_len = 0;
    if (read_file(img_path, &img, &img_len) != 0) {
        fprintf(stderr, "mkiso: cannot read %s\n", img_path);
        return 1;
    }

    uint8_t media_type = 0;
    if (img_len == 1200u * 1024u) media_type = 1;
    else if (img_len == 1440u * 1024u) media_type = 2;
    else if (img_len == 2880u * 1024u) media_type = 3;
    else {
        fprintf(stderr, "mkiso: boot image must be 1.2M, 1.44M, or 2.88M for floppy emulation\n");
        free(img);
        return 1;
    }

    uint32_t img_sectors = (uint32_t)((img_len + ISO_SECTOR_SIZE - 1u) / ISO_SECTOR_SIZE);
    uint32_t total_sectors = ISO_BOOTIMG_SECTOR + img_sectors;
    size_t iso_len = (size_t)total_sectors * ISO_SECTOR_SIZE;
    uint8_t* iso = (uint8_t*)calloc(1, iso_len);
    if (!iso) {
        fprintf(stderr, "mkiso: out of memory\n");
        free(img);
        return 1;
    }

    write_pvd(iso + (size_t)ISO_PVD_SECTOR * ISO_SECTOR_SIZE, total_sectors, volume_id);
    write_boot_record(iso + (size_t)ISO_BOOT_SECTOR * ISO_SECTOR_SIZE);
    write_terminator(iso + (size_t)ISO_TERM_SECTOR * ISO_SECTOR_SIZE);
    write_path_table(iso + (size_t)ISO_PATH_LE_SECTOR * ISO_SECTOR_SIZE, 0);
    write_path_table(iso + (size_t)ISO_PATH_BE_SECTOR * ISO_SECTOR_SIZE, 1);
    write_root_dir(iso + (size_t)ISO_ROOT_SECTOR * ISO_SECTOR_SIZE, img_len);
    write_boot_catalog(iso + (size_t)ISO_BOOTCAT_SECTOR * ISO_SECTOR_SIZE, media_type);
    memcpy(iso + (size_t)ISO_BOOTIMG_SECTOR * ISO_SECTOR_SIZE, img, img_len);

    FILE* f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "mkiso: cannot write %s\n", out_path);
        free(iso);
        free(img);
        return 1;
    }
    size_t n = fwrite(iso, 1, iso_len, f);
    fclose(f);
    free(iso);
    free(img);

    if (n != iso_len) {
        fprintf(stderr, "mkiso: write failed\n");
        return 1;
    }
    return 0;
}
