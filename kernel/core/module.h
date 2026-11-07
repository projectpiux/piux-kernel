#ifndef KERNEL_MODULE_H
#define KERNEL_MODULE_H

#include <stdint.h>
#include "piux_module.h"

#define MODULE_MAX 16

void module_init(void);

int module_register_core_symbols(const char *const *names,
                                 const void *const *addresses,
                                 uint32_t count);

typedef void (*module_exports_fn)(const char *const **names,
                                  const void *const **addresses,
                                  uint32_t *count);

int module_load(const char *path);
int module_load_by_name(const char *name);

int module_unload(const char *name);
int module_reload(const char *name);
int module_count(void);
const module_image_t *module_at(int index);
const module_image_t *module_find(const char *name);

void module_describe(const module_image_t *image, char *buffer, uint32_t size);

extern char module_last_detail[80];

#endif
