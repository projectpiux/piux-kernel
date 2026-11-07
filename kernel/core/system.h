#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

extern uint32_t system_memory_total_kb;
extern uint32_t system_memory_lower_kb;
extern uint32_t system_memory_upper_kb;
extern uint32_t system_memory_used_kb;
extern uint32_t system_memory_free_kb;

void system_info_init(uint32_t magic, uint32_t multiboot_address);

#endif
