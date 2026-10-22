#ifndef PIUX_DESKTOP_H
#define PIUX_DESKTOP_H

#include "../apps/app.h"

#define DESKTOP_MAX_WINDOWS 4

typedef struct {
    int open;
    int minimized;
    int x;
    int y;
    int width;
    int height;
    const app_definition_t *application;
    app_context_t context;
} desktop_window_t;

int desktop_run(void);
int desktop_is_active(void);
int desktop_read_char(void);
int desktop_stop(void);
int desktop_reload(void);

#endif
