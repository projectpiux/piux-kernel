#ifndef VFS_CORE_H
#define VFS_CORE_H

#include "vfs.h"

int core_vfs_ready(void);

extern int (*vfs_open_dispatch)(const char *path, int flags);
extern int (*vfs_read_dispatch)(int fd, void *buffer, uint32_t size);
extern int (*vfs_write_dispatch)(int fd, const void *buffer, uint32_t size);
extern int (*vfs_close_dispatch)(int fd);
extern int (*vfs_seek_dispatch)(int fd, uint32_t offset);
extern vfs_node_type_t (*vfs_node_type_dispatch)(const char *path);
extern vfs_console_putc_t (*vfs_console_output_dispatch)(vfs_console_putc_t putc);

#endif
