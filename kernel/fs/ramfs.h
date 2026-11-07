#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

#define RAMFS_MAX_FILES 256
#define RAMFS_MAX_FILENAME 64
#define RAMFS_MAX_FILESIZE 8192

typedef struct {
    char filename[RAMFS_MAX_FILENAME];
    uint8_t data[RAMFS_MAX_FILESIZE];
    uint32_t size;
    int used;
} ramfs_file_t;

typedef struct {
    ramfs_file_t files[RAMFS_MAX_FILES];
    uint32_t file_count;
} ramfs_t;

int ramfs_init(void);
int ramfs_create_file(const char *filename);
int ramfs_write_file(const char *filename, const char *data, uint32_t size);
int ramfs_read_file(const char *filename, char *buffer, uint32_t size);
int ramfs_find_file(const char *filename);
void ramfs_list_files(void (*vga_puts)(const char*));

#endif
