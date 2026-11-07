#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include "memory.h"

int elf_load(const char *path, uint32_t *entry_point);
int elf_load_into(const memory_address_space_t *address_space, const char *path,
                  uint32_t *entry_point);

#endif
