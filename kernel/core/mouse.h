#ifndef PIUX_MOUSE_H
#define PIUX_MOUSE_H

#include <stdint.h>

typedef struct {
    int8_t dx;
    int8_t dy;
    int8_t wheel;
    uint8_t buttons;
} mouse_event_t;

#define MOUSE_BUTTON_LEFT 0x01

void mouse_init(void);
int mouse_poll(mouse_event_t *event);
void mouse_irq_handler(void);

#endif
