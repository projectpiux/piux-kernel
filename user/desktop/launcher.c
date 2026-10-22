#include "launcher.h"
#include "app_registry.h"
#include "video.h"

static int open;
static int selection;

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
    int top = video_rows - app_registry_count() - 6;
    int width = video_columns < 34 ? video_columns : 34;
    int bottom = top + app_registry_count() + 4;
    if (!open || x < 0 || x >= width || y < top || y > bottom) return -1;
    if (y == top || y == top + 2 || y == bottom) return -2;
    if (y >= top + 3 && y < top + app_registry_count() + 3) return y - top - 3;
    return -1;
}

void launcher_draw(void) {
    int count = app_registry_count();
    int top = video_rows - count - 6;
    int width = video_columns < 34 ? video_columns : 34;
    if (!open || width <= 0) return;
    for (int row = 0; row < count + 5; row++) {
        int y = top + row;
        for (int column = 0; column < width; column++) {
            char character = ' ';
            uint8_t color = 0x78;
            if (row == 0 || row == count + 4) character = '-';
            if (column == 0 || column == width - 1) character = '|';
            if ((row == 0 || row == count + 4) &&
                (column == 0 || column == width - 1)) character = '+';
            if (row == 1) color = 0x1F;
            if (row == 2) character = '-';
            if (row >= 3 && row < count + 3 && row - 3 == selection) color = 0x1F;
            video_put_cell(column, y, character, color);
        }
        if (row == 1) {
            const char *title = "Piux applications";
            for (int index = 0; title[index] && index < width - 2; index++)
                video_put_cell(index + 1, y, title[index], 0x1F);
        } else if (row == 2) {
            for (int index = 1; index < width - 1; index++)
                video_put_cell(index, y, '-', 0x78);
        } else if (row == 3 + count) {
            const char *hint = "Enter: launch";
            for (int index = 0; hint[index] && index < width - 2; index++)
                video_put_cell(index + 1, y, hint[index], 0x70);
        } else if (row >= 3 && row < count + 3) {
            const app_definition_t *application = app_registry_get(row - 3);
            uint8_t color = row - 3 == selection ? 0x1F : 0x70;
            char icon = application->identifier[0];
            video_put_cell(2, y, icon, row - 3 == selection ? 0x2F : 0x17);
            for (int index = 0; application->name[index] && index < width - 6; index++)
                video_put_cell(index + 5, y, application->name[index], color);
        }
    }
}
