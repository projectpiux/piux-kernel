#ifndef ELF_H
#define ELF_H

#include <stdint.h>

int elf_load(const char *path, uint32_t *entry_point);

#endif
