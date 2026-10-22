#ifndef PIUX_APP_REGISTRY_H
#define PIUX_APP_REGISTRY_H

#include "../apps/app.h"

int app_registry_count(void);
const app_definition_t *app_registry_get(int index);
const app_definition_t *app_registry_find(const char *identifier);

#endif
