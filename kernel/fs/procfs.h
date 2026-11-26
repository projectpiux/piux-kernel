#ifndef PROCFS_H
#define PROCFS_H

#include <stdint.h>

#define PROC_BUFFER_SIZE 1024

/*
 * Fills the descriptor's snapshot buffer with the generated content of its
 * path. Returns 0 on success, -1 when the entry does not exist.
 */
typedef int (*procfs_read_fn)(void *file);

const char *procfs_file_path(void *file);
char *procfs_file_buffer(void *file);

int procfs_is_proc_path(const char *path);
int procfs_node_type(const char *path);
void procfs_list(void (*puts)(const char *));
int procfs_open(const char *path, int flags);
int procfs_read(int fd, char *buffer, uint32_t size);
int procfs_seek(int fd, uint32_t offset);
void procfs_close(int fd);
int procfs_set_hook(procfs_read_fn hook);

#endif
