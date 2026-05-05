/*
 * LFS - Lard File System
 *
 * Custom filesystem format for LardOS. Flat namespace, contiguous layout.
 *
 * Layout (little-endian):
 *   0x00 magic[4]   = "LFS\0"
 *   0x04 version u8 = 1
 *   0x05 reserved[3]
 *   0x08 file_count u16
 *   For each file entry:
 *     name_len u8
 *     name[name_len] (no NUL)
 *     offset u32   byte offset from start of LFS image
 *     size u32
 *   File data follows entries (each file at its offset).
 */
#pragma once

#include <stdint.h>

#define LFS_MAGIC  0x0053464Cu  /* "LFS\0" LE */

#define LFS_MAX_NAME  64
#define LFS_MAX_FILES 128

/* Mount LFS volume. image must remain valid. Returns 0 on success. */
int lfs_mount(const uint8_t* image, uint32_t size);

/* Lookup file. Returns 1 if found, 0 otherwise. Fills *out_data and *out_size. */
int lfs_lookup(const char* name, const uint8_t** out_data, uint32_t* out_size);

/* List files. Calls cb for each file. */
void lfs_list(void (*cb)(const char* name, uint32_t size, void* user), void* user);
