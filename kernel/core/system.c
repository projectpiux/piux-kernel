#include "system.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

uint32_t system_memory_total_kb;
uint32_t system_memory_lower_kb;
uint32_t system_memory_upper_kb;
uint32_t system_memory_used_kb;
uint32_t system_memory_free_kb;

extern char __kernel_end;

typedef struct {
    uint32_t flags;
    uint32_t memory_lower;
    uint32_t memory_upper;
} multiboot_memory_info_t;

void system_info_init(uint32_t magic, uint32_t multiboot_address) {
    multiboot_memory_info_t *info;
    system_memory_total_kb = 0;
    system_memory_lower_kb = 0;
    system_memory_upper_kb = 0;
    system_memory_used_kb = 0;
    system_memory_free_kb = 0;
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || multiboot_address == 0) return;
    info = (multiboot_memory_info_t *)multiboot_address;
    if ((info->flags & 1u) == 0) return;
    system_memory_lower_kb = info->memory_lower;
    system_memory_upper_kb = info->memory_upper;
    system_memory_total_kb = info->memory_lower + info->memory_upper + 1024;
    if ((uint32_t)(uintptr_t)&__kernel_end > 0x00100000) {
        system_memory_used_kb = ((uint32_t)(uintptr_t)&__kernel_end - 0x00100000 + 1023) / 1024;
    }
    system_memory_free_kb = system_memory_total_kb > system_memory_used_kb ?
                            system_memory_total_kb - system_memory_used_kb : 0;
}
