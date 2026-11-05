#include <stdint.h>
#include "pixelgfx.h"
#include "../lib/api.h"

/* Primitive rettangolari per il desktop. pixel_fill_rect sta in api.h. */

void pixel_draw_rect_border(int x, int y, int width, int height, int thickness,
                            uint32_t color) {
    if (width <= 0 || height <= 0 || thickness <= 0) return;
    if (thickness * 2 > width) thickness = width / 2;
    if (thickness * 2 > height) thickness = height / 2;
    pixel_fill_rect(x, y, width, thickness, color);
    pixel_fill_rect(x, y + height - thickness, width, thickness, color);
    pixel_fill_rect(x, y, thickness, height, color);
    pixel_fill_rect(x + width - thickness, y, thickness, height, color);
}
