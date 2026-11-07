#include <stdint.h>
#include "ramfs.h"

ramfs_t ramfs;

int strncmp_ramfs(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

int strlen_ramfs(const char *s) {
    int i = 0;
    while (s[i] && i < RAMFS_MAX_FILENAME) i++;
    return i;
}

void strcpy_ramfs(char *dest, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs.files[i].used = 0;
        ramfs.files[i].size = 0;
    }
    ramfs.file_count = 0;
    return 0;
}

int ramfs_create_file(const char *filename) {
    int len = strlen_ramfs(filename);
    if (len == 0 || len >= RAMFS_MAX_FILENAME) return -1;

    if (ramfs_find_file(filename) >= 0) {
        return -1;
    }

    if (ramfs.file_count >= RAMFS_MAX_FILES) return -1;

    int idx = ramfs.file_count;
    strcpy_ramfs(ramfs.files[idx].filename, filename, RAMFS_MAX_FILENAME);
    ramfs.files[idx].size = 0;
    ramfs.files[idx].used = 1;
    ramfs.file_count++;

    return idx;
}

int ramfs_write_file(const char *filename, const char *data, uint32_t size) {
    int idx = ramfs_find_file(filename);
    if (idx < 0) return -1;

    if (size > RAMFS_MAX_FILESIZE) size = RAMFS_MAX_FILESIZE;

    for (uint32_t i = 0; i < size; i++) {
        ramfs.files[idx].data[i] = data[i];
    }
    ramfs.files[idx].size = size;

    return size;
}

int ramfs_read_file(const char *filename, char *buffer, uint32_t size) {
    int idx = ramfs_find_file(filename);
    if (idx < 0) return -1;

    uint32_t to_read = ramfs.files[idx].size;
    if (to_read > size) to_read = size;

    for (uint32_t i = 0; i < to_read; i++) {
        buffer[i] = ramfs.files[idx].data[i];
    }

    return to_read;
}

int ramfs_find_file(const char *filename) {
    int len = strlen_ramfs(filename);
    for (int i = 0; i < ramfs.file_count; i++) {
        if (ramfs.files[i].used && strncmp_ramfs(ramfs.files[i].filename, filename, len) == 0) {
            return i;
        }
    }
    return -1;
}

void ramfs_list_files(void (*vga_puts)(const char*)) {
    if (ramfs.file_count == 0) {
        vga_puts("(empty)\n");
        return;
    }

    for (int i = 0; i < ramfs.file_count; i++) {
        if (ramfs.files[i].used) {
            vga_puts("[FILE] ");
            vga_puts(ramfs.files[i].filename);
            vga_puts(" bytes\n");
        }
    }
}