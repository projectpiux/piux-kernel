#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define PAGE_SIZE 4096
#define USER_SPACE_BASE 0x80000000U
#define USER_SPACE_SIZE (64U * 1024U * 1024U)
#define USER_SPACE_TOP (USER_SPACE_BASE + USER_SPACE_SIZE)
#define USER_STACK_SIZE (64U * 1024U)

typedef struct {
	uint32_t *directory;
} memory_address_space_t;

void memory_init(uint32_t upper_memory_kb);
void *kmalloc(uint32_t size);
void kfree(void *address);
int memory_range_valid(const void *address, uint32_t size, int user_space);
int memory_user_range_valid(const void *address, uint32_t size, int writable);
int memory_address_space_create(memory_address_space_t *address_space);
void memory_address_space_activate(const memory_address_space_t *address_space);
void memory_address_space_destroy(memory_address_space_t *address_space);
int memory_user_map(uint32_t address, uint32_t size, int writable);
int memory_user_map_into(const memory_address_space_t *address_space, uint32_t address,
                         uint32_t size, int writable);
int memory_user_protect_into(const memory_address_space_t *address_space, uint32_t address,
                             uint32_t size, int writable);
int memory_user_copy_into(const memory_address_space_t *address_space, uint32_t user_address,
                          const void *kernel_buffer, uint32_t size);
int memory_user_protect(uint32_t address, uint32_t size, int writable);
int paging_enabled(void);

#endif