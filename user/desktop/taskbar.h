#ifndef PIUX_TASKBAR_H
#define PIUX_TASKBAR_H

#include "desktop.h"

void taskbar_draw(const desktop_window_t *windows, int count, int active_window);
int taskbar_hit_test(int x, int y, int rows, const desktop_window_t *windows, int count);

#endif
