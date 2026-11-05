#include "launcher.h"
#include "taskbar.h"
#include "app_registry.h"
#include "video.h"
#include "pixelfont.h"
#include "../lib/api.h"
#include "pixelgfx.h"

/* Launcher in pixel nativi, ancorato in basso a sinistra sopra la taskbar. */

#define LAUNCHER_WIDTH     250
#define LAUNCHER_ITEM_H    22
#define LAUNCHER_HEADER_H  22
#define LAUNCHER_FOOTER_H  18

static int open;
static int selection;

static int launcher_top(void) {
    int height = LAUNCHER_HEADER_H + app_registry_count() * LAUNCHER_ITEM_H + LAUNCHER_FOOTER_H;
    int top = video_pixel_height - taskbar_height() - height - 8;
    return top < 0 ? 0 : top;
}

void launcher_set_open(int value) {
    open = value != 0;
    if (selection >= app_registry_count()) selection = 0;
}

int launcher_is_open(void) {
    return open;
}

int launcher_selection(void) {
    return selection;
}

void launcher_move_selection(int direction) {
    int count = app_registry_count();
    if (count == 0) return;
    selection += direction;
    if (selection < 0) selection = count - 1;
    if (selection >= count) selection = 0;
}

int launcher_hit_test(int x, int y) {
    int top = launcher_top();
    int width = LAUNCHER_WIDTH < video_pixel_width ? LAUNCHER_WIDTH : video_pixel_width;
    int items_bottom = top + LAUNCHER_HEADER_H + app_registry_count() * LAUNCHER_ITEM_H;
    if (!open || x < 0 || x >= width || y < top ||
        y >= items_bottom + LAUNCHER_FOOTER_H) return -1;
    if (y < top + LAUNCHER_HEADER_H) return -2;          /* header: chiudi */
    if (y < items_bottom) return (y - top - LAUNCHER_HEADER_H) / LAUNCHER_ITEM_H;
    return -1;
}

void launcher_draw(void) {
    int count = app_registry_count();
    int top = launcher_top();
    int width = LAUNCHER_WIDTH < video_pixel_width ? LAUNCHER_WIDTH : video_pixel_width;
    int items_bottom;
    if (!open || width <= 0) return;
    items_bottom = top + LAUNCHER_HEADER_H + count * LAUNCHER_ITEM_H;
    pixel_fill_rect(0, top, width, items_bottom + LAUNCHER_FOOTER_H - top, 0x242830u);
    pixel_draw_rect_border(0, top, width, items_bottom + LAUNCHER_FOOTER_H - top,
                           2, 0x0B60B0u);
    pixel_fill_rect(2, top + 2, width - 4, LAUNCHER_HEADER_H - 4, 0x0B60B0u);
    pixel_draw_text(10, top + (LAUNCHER_HEADER_H - PIXFONT_HEIGHT) / 2,
                    "Piux applications", 0xFFFFFFu);
    for (int index = 0; index < count; index++) {
        const app_definition_t *application = app_registry_get(index);
        int item_y = top + LAUNCHER_HEADER_H + index * LAUNCHER_ITEM_H;
        if (index == selection)
            pixel_fill_rect(4, item_y, width - 8, LAUNCHER_ITEM_H, 0x0B60B0u);
        pixel_draw_text(12, item_y + (LAUNCHER_ITEM_H - PIXFONT_HEIGHT) / 2,
                        application->name, index == selection ? 0xFFFFFFu : 0xC0C8D0u);
    }
    pixel_draw_text(10, items_bottom + (LAUNCHER_FOOTER_H - PIXFONT_HEIGHT) / 2,
                    "Enter: launch   Esc: close", 0x808890u);
}
