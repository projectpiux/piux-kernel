#include <stdint.h>
#include "../tui/wm/wm.h"
#include "../user/desktop/desktop.h"
#include "ext2.h"

typedef void (*nano_puts_t)(const char *);
typedef void (*nano_putc_t)(char);

#define NANO_MAX_FILE 4096
#define NANO_MAX_NAME 128

extern int ext2_is_mounted(void);
extern int ext2_find_inode_by_path(const char *path);
extern int ext2_create_file(const char *path);
extern int ext2_read_file_by_path(const char *path, char *buffer, uint32_t size);
extern int ext2_write_file_by_path(const char *path, const char *buffer, uint32_t size);

extern int ramfs_find_file(const char *filename);
extern int ramfs_create_file(const char *filename);
extern int ramfs_read_file(const char *filename, char *buffer, uint32_t max_size);
extern int ramfs_write_file(const char *filename, const char *data, uint32_t size);

static char nano_text[NANO_MAX_FILE + 1];
static char nano_name[NANO_MAX_NAME];

static void nano_copy(char *dst, const char *src, int limit) {
    int i = 0;
    while (src[i] != '\0' && i < limit - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int nano_in_wm(void) {
    return pwm_command_is_active();
}

static void nano_emit(nano_putc_t putc, char c) {
    putc(c);
    if (nano_in_wm()) pwm_command_refresh();
}

static void nano_render(nano_puts_t puts, nano_putc_t putc, int length, const char *status) {
    if (nano_in_wm()) pwm_command_clear();
    puts("=== nano: ");
    puts(nano_name);
    puts(" === ");
    puts(status);
    putc('\n');
    puts("Ctrl+S save | Ctrl+X exit | Backspace delete\n");
    puts("-------------------------------------------\n");
    for (int i = 0; i < length; i++) putc(nano_text[i]);
    if (length > 0 && nano_text[length - 1] != '\n') putc('\n');
    if (nano_in_wm()) pwm_command_refresh();
}

static int nano_save(int use_ext2, int length) {
    if (use_ext2) {
        if (ext2_find_inode_by_path(nano_name) < 0 && ext2_create_file(nano_name) < 0) return -1;
        return ext2_write_file_by_path(nano_name, nano_text, (uint32_t)length);
    }
    if (ramfs_find_file(nano_name) < 0 && ramfs_create_file(nano_name) < 0) return -1;
    return ramfs_write_file(nano_name, nano_text, (uint32_t)length);
}

void cmd_nano(const char *filename, nano_puts_t puts, nano_putc_t putc) {
    if (filename == 0 || filename[0] == '\0') {
        puts("usage: nano <filename>\n");
        return;
    }

    nano_copy(nano_name, filename, NANO_MAX_NAME);
    for (int i = 0; i <= NANO_MAX_FILE; i++) nano_text[i] = '\0';

    int use_ext2 = ext2_is_mounted();
    int length = 0;
    int exists = use_ext2 ? (ext2_find_inode_by_path(nano_name) >= 0)
                          : (ramfs_find_file(nano_name) >= 0);

    if (exists) {
        int loaded = use_ext2 ? ext2_read_file_by_path(nano_name, nano_text, NANO_MAX_FILE)
                              : ramfs_read_file(nano_name, nano_text, NANO_MAX_FILE);
        if (loaded > 0 && loaded <= NANO_MAX_FILE) length = loaded;
        nano_text[length] = '\0';
    }

    nano_render(puts, putc, length, exists ? "[loaded]" : "[new file]");

    for (;;) {
        int key = desktop_is_active() ? desktop_read_char() : pwm_command_read_char();
        if (key == 0) continue;

        if (key == 19) {
            int written = nano_save(use_ext2, length);
            if (nano_in_wm()) {
                nano_render(puts, putc, length, written >= 0 ? "[saved]" : "[save failed]");
            } else {
                puts(written >= 0 ? "[saved]\n" : "[save failed]\n");
            }
            continue;
        }

        if (key == 24 || key == 3) {
            if (nano_in_wm()) nano_render(puts, putc, length, "[closed]");
            else puts("[closed]\n");
            return;
        }

        if (key == 8 || key == 127) {
            if (length > 0) {
                char removed = nano_text[length - 1];
                length--;
                nano_text[length] = '\0';
                if (nano_in_wm() && removed == '\n') nano_render(puts, putc, length, "[edited]");
                else nano_emit(putc, '\b');
            }
            continue;
        }

        if (key == '\n' || key == '\r') {
            if (length < NANO_MAX_FILE) {
                nano_text[length++] = '\n';
                nano_text[length] = '\0';
                nano_emit(putc, '\n');
            }
            continue;
        }

        if (key >= 32 && key < 127) {
            if (length < NANO_MAX_FILE) {
                nano_text[length++] = (char)key;
                nano_text[length] = '\0';
                nano_emit(putc, (char)key);
            }
            continue;
        }
    }
}