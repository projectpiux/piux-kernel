#ifndef PIUX_DESKTOP_H
#define PIUX_DESKTOP_H

#include "../apps/app.h"

#define DESKTOP_MAX_WINDOWS 4

/* Le finestre sono in coordinate pixel nativi; bordo e barra titolo in px. */
#define DESKTOP_BORDER      2
#define DESKTOP_TITLEBAR_H  26

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

#endif
