#ifndef PIUX_PIXELGFX_H
#define PIUX_PIXELGFX_H

#include <stdint.h>

void pixel_draw_rect_border(int x, int y, int width, int height, int thickness,
                            uint32_t color);

#endif
