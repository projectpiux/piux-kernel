#include "taskbar.h"
#include "../apps/app.h"
#include "video.h"

static int string_length(const char *text) {
    int length = 0;
    while (text[length]) length++;
    return length;
}

static void draw_text(int x, int y, const char *text, int limit, uint8_t color) {
    for (int index = 0; text[index] && index < limit; index++)
        video_put_cell(x + index, y, text[index], color);
}

void taskbar_draw(const desktop_window_t *windows, int count, int active_window) {
    int row = video_rows - 1;
    int column = 0;
    if (row < 0) return;
    while (column < video_columns) video_put_cell(column++, row, ' ', 0x78);
    draw_text(0, row, " Piux  ", video_columns, 0x1F);
    column = 7;
    for (int index = 0; index < count && column < video_columns; index++) {
        const char *name;
        int length;
        if (!windows[index].open) continue;
        if (column >= video_columns - 17) break;
        name = windows[index].application->name;
        length = string_length(name);
        if (length > 10) length = 10;
        video_put_cell(column++, row, ' ', 0x70);
        for (int character = 0; character < length && column < video_columns - 17; character++)
            video_put_cell(column++, row, name[character], index == active_window ? 0x1F : 0x70);
        if (column < video_columns - 17) video_put_cell(column++, row, ' ', 0x70);
        if (column < video_columns - 17) video_put_cell(column++, row, ' ', 0x70);
    }
    if (video_columns >= 19)
        draw_text(video_columns - 17, row, "Ctrl+Alt+Q exit", 15, 0x70);
}

int taskbar_hit_test(int x, int y, int rows, const desktop_window_t *windows, int count) {
    int column = 7;
    if (y != rows - 1 || x < 0 || x >= video_columns) return -1;
    if (x < 7) return -2;
    for (int index = 0; index < count; index++) {
        int width;
        if (!windows[index].open) continue;
        if (column >= video_columns - 17) return -1;
        width = string_length(windows[index].application->name);
        if (width > 10) width = 10;
        width += 2;
        if (x >= column && x < column + width) return index;
        column += width + 1;
    }
    return -1;
}
