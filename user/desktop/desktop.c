#include <stdint.h>
#include "desktop.h"
#include "app_registry.h"
#include "launcher.h"
#include "taskbar.h"
#include "keyboard.h"
#include "mouse.h"
#include "video.h"

extern void vga_clear(void);
extern const uint8_t _binary_build_cursor_rgba_start[];
extern const uint8_t _binary_build_cursor_rgba_end[];

static desktop_window_t windows[DESKTOP_MAX_WINDOWS];
static int window_count;
static int active_window;
static int desktop_active;
static int cursor_x;
static int cursor_y;
static int cursor_pixel_x;
static int cursor_pixel_y;
static int cursor_buttons;
static int drag_window;
static int resize_window;
static int resize_left;
static int resize_top;
#define DESKTOP_MIN_WIDTH 24
#define DESKTOP_MIN_HEIGHT 12
static int cursor_drawn_x;
static int cursor_drawn_y;
static int cursor_background_valid;
static uint8_t cursor_background[24 * 24 * 4];

static void draw_cursor(void) {
    const uint8_t *pixel = _binary_build_cursor_rgba_start;
    const uint8_t *end = _binary_build_cursor_rgba_end;
    int width = 24;
    int height = 24;
    if (end - pixel < width * height * 4) return;
    if (cursor_background_valid)
        video_restore_region(cursor_drawn_x, cursor_drawn_y, width, height, cursor_background);
    video_capture_region(cursor_pixel_x, cursor_pixel_y, width, height, cursor_background);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *rgba = pixel + (y * width + x) * 4;
            if (rgba[3])
                video_put_pixel(cursor_pixel_x + x, cursor_pixel_y + y,
                               ((uint32_t)rgba[0] << 16) | ((uint32_t)rgba[1] << 8) | rgba[2],
                               rgba[3]);
        }
    }
    cursor_drawn_x = cursor_pixel_x;
    cursor_drawn_y = cursor_pixel_y;
    cursor_background_valid = 1;
}

static void draw_window(int index) {
    desktop_window_t *window = &windows[index];
    int width = window->width;
    int height = window->height;
    int right = window->x + width - 1;
    uint8_t title_color = index == active_window ? 0x1F : 0x17;
    if (!window->open || window->minimized) return;
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            char character = ' ';
            uint8_t color = 0x07;
            if (row == 0 || row == height - 1) character = '-';
            if (column == 0 || column == width - 1) character = '|';
            if ((row == 0 || row == height - 1) &&
                (column == 0 || column == width - 1)) character = '+';
            if (row == 1) color = title_color;
            video_put_cell(window->x + column, window->y + row, character, color);
        }
    }
    for (int column = 0; window->application->name[column] && column < width - 7; column++)
        video_put_cell(window->x + 2 + column, window->y + 1,
                       window->application->name[column], title_color);
    video_put_cell(right - 4, window->y + 1, '_', title_color);
    video_put_cell(right - 2, window->y + 1, 'X', title_color);
    window->context.content.x = window->x + 1;
    window->context.content.y = window->y + 2;
    window->context.content.width = width - 2;
    window->context.content.height = height - 3;
    window->application->render(&window->context);
}

static void draw_wallpaper_region(int x, int y, int width, int height) {
    int right = x + width;
    int bottom = y + height;
    int desktop_height = video_rows - 1;
    const char *brand = "Piux Desktop";
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > video_columns) right = video_columns;
    if (bottom > desktop_height) bottom = desktop_height;
    for (int row = y; row < bottom; row++) {
        int diagonal = video_columns / 5 + row / 3;
        int second_diagonal = video_columns / 2 + row / 5;
        for (int column = x; column < right; column++) {
            uint8_t color = row < desktop_height / 2 ? 0x19 : 0x91;
            char character = ' ';
            if (column >= diagonal && column < diagonal + video_columns / 4) color = 0x39;
            if (column >= second_diagonal && column < second_diagonal + video_columns / 5)
                color = 0xB9;
            if (column >= 2 && column < 2 + 12 && row == 2 &&
                video_columns > 18 && desktop_height > 3) {
                character = brand[column - 2];
                color = 0x1B;
            }
            video_put_cell(column, row, character, color);
        }
    }
}

static void draw_wallpaper(void) {
    draw_wallpaper_region(0, 0, video_columns, video_rows - 1);
}

static void draw_desktop(void) {
    int buffered = video_begin_frame();
    cursor_background_valid = 0;
    draw_wallpaper();
    for (int index = 0; index < window_count; index++)
        if (index != active_window) draw_window(index);
    if (active_window >= 0) draw_window(active_window);
    taskbar_draw(windows, window_count, active_window);
    launcher_draw();
    draw_cursor();
    if (buffered) video_present();
}

static void draw_drag_frame(int index, int old_x, int old_y, int old_width, int old_height) {
    desktop_window_t *window = &windows[index];
    int new_x = window->x;
    int new_y = window->y;
    int left = old_x < new_x ? old_x : new_x;
    int top = old_y < new_y ? old_y : new_y;
    int right = old_x + old_width;
    int bottom = old_y + old_height;
    int new_right = new_x + window->width;
    int new_bottom = new_y + window->height;
    int frame_x;
    int frame_y;
    int frame_right;
    int frame_bottom;
    int buffered;
    if (new_right > right) right = new_right;
    if (new_bottom > bottom) bottom = new_bottom;
    frame_x = left * VIDEO_CELL_WIDTH;
    frame_y = top * VIDEO_CELL_HEIGHT;
    frame_right = right * VIDEO_CELL_WIDTH;
    frame_bottom = bottom * VIDEO_CELL_HEIGHT;
    if (cursor_background_valid) {
        if (cursor_drawn_x < frame_x) frame_x = cursor_drawn_x;
        if (cursor_drawn_y < frame_y) frame_y = cursor_drawn_y;
        if (cursor_drawn_x + 24 > frame_right) frame_right = cursor_drawn_x + 24;
        if (cursor_drawn_y + 24 > frame_bottom) frame_bottom = cursor_drawn_y + 24;
    }
    if (cursor_pixel_x < frame_x) frame_x = cursor_pixel_x;
    if (cursor_pixel_y < frame_y) frame_y = cursor_pixel_y;
    if (cursor_pixel_x + 24 > frame_right) frame_right = cursor_pixel_x + 24;
    if (cursor_pixel_y + 24 > frame_bottom) frame_bottom = cursor_pixel_y + 24;
    buffered = video_begin_region_frame(frame_x, frame_y, frame_right - frame_x,
                                        frame_bottom - frame_y);
    if (cursor_background_valid) {
        video_restore_region(cursor_drawn_x, cursor_drawn_y, 24, 24, cursor_background);
        cursor_background_valid = 0;
    }
    draw_wallpaper_region(old_x, old_y, old_width, old_height);
    for (int candidate = 0; candidate < window_count; candidate++)
        if (candidate != active_window) draw_window(candidate);
    if (active_window >= 0) draw_window(active_window);
    draw_cursor();
    if (buffered) video_present_region();
}

static void focus_window(int index) {
    if (index < 0 || index >= window_count || !windows[index].open) return;
    windows[index].minimized = 0;
    active_window = index;
}

static void close_window(int index) {
    if (index < 0 || index >= window_count || !windows[index].open) return;
    windows[index].application->shutdown(&windows[index].context);
    windows[index].open = 0;
    windows[index].minimized = 0;
    if (active_window == index) {
        active_window = -1;
        for (int candidate = window_count - 1; candidate >= 0; candidate--) {
            if (windows[candidate].open && !windows[candidate].minimized) {
                active_window = candidate;
                break;
            }
        }
    }
}

static int launch_application(const app_definition_t *application) {
    int slot = -1;
    int x;
    int y;
    int width;
    int height;
    if (!application) return -1;
    for (int index = 0; index < window_count; index++) {
        if (windows[index].open && windows[index].application == application) {
            focus_window(index);
            return 0;
        }
    }
    for (int index = 0; index < DESKTOP_MAX_WINDOWS; index++)
        if (!windows[index].open) {
            slot = index;
            break;
        }
    if (slot < 0) return -1;
    width = video_columns - 4;
    if (width > 64) width = 64;
    height = video_rows - 4;
    if (height > 20) height = 20;
    if (width < 24 || height < 12) return -1;
    x = 2 + slot * 3;
    y = 1 + slot;
    if (x + width >= video_columns) x = video_columns - width - 1;
    if (y + height >= video_rows - 1) y = video_rows - height - 2;
    windows[slot].x = x;
    windows[slot].y = y;
    windows[slot].width = width;
    windows[slot].height = height;
    windows[slot].application = application;
    windows[slot].context.content.x = x + 1;
    windows[slot].context.content.y = y + 2;
    windows[slot].context.content.width = width - 2;
    windows[slot].context.content.height = height - 3;
    windows[slot].context.state = 0;
    windows[slot].minimized = 0;
    if (application->initialize(&windows[slot].context) < 0) return -1;
    windows[slot].open = 1;
    if (slot >= window_count) window_count = slot + 1;
    active_window = slot;
    return 0;
}

static int top_window_at(int x, int y) {
    if (active_window >= 0 && windows[active_window].open && !windows[active_window].minimized) {
        desktop_window_t *window = &windows[active_window];
        if (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) return active_window;
    }
    for (int index = window_count - 1; index >= 0; index--) {
        desktop_window_t *window = &windows[index];
        if (index == active_window || !window->open || window->minimized) continue;
        if (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) return index;
    }
    return -1;
}

static void select_launcher_item(int item) {
    if (item >= 0) launch_application(app_registry_get(item));
    launcher_set_open(0);
}

static int handle_mouse_event(const mouse_event_t *event) {
    int old_cell_x = cursor_x;
    int old_cell_y = cursor_y;
    int next_x = cursor_pixel_x + event->dx;
    int next_y = cursor_pixel_y - event->dy;
    int pressed = (event->buttons & MOUSE_BUTTON_LEFT) &&
                  !(cursor_buttons & MOUSE_BUTTON_LEFT);
    int redraw = 0;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x >= video_columns * VIDEO_CELL_WIDTH) next_x = video_columns * VIDEO_CELL_WIDTH - 1;
    if (next_y >= video_rows * VIDEO_CELL_HEIGHT)
        next_y = video_rows * VIDEO_CELL_HEIGHT - 1;
    cursor_pixel_x = next_x;
    cursor_pixel_y = next_y;
    cursor_x = cursor_pixel_x / VIDEO_CELL_WIDTH;
    cursor_y = cursor_pixel_y / VIDEO_CELL_HEIGHT;
    if (drag_window >= 0 && (event->buttons & MOUSE_BUTTON_LEFT)) {
        desktop_window_t *window = &windows[drag_window];
        int old_x = window->x;
        int old_y = window->y;
        window->x += cursor_x - old_cell_x;
        window->y += cursor_y - old_cell_y;
        if (window->x < 0) window->x = 0;
        if (window->y < 0) window->y = 0;
        if (window->x + window->width > video_columns)
            window->x = video_columns - window->width;
        if (window->y + window->height > video_rows - 1)
            window->y = video_rows - window->height - 1;
        if (window->x != old_x || window->y != old_y) redraw = 2;
    }
    if (resize_window >= 0 && (event->buttons & MOUSE_BUTTON_LEFT)) {
        desktop_window_t *window = &windows[resize_window];
        int old_x = window->x;
        int old_y = window->y;
        int old_width = window->width;
        int old_height = window->height;
        int right = window->x + window->width;
        int bottom = window->y + window->height;
        if (resize_left) {
            int new_x = cursor_x;
            if (new_x > right - DESKTOP_MIN_WIDTH) new_x = right - DESKTOP_MIN_WIDTH;
            window->x = new_x;
            window->width = right - new_x;
        } else {
            int width = cursor_x - window->x + 1;
            if (width < DESKTOP_MIN_WIDTH) width = DESKTOP_MIN_WIDTH;
            if (window->x + width > video_columns) width = video_columns - window->x;
            window->width = width;
        }
        if (resize_top) {
            int new_y = cursor_y;
            if (new_y > bottom - DESKTOP_MIN_HEIGHT) new_y = bottom - DESKTOP_MIN_HEIGHT;
            window->y = new_y;
            window->height = bottom - new_y;
        } else {
            int height = cursor_y - window->y + 1;
            if (height < DESKTOP_MIN_HEIGHT) height = DESKTOP_MIN_HEIGHT;
            if (window->y + height > video_rows - 1) height = video_rows - 1 - window->y;
            window->height = height;
        }
        if (window->x != old_x || window->y != old_y ||
            window->width != old_width || window->height != old_height)
            redraw = 2;
    }
    if (pressed) {
        redraw = 1;
        int task = taskbar_hit_test(cursor_x, cursor_y, video_rows, windows, window_count);
        if (task == -2) {
            launcher_set_open(!launcher_is_open());
            drag_window = -1;
        } else if (task >= 0) {
            focus_window(task);
            drag_window = -1;
        } else {
            int item = launcher_hit_test(cursor_x, cursor_y);
            if (launcher_is_open() && item >= 0) select_launcher_item(item);
            else if (launcher_is_open() && item == -2) launcher_set_open(0);
            else {
                if (launcher_is_open()) launcher_set_open(0);
                int index = top_window_at(cursor_x, cursor_y);
                if (index >= 0) {
                    focus_window(index);
                    desktop_window_t *window = &windows[index];
                    if (cursor_y == window->y + 1 && cursor_x == window->x + window->width - 3)
                        close_window(index);
                    else if (cursor_y == window->y + 1 &&
                             cursor_x == window->x + window->width - 5) {
                        window->minimized = 1;
                        active_window = -1;
                        for (int candidate = window_count - 1; candidate >= 0; candidate--) {
                            if (candidate != index && windows[candidate].open &&
                                !windows[candidate].minimized) {
                                active_window = candidate;
                                break;
                            }
                        }
                    } else if ((cursor_x <= window->x + 1 ||
                                cursor_x >= window->x + window->width - 2) &&
                               (cursor_y <= window->y + 1 ||
                                cursor_y >= window->y + window->height - 2) &&
                               !(cursor_y == window->y + 1 &&
                                 cursor_x > window->x + 1 &&
                                 cursor_x < window->x + window->width - 2)) {
                        resize_window = index;
                        resize_left = cursor_x <= window->x + 1;
                        resize_top = cursor_y <= window->y + 1;
                    } else if (cursor_y == window->y + 1) {
                        drag_window = index;
                    } else {
                        app_event_t app_event = {
                            APP_EVENT_MOUSE, 0,
                            cursor_x - window->context.content.x,
                            cursor_y - window->context.content.y,
                            event->wheel, event->buttons
                        };
                        window->application->handle_event(&window->context, &app_event);
                    }
                }
            }
        }
    }
    if (!(event->buttons & MOUSE_BUTTON_LEFT)) {
        drag_window = -1;
        resize_window = -1;
    }
    if (event->wheel && active_window >= 0) {
        app_event_t app_event = {
            APP_EVENT_MOUSE, 0, cursor_x, cursor_y, event->wheel, event->buttons
        };
        windows[active_window].application->handle_event(&windows[active_window].context, &app_event);
        redraw = 1;
    }
    cursor_buttons = event->buttons;
    return redraw;
}

static void handle_key(int key) {
    if (key == 17 && keyboard_is_ctrl_pressed() && keyboard_is_alt_pressed()) {
        desktop_active = 0;
        return;
    }
    if (launcher_is_open()) {
        if (key == KEY_ARROW_UP) launcher_move_selection(-1);
        else if (key == KEY_ARROW_DOWN) launcher_move_selection(1);
        else if (key == '\n') select_launcher_item(launcher_selection());
        else if (key == 27) launcher_set_open(0);
        return;
    }
    if (key == 17 && keyboard_is_ctrl_pressed()) {
        launcher_set_open(1);
        return;
    }
    if (active_window >= 0 && windows[active_window].open) {
        app_event_t event = { APP_EVENT_KEY, key, 0, 0, 0, 0 };
        windows[active_window].application->handle_event(&windows[active_window].context, &event);
    }
}

int desktop_is_active(void) {
    return desktop_active;
}

int desktop_stop(void) {
    if (!desktop_active) return 0;
    desktop_active = 0;
    return 1;
}

int desktop_reload(void) {
    if (!desktop_active) return 0;
    launcher_set_open(0);
    draw_desktop();
    return 1;
}

int desktop_read_char(void) {
    if (!desktop_active) return keyboard_read_char();
    while (desktop_active) {
        mouse_event_t mouse_event;
        int changed = 0;
        int cursor_moved = 0;
        int drag_changed = 0;
        int dragged_window = drag_window >= 0 ? drag_window : resize_window;
        int drag_old_x = dragged_window >= 0 ? windows[dragged_window].x : 0;
        int drag_old_y = dragged_window >= 0 ? windows[dragged_window].y : 0;
        int drag_old_w = dragged_window >= 0 ? windows[dragged_window].width : 0;
        int drag_old_h = dragged_window >= 0 ? windows[dragged_window].height : 0;
        while (mouse_poll(&mouse_event)) {
            int old_x = cursor_pixel_x;
            int old_y = cursor_pixel_y;
            int redraw = handle_mouse_event(&mouse_event);
            if (redraw == 1) changed = 1;
            else if (redraw == 2) drag_changed = 1;
            if (cursor_pixel_x != old_x || cursor_pixel_y != old_y) cursor_moved = 1;
        }
        if (changed) draw_desktop();
        else if (drag_changed && dragged_window >= 0)
            draw_drag_frame(dragged_window, drag_old_x, drag_old_y, drag_old_w, drag_old_h);
        else if (cursor_moved) draw_cursor();
        {
            int key = keyboard_try_read_char();
            if (key >= 0) {
                draw_desktop();
                return key;
            }
        }
    }
    return -1;
}

int desktop_run(void) {
    if (desktop_active) return -1;
    if (video_columns < 36 || video_rows < 16) return -1;
    for (int index = 0; index < DESKTOP_MAX_WINDOWS; index++) {
        windows[index].open = 0;
        windows[index].minimized = 0;
    }
    window_count = 0;
    active_window = -1;
    cursor_x = video_columns / 2;
    cursor_y = (video_rows - 1) / 2;
    cursor_pixel_x = cursor_x * VIDEO_CELL_WIDTH;
    cursor_pixel_y = cursor_y * VIDEO_CELL_HEIGHT;
    cursor_buttons = 0;
    drag_window = -1;
    resize_window = -1;
    launcher_set_open(0);
    mouse_init();
    desktop_active = 1;
    draw_desktop();
    while (desktop_active) {
        mouse_event_t mouse_event;
        int changed = 0;
        int cursor_moved = 0;
        int drag_changed = 0;
        int dragged_window = drag_window >= 0 ? drag_window : resize_window;
        int drag_old_x = dragged_window >= 0 ? windows[dragged_window].x : 0;
        int drag_old_y = dragged_window >= 0 ? windows[dragged_window].y : 0;
        int drag_old_w = dragged_window >= 0 ? windows[dragged_window].width : 0;
        int drag_old_h = dragged_window >= 0 ? windows[dragged_window].height : 0;
        while (mouse_poll(&mouse_event)) {
            int old_x = cursor_pixel_x;
            int old_y = cursor_pixel_y;
            int redraw = handle_mouse_event(&mouse_event);
            if (redraw == 1) changed = 1;
            else if (redraw == 2) drag_changed = 1;
            if (cursor_pixel_x != old_x || cursor_pixel_y != old_y) cursor_moved = 1;
        }
        {
            int key = keyboard_try_read_char();
            if (key >= 0) {
                handle_key(key);
                changed = 1;
            }
        }
        if (changed) draw_desktop();
        else if (drag_changed && dragged_window >= 0)
            draw_drag_frame(dragged_window, drag_old_x, drag_old_y, drag_old_w, drag_old_h);
        else if (cursor_moved) draw_cursor();
    }
    for (int index = 0; index < window_count; index++)
        if (windows[index].open) close_window(index);
    launcher_set_open(0);
    vga_clear();
    return 0;
}
