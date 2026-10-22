#ifndef PIUX_APP_H
#define PIUX_APP_H

#include <stdint.h>

typedef struct {
    int x;
    int y;
    int width;
    int height;
} app_rect_t;

typedef struct {
    app_rect_t content;
    void *state;
} app_context_t;

typedef enum {
    APP_EVENT_KEY,
    APP_EVENT_MOUSE
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    int key;
    int x;
    int y;
    int wheel;
    uint8_t buttons;
} app_event_t;

typedef struct app_definition {
    const char *identifier;
    const char *name;
    int (*initialize)(app_context_t *context);
    void (*render)(app_context_t *context);
    void (*handle_event)(app_context_t *context, const app_event_t *event);
    void (*shutdown)(app_context_t *context);
} app_definition_t;

#endif
