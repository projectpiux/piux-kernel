#include <stdint.h>
#include "module.h"
#include "memory.h"
#include "piux_module.h"

#define CORE_SERVICE_COUNT 9

static const char *const core_names[CORE_SERVICE_COUNT] = {
    "kmalloc", "kfree",
    "memory_range_valid", "memory_user_range_valid",
    "memory_address_space_create", "memory_address_space_activate",
    "memory_address_space_destroy", "memory_user_map", "memory_user_protect"
};

static const void *core_addresses[CORE_SERVICE_COUNT];

void core_services_init(void) {
    core_addresses[0] = PIUX_EXPORT(kmalloc);
    core_addresses[1] = PIUX_EXPORT(kfree);
    core_addresses[2] = PIUX_EXPORT(memory_range_valid);
    core_addresses[3] = PIUX_EXPORT(memory_user_range_valid);
    core_addresses[4] = PIUX_EXPORT(memory_address_space_create);
    core_addresses[5] = PIUX_EXPORT(memory_address_space_activate);
    core_addresses[6] = PIUX_EXPORT(memory_address_space_destroy);
    core_addresses[7] = PIUX_EXPORT(memory_user_map);
    core_addresses[8] = PIUX_EXPORT(memory_user_protect);
    module_register_core_symbols(core_names, core_addresses, CORE_SERVICE_COUNT);
}
