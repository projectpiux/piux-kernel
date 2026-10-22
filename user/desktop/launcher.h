#ifndef PIUX_LAUNCHER_H
#define PIUX_LAUNCHER_H

#include "desktop.h"

void launcher_set_open(int open);
int launcher_is_open(void);
int launcher_selection(void);
void launcher_move_selection(int direction);
int launcher_hit_test(int x, int y);
void launcher_draw(void);

#endif
