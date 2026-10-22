#include "app_registry.h"
#include "../apps/systeminfo/systeminfo.h"
#include "../apps/terminal/terminal.h"

static const app_definition_t *const applications[] = {
    &terminal_application,
    &systeminfo_application
};

int app_registry_count(void) {
    return (int)(sizeof(applications) / sizeof(applications[0]));
}

const app_definition_t *app_registry_get(int index) {
    if (index < 0 || index >= app_registry_count()) return 0;
    return applications[index];
}

const app_definition_t *app_registry_find(const char *identifier) {
    if (!identifier) return 0;
    for (int index = 0; index < app_registry_count(); index++) {
        const app_definition_t *application = applications[index];
        int character = 0;
        while (application->identifier[character] &&
               identifier[character] == application->identifier[character]) character++;
        if (identifier[character] == application->identifier[character]) return application;
    }
    return 0;
}
