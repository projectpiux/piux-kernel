#ifndef VFS_MODULE_H
#define VFS_MODULE_H

#include "../fs/vfs.h"

extern int vfs_module_ready;

#if defined(PIUX_MODULE_VFS)

#include "piux_module.h"

int vfs_init(void);
int vfs_module_open(const char *path, int flags);
int vfs_module_read(int fd, void *buffer, uint32_t size);
int vfs_module_write(int fd, const void *buffer, uint32_t size);
int vfs_module_close(int fd);
int vfs_module_seek(int fd, uint32_t offset);
vfs_node_type_t vfs_module_node_type(const char *path);
vfs_console_putc_t vfs_module_set_console_output(vfs_console_putc_t putc);

#else

int vfs_open(const char *path, int flags);
int vfs_read(int fd, void *buffer, uint32_t size);
int vfs_write(int fd, const void *buffer, uint32_t size);
int vfs_close(int fd);
int vfs_seek(int fd, uint32_t offset);
vfs_node_type_t vfs_node_type(const char *path);
vfs_console_putc_t vfs_set_console_output(vfs_console_putc_t putc);

#endif

#endif
