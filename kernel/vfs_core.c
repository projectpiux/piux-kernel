#include <stdint.h>
#include "vfs.h"
#include "vfs_core.h"
#include "vfs_module.h"

#ifndef PIUX_MODULE_VFS
#define PIUX_MODULE_VFS
#endif

int vfs_module_ready;

int core_vfs_ready(void) {
    return vfs_open_dispatch != 0;
}

static vfs_console_putc_t core_console_output;

int (*vfs_open_dispatch)(const char *path, int flags);
int (*vfs_read_dispatch)(int fd, void *buffer, uint32_t size);
int (*vfs_write_dispatch)(int fd, const void *buffer, uint32_t size);
int (*vfs_close_dispatch)(int fd);
int (*vfs_seek_dispatch)(int fd, uint32_t offset);
vfs_node_type_t (*vfs_node_type_dispatch)(const char *path);
vfs_console_putc_t (*vfs_console_output_dispatch)(vfs_console_putc_t putc);

int vfs_open(const char *path, int flags) {
    return vfs_open_dispatch ? vfs_open_dispatch(path, flags) : -1;
}

int vfs_read(int fd, void *buffer, uint32_t size) {
    return vfs_read_dispatch ? vfs_read_dispatch(fd, buffer, size) : -1;
}

int vfs_write(int fd, const void *buffer, uint32_t size) {
    return vfs_write_dispatch ? vfs_write_dispatch(fd, buffer, size) : -1;
}

int vfs_close(int fd) {
    return vfs_close_dispatch ? vfs_close_dispatch(fd) : -1;
}

int vfs_seek(int fd, uint32_t offset) {
    return vfs_seek_dispatch ? vfs_seek_dispatch(fd, offset) : -1;
}

vfs_node_type_t vfs_node_type(const char *path) {
    return vfs_node_type_dispatch ? vfs_node_type_dispatch(path) : VFS_NODE_FILE;
}

vfs_console_putc_t vfs_set_console_output(vfs_console_putc_t putc) {
    if (vfs_console_output_dispatch) return vfs_console_output_dispatch(putc);
    {
        vfs_console_putc_t previous = core_console_output;
        core_console_output = putc;
        return previous;
    }
}
