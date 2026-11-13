#include <stdint.h>
#include "desktop.h"
#include "../../kernel/core/syscall.h"
#include "app_registry.h"
#include "launcher.h"
#include "taskbar.h"
#include "keyboard.h"
#include "mouse.h"
#include "video.h"
#include "pixelgfx.h"
#include "pixelfont.h"
#include "taskbar.h"

extern void vga_clear(void);
extern const uint8_t _binary_build_cursor_rgba_start[];
extern const uint8_t _binary_build_cursor_rgba_end[];

#define DESKTOP_MIN_WIDTH       320
#define DESKTOP_MIN_HEIGHT      240
#define DESKTOP_CURSOR_SIZE     24
#define DESKTOP_KEY_DRAIN_LIMIT 32
#define DESKTOP_CASCADE_STEPS   6

#define DESKTOP_KEY_CTRL_Q      17
#define DESKTOP_KEY_TAB         '\t'
#define DESKTOP_KEY_ESC         27

#define REDRAW_FULL             1
#define REDRAW_DRAG             2

static desktop_window_t windows[DESKTOP_MAX_WINDOWS];
static int window_count;
static int active_window;
static int desktop_active;

static int z_order[DESKTOP_MAX_WINDOWS];
static int z_count;

static int cursor_pixel_x;
static int cursor_pixel_y;
static int cursor_buttons;

static int drag_window;
static int drag_grab_x;
static int drag_grab_y;

static int resize_window;
static int resize_x;
static int resize_y;
static int resize_grab_x;
static int resize_grab_y;

static int dirty_window;
static int dirty_x;
static int dirty_y;
static int dirty_width;
static int dirty_height;

static int cursor_drawn_x;
static int cursor_drawn_y;
static int cursor_background_valid;
static uint8_t cursor_background[DESKTOP_CURSOR_SIZE * DESKTOP_CURSOR_SIZE * 4];

static const desktop_window_t *cover[DESKTOP_MAX_WINDOWS];
static int cover_count;

static void cpu_relax(void) {
#if defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("pause");
#endif
}

static int window_visible(const desktop_window_t *window) {
    return window->open && !window->minimized;
}

static int point_in_window(const desktop_window_t *window, int x, int y) {
    return x >= window->x && x < window->x + window->width &&
           y >= window->y && y < window->y + window->height;
}

static void sync_content(desktop_window_t *window) {

    window->context.content.x = window->x + DESKTOP_BORDER;
    window->context.content.y = window->y + DESKTOP_BORDER + DESKTOP_TITLEBAR_H;
    window->context.content.width = window->width - DESKTOP_BORDER * 2;
    window->context.content.height = window->height - DESKTOP_BORDER * 2 - DESKTOP_TITLEBAR_H;
}

static int count_open_windows(void) {
    int total = 0;
    for (int index = 0; index < window_count; index++)
        if (windows[index].open) total++;
    return total;
}

static void z_remove(int index) {
    int position = -1;
    for (int k = 0; k < z_count; k++) {
        if (z_order[k] == index) { position = k; break; }
    }
    if (position < 0) return;
    for (int k = position; k < z_count - 1; k++) z_order[k] = z_order[k + 1];
    z_count--;
}

static void z_raise(int index) {
    z_remove(index);
    if (z_count < DESKTOP_MAX_WINDOWS) z_order[z_count++] = index;
}

static int topmost_visible(int exclude) {
    for (int k = z_count - 1; k >= 0; k--) {
        int index = z_order[k];
        if (index != exclude && window_visible(&windows[index])) return index;
    }
    return -1;
}

static void draw_cursor(void) {
    const uint8_t *pixel = _binary_build_cursor_rgba_start;
    const uint8_t *end = _binary_build_cursor_rgba_end;
    int width = DESKTOP_CURSOR_SIZE;
    int height = DESKTOP_CURSOR_SIZE;
    if (end - pixel < width * height * 4) return;
    if (cursor_background_valid &&
        cursor_drawn_x == cursor_pixel_x && cursor_drawn_y == cursor_pixel_y) return;
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

static const char wallpaper_brand[] = "Piux Desktop";
static const char wallpaper_hint[] = "Ctrl+Q: Apps   Ctrl+Alt+Q: Exit";
#define WALLPAPER_BRAND_LENGTH ((int)sizeof(wallpaper_brand) - 1)
#define WALLPAPER_HINT_LENGTH  ((int)sizeof(wallpaper_hint) - 1)

static void collect_cover(void) {
    cover_count = 0;
    for (int k = 0; k < z_count; k++) {
        const desktop_window_t *window = &windows[z_order[k]];
        if (window_visible(window)) cover[cover_count++] = window;
    }
}

static void draw_wallpaper_region(int x, int y, int width, int height) {
    int right = x + width;
    int bottom = y + height;
    int desktop_height = video_pixel_height - taskbar_height();
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > video_pixel_width) right = video_pixel_width;
    if (bottom > desktop_height) bottom = desktop_height;
    if (right <= x || bottom <= y) return;

    for (int row = y; row < bottom; row += 4) {
        int band_bottom = row + 4;
        if (band_bottom > bottom) band_bottom = bottom;
        uint32_t color = row < desktop_height / 2 ? 0x0C1C30u : 0x101828u;
        if (row >= desktop_height / 2 - 24 && row < desktop_height / 2)
            color = 0x14243C;  
        pixel_fill_rect(x, row, right - x, band_bottom - row, color);
    }

    if (x == 0 && y == 0 && width == video_pixel_width && height >= desktop_height) {
        if (desktop_height > 60) {
            pixel_draw_text(16, 12, wallpaper_brand, 0xBFDFFFu);
            pixel_draw_text(16, 34, wallpaper_hint, 0x607890u);
        }
    }
}

static void draw_window(int index) {
    desktop_window_t *window = &windows[index];
    int width = window->width;
    int height = window->height;
    int active = index == active_window;
    uint32_t frame_color = active ? 0x0B60B0u : 0x404040u;
    uint32_t title_bg    = active ? 0x0B60B0u : 0x282828u;
    uint32_t close_bg    = active ? 0xB02020u : 0x602020u;
    if (!window_visible(window)) return;

    pixel_fill_rect(window->x, window->y, width, height, 0x101010u);
    pixel_draw_rect_border(window->x, window->y, width, height, DESKTOP_BORDER, frame_color);

    pixel_fill_rect(window->x + DESKTOP_BORDER, window->y + DESKTOP_BORDER,
                    width - DESKTOP_BORDER * 2, DESKTOP_TITLEBAR_H, title_bg);
    {
        int text_y = window->y + DESKTOP_BORDER + (DESKTOP_TITLEBAR_H - PIXFONT_HEIGHT) / 2;
        int title_max = width - DESKTOP_BORDER * 2 - 64;
        char name[32];
        int chars = 0;
        while (window->application->name[chars] && chars < 31 &&
               (chars + 1) * (PIXFONT_WIDTH + 1) <= title_max) chars++;
        name[chars] = '\0';
        for (int k = 0; k < chars; k++) name[k] = window->application->name[k];
        pixel_draw_text(window->x + DESKTOP_BORDER + 8, text_y, name, 0xFFFFFFu);
    }

    pixel_fill_rect(window->x + width - DESKTOP_BORDER - 54, window->y + DESKTOP_BORDER + 11, 16, 3, 0xFFFFFFu);
    pixel_fill_rect(window->x + width - DESKTOP_BORDER - 32, window->y + DESKTOP_BORDER + 5, 16, 16, close_bg);
    pixel_draw_text(window->x + width - DESKTOP_BORDER - 29, window->y + DESKTOP_BORDER + 2, "x", 0xFFFFFFu);
    sync_content(window);
    window->application->render(&window->context);
}

static void draw_desktop(void) {
    int buffered = video_begin_frame();
    cursor_background_valid = 0;
    draw_wallpaper_region(0, 0, video_pixel_width, video_pixel_height);
    for (int k = 0; k < z_count; k++) draw_window(z_order[k]);
    taskbar_draw(windows, window_count, active_window);
    launcher_draw();
    draw_cursor();
    if (buffered) video_present();
}

static void mark_dirty(int index, int old_x, int old_y, int old_width, int old_height) {
    if (dirty_window == index) return;
    dirty_window = index;
    dirty_x = old_x;
    dirty_y = old_y;
    dirty_width = old_width;
    dirty_height = old_height;
}

static void draw_drag_frame(void) {
    desktop_window_t *window = &windows[dirty_window];
    int left = dirty_x < window->x ? dirty_x : window->x;
    int top = dirty_y < window->y ? dirty_y : window->y;
    int right = dirty_x + dirty_width;
    int bottom = dirty_y + dirty_height;
    int new_right = window->x + window->width;
    int new_bottom = window->y + window->height;
    int frame_x;
    int frame_y;
    int frame_right;
    int frame_bottom;
    int buffered;
    if (new_right > right) right = new_right;
    if (new_bottom > bottom) bottom = new_bottom;
    frame_x = left;
    frame_y = top;
    frame_right = right;
    frame_bottom = bottom;
    if (cursor_background_valid) {
        if (cursor_drawn_x < frame_x) frame_x = cursor_drawn_x;
        if (cursor_drawn_y < frame_y) frame_y = cursor_drawn_y;
        if (cursor_drawn_x + DESKTOP_CURSOR_SIZE > frame_right)
            frame_right = cursor_drawn_x + DESKTOP_CURSOR_SIZE;
        if (cursor_drawn_y + DESKTOP_CURSOR_SIZE > frame_bottom)
            frame_bottom = cursor_drawn_y + DESKTOP_CURSOR_SIZE;
    }
    if (cursor_pixel_x < frame_x) frame_x = cursor_pixel_x;
    if (cursor_pixel_y < frame_y) frame_y = cursor_pixel_y;
    if (cursor_pixel_x + DESKTOP_CURSOR_SIZE > frame_right)
        frame_right = cursor_pixel_x + DESKTOP_CURSOR_SIZE;
    if (cursor_pixel_y + DESKTOP_CURSOR_SIZE > frame_bottom)
        frame_bottom = cursor_pixel_y + DESKTOP_CURSOR_SIZE;
    buffered = video_begin_region_frame(frame_x, frame_y, frame_right - frame_x,
                                        frame_bottom - frame_y);
    if (cursor_background_valid) {
        video_restore_region(cursor_drawn_x, cursor_drawn_y,
                             DESKTOP_CURSOR_SIZE, DESKTOP_CURSOR_SIZE, cursor_background);
        cursor_background_valid = 0;
    }
    collect_cover();
    draw_wallpaper_region(dirty_x, dirty_y, dirty_width, dirty_height);
    for (int k = 0; k < z_count; k++) {
        const desktop_window_t *candidate = &windows[z_order[k]];
        if (candidate->x >= frame_right || candidate->x + candidate->width <= frame_x ||
            candidate->y >= frame_bottom || candidate->y + candidate->height <= frame_y) continue;
        draw_window(z_order[k]);
    }
    draw_cursor();
    if (buffered) video_present_region();
}

static void focus_window(int index) {
    if (index < 0 || index >= window_count || !windows[index].open) return;
    windows[index].minimized = 0;
    if (active_window != index || (z_count > 0 && z_order[z_count - 1] != index)) z_raise(index);
    active_window = index;
}

static void minimize_window(int index) {
    if (index < 0 || index >= window_count || !windows[index].open) return;
    windows[index].minimized = 1;
    if (active_window == index) active_window = topmost_visible(index);
}

static void close_window(int index) {
    if (index < 0 || index >= window_count || !windows[index].open) return;
    windows[index].application->shutdown(&windows[index].context);
    windows[index].open = 0;
    windows[index].minimized = 0;
    z_remove(index);
    if (drag_window == index) drag_window = -1;
    if (resize_window == index) resize_window = -1;
    if (dirty_window == index) dirty_window = -1;
    if (active_window == index) active_window = topmost_visible(-1);
}

static int launch_application(const app_definition_t *application) {
    int slot = -1;
    int x;
    int y;
    int width;
    int height;
    int cascade;
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
    width = video_pixel_width * 2 / 3;
    if (width > 960) width = 960;
    height = video_pixel_height * 2 / 3;
    if (height > 640) height = 640;
    if (width < DESKTOP_MIN_WIDTH || height < DESKTOP_MIN_HEIGHT) return -1;
    cascade = count_open_windows() % DESKTOP_CASCADE_STEPS;
    x = 40 + cascade * 32;
    y = 30 + cascade * 24;
    if (x + width >= video_pixel_width) x = video_pixel_width - width - 8;
    if (y + height >= video_pixel_height - taskbar_height())
        y = video_pixel_height - taskbar_height() - height - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    windows[slot].x = x;
    windows[slot].y = y;
    windows[slot].width = width;
    windows[slot].height = height;
    windows[slot].application = application;
    sync_content(&windows[slot]);
    windows[slot].context.state = 0;
    windows[slot].minimized = 0;
    if (application->initialize(&windows[slot].context) < 0) return -1;
    windows[slot].open = 1;
    if (slot >= window_count) window_count = slot + 1;
    z_raise(slot);
    active_window = slot;
    return 0;
}

static int top_window_at(int x, int y) {
    for (int k = z_count - 1; k >= 0; k--) {
        int index = z_order[k];
        if (window_visible(&windows[index]) && point_in_window(&windows[index], x, y)) return index;
    }
    return -1;
}

static void select_launcher_item(int item) {
    if (item >= 0) launch_application(app_registry_get(item));
    launcher_set_open(0);
}

static int resize_zone(const desktop_window_t *window, int x, int y, int *zone_x, int *zone_y) {
    int rel_x = x - window->x;
    int rel_y = y - window->y;
    int near_left = rel_x <= 4;
    int near_right = rel_x >= window->width - 5;
    int near_top = rel_y <= 4;
    int near_bottom = rel_y >= window->height - 5;
    *zone_x = 0;
    *zone_y = 0;
    if ((near_left || near_right) && (near_top || near_bottom)) {
        *zone_x = near_left ? -1 : 1;
        *zone_y = near_top ? -1 : 1;
        return 1;
    }
    if (near_left) { *zone_x = -1; return 1; }
    if (near_right) { *zone_x = 1; return 1; }
    if (near_top) { *zone_y = -1; return 1; }
    if (near_bottom) { *zone_y = 1; return 1; }
    return 0;
}

static int update_drag(void) {
    desktop_window_t *window = &windows[drag_window];
    int old_x = window->x;
    int old_y = window->y;
    int new_x = cursor_pixel_x - drag_grab_x;
    int new_y = cursor_pixel_y - drag_grab_y;
    if (new_x + window->width > video_pixel_width) new_x = video_pixel_width - window->width;
    if (new_y + window->height > video_pixel_height - taskbar_height())
        new_y = video_pixel_height - taskbar_height() - window->height;
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x == old_x && new_y == old_y) return 0;
    mark_dirty(drag_window, old_x, old_y, window->width, window->height);
    window->x = new_x;
    window->y = new_y;
    sync_content(window);
    return REDRAW_DRAG;
}

static int update_resize(void) {
    desktop_window_t *window = &windows[resize_window];
    int left = window->x;
    int top = window->y;
    int right = window->x + window->width;
    int bottom = window->y + window->height;
    int changed;
    if (resize_x < 0) {
        left = cursor_pixel_x - resize_grab_x;
        if (left < 0) left = 0;
        if (left > right - DESKTOP_MIN_WIDTH) left = right - DESKTOP_MIN_WIDTH;
    } else if (resize_x > 0) {
        right = cursor_pixel_x + resize_grab_x + 1;
        if (right < left + DESKTOP_MIN_WIDTH) right = left + DESKTOP_MIN_WIDTH;
        if (right > video_pixel_width) right = video_pixel_width;
    }
    if (resize_y < 0) {
        top = cursor_pixel_y - resize_grab_y;
        if (top < 0) top = 0;
        if (top > bottom - DESKTOP_MIN_HEIGHT) top = bottom - DESKTOP_MIN_HEIGHT;
    } else if (resize_y > 0) {
        bottom = cursor_pixel_y + resize_grab_y + 1;
        if (bottom < top + DESKTOP_MIN_HEIGHT) bottom = top + DESKTOP_MIN_HEIGHT;
        if (bottom > video_pixel_height - taskbar_height())
            bottom = video_pixel_height - taskbar_height();
    }
    changed = left != window->x || top != window->y ||
              right - left != window->width || bottom - top != window->height;
    if (!changed) return 0;
    mark_dirty(resize_window, window->x, window->y, window->width, window->height);
    window->x = left;
    window->y = top;
    window->width = right - left;
    window->height = bottom - top;
    sync_content(window);
    return REDRAW_DRAG;
}

static void send_mouse_to_window(desktop_window_t *window, const mouse_event_t *event) {
    app_event_t app_event = {
        APP_EVENT_MOUSE, 0,
        cursor_pixel_x - (window->x + DESKTOP_BORDER),
        cursor_pixel_y - (window->y + DESKTOP_BORDER + DESKTOP_TITLEBAR_H),
        event->wheel, event->buttons
    };
    window->application->handle_event(&window->context, &app_event);
}

static void handle_window_press(const mouse_event_t *event) {
    int index = top_window_at(cursor_pixel_x, cursor_pixel_y);
    desktop_window_t *window;
    int zone_x;
    int zone_y;
    if (index < 0) return;
    focus_window(index);
    window = &windows[index];

    if (cursor_pixel_y >= window->y + DESKTOP_BORDER &&
        cursor_pixel_y < window->y + DESKTOP_BORDER + DESKTOP_TITLEBAR_H &&
        cursor_pixel_x >= window->x + window->width - DESKTOP_BORDER - 36 &&
        cursor_pixel_x < window->x + window->width - DESKTOP_BORDER - 12) {
        close_window(index);
    } else if (cursor_pixel_y >= window->y + DESKTOP_BORDER &&
               cursor_pixel_y < window->y + DESKTOP_BORDER + DESKTOP_TITLEBAR_H &&
               cursor_pixel_x >= window->x + window->width - DESKTOP_BORDER - 58 &&
               cursor_pixel_x < window->x + window->width - DESKTOP_BORDER - 34) {
        minimize_window(index);
    } else if (resize_zone(window, cursor_pixel_x, cursor_pixel_y, &zone_x, &zone_y)) {
        resize_window = index;
        resize_x = zone_x;
        resize_y = zone_y;
        resize_grab_x = zone_x < 0 ? cursor_pixel_x - window->x
                                   : window->x + window->width - 1 - cursor_pixel_x;
        resize_grab_y = zone_y < 0 ? cursor_pixel_y - window->y
                                   : window->y + window->height - 1 - cursor_pixel_y;
    } else if (cursor_pixel_y >= window->y + DESKTOP_BORDER &&
               cursor_pixel_y < window->y + DESKTOP_BORDER + DESKTOP_TITLEBAR_H) {
        drag_window = index;
        drag_grab_x = cursor_pixel_x - window->x;
        drag_grab_y = cursor_pixel_y - window->y;
    } else {
        send_mouse_to_window(window, event);
    }
}

static void handle_press(const mouse_event_t *event) {
    int task = taskbar_hit_test(cursor_pixel_x, cursor_pixel_y, 0, windows, window_count);
    if (task == -2) {
        launcher_set_open(!launcher_is_open());
        drag_window = -1;
        return;
    }
    if (task >= 0) {
        if (launcher_is_open()) launcher_set_open(0);
        if (task == active_window && window_visible(&windows[task])) minimize_window(task);
        else focus_window(task);
        drag_window = -1;
        return;
    }
    if (launcher_is_open()) {
        int item = launcher_hit_test(cursor_pixel_x, cursor_pixel_y);
        if (item >= 0) { select_launcher_item(item); return; }
        if (item == -2) { launcher_set_open(0); return; }
        launcher_set_open(0);
    }
    handle_window_press(event);
}

static int handle_mouse_event(const mouse_event_t *event) {
    int left_down = (event->buttons & MOUSE_BUTTON_LEFT) != 0;
    int pressed = left_down && !(cursor_buttons & MOUSE_BUTTON_LEFT);
    int next_x = cursor_pixel_x + event->dx;
    int next_y = cursor_pixel_y - event->dy;  
    int redraw = 0;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x >= video_pixel_width) next_x = video_pixel_width - 1;
    if (next_y >= video_pixel_height) next_y = video_pixel_height - 1;
    cursor_pixel_x = next_x;
    cursor_pixel_y = next_y;
    if (drag_window >= 0 && left_down) redraw |= update_drag();
    if (resize_window >= 0 && left_down) redraw |= update_resize();
    if (!left_down && (drag_window >= 0 || resize_window >= 0)) {

        drag_window = -1;
        resize_window = -1;
        redraw |= REDRAW_FULL;
    }
    if (pressed) {
        redraw |= REDRAW_FULL;
        handle_press(event);
    }
    if (!left_down) {
        drag_window = -1;
        resize_window = -1;
    }
    if (event->wheel && !launcher_is_open()) {  
        int target = top_window_at(cursor_pixel_x, cursor_pixel_y);
        if (target < 0) target = active_window;
        if (target >= 0 && windows[target].open) {
            send_mouse_to_window(&windows[target], event);
            redraw |= REDRAW_FULL;
        }
    }
    cursor_buttons = event->buttons;
    return redraw;
}

static int pump_mouse(int *redraw) {
    mouse_event_t mouse_event;
    mouse_event_t pending;
    int have_pending = 0;
    int activity = 0;

    while (mouse_poll(&mouse_event)) {
        activity = 1;
        if (have_pending && (mouse_event.buttons & MOUSE_BUTTON_LEFT) ==
                            (pending.buttons & MOUSE_BUTTON_LEFT)) {
            /* Coalesce consecutive motion events so a fast mouse does not build
               a backlog: only the last position of the batch matters. */
            pending.dx += mouse_event.dx;
            pending.dy += mouse_event.dy;
            pending.wheel += mouse_event.wheel;
            pending.buttons = mouse_event.buttons;
            continue;
        }
        if (have_pending) *redraw |= handle_mouse_event(&pending);
        pending = mouse_event;
        have_pending = 1;
    }
    if (have_pending) *redraw |= handle_mouse_event(&pending);
    return activity;
}

static void present_frame(int redraw) {
    if (redraw & REDRAW_FULL) {
        draw_desktop();
        dirty_window = -1;
    } else if ((redraw & REDRAW_DRAG) && dirty_window >= 0 && windows[dirty_window].open) {
        draw_drag_frame();
        dirty_window = -1;
    } else {
        dirty_window = -1;
        draw_cursor();
        video_present();
    }
}

static void cycle_windows(void) {
    for (int k = 0; k < z_count; k++) {
        int index = z_order[k];
        if (index != active_window && window_visible(&windows[index])) {
            focus_window(index);
            return;
        }
    }
}

static void handle_key(int key) {
    if (key == DESKTOP_KEY_CTRL_Q && keyboard_is_ctrl_pressed() && keyboard_is_alt_pressed()) {
        desktop_active = 0;
        return;
    }
    if (launcher_is_open()) {
        if (key == KEY_ARROW_UP) launcher_move_selection(-1);
        else if (key == KEY_ARROW_DOWN) launcher_move_selection(1);
        else if (key == '\n' || key == '\r') select_launcher_item(launcher_selection());
        else if (key == DESKTOP_KEY_ESC) launcher_set_open(0);
        else if (key == DESKTOP_KEY_CTRL_Q && keyboard_is_ctrl_pressed()) launcher_set_open(0);
        return;
    }
    if (key == DESKTOP_KEY_CTRL_Q && keyboard_is_ctrl_pressed()) {
        launcher_set_open(1);
        return;
    }
    if (key == DESKTOP_KEY_TAB && keyboard_is_alt_pressed()) {
        cycle_windows();
        return;
    }
    if (active_window >= 0 && windows[active_window].open) {
        app_event_t event = { APP_EVENT_KEY, key, 0, 0, 0, 0 };
        windows[active_window].application->handle_event(&windows[active_window].context, &event);
    }
}

int desktop_run(void) {
    if (desktop_active) return -1;
    if (video_pixel_width < DESKTOP_MIN_WIDTH || video_pixel_height < DESKTOP_MIN_HEIGHT) return -1;
    for (int index = 0; index < DESKTOP_MAX_WINDOWS; index++) {
        windows[index].open = 0;
        windows[index].minimized = 0;
    }
    window_count = 0;
    z_count = 0;
    active_window = -1;
    cursor_pixel_x = video_pixel_width / 2;
    cursor_pixel_y = (video_pixel_height - taskbar_height()) / 2;
    cursor_buttons = 0;
    cursor_background_valid = 0;
    drag_window = -1;
    resize_window = -1;
    dirty_window = -1;
    launcher_set_open(0);
    mouse_init();
    desktop_active = 1;
    draw_desktop();
    while (desktop_active) {
        int redraw = 0;
        int activity = pump_mouse(&redraw);
        for (int drained = 0; drained < DESKTOP_KEY_DRAIN_LIMIT && desktop_active; drained++) {
            int key = keyboard_try_read_char();
            if (key < 0) break;
            handle_key(key);
            redraw |= REDRAW_FULL;
            activity = 1;
        }
        if (!desktop_active) break;
        if (activity) present_frame(redraw);
        else { syscall_invoke(SYS_YIELD, 0, 0, 0); cpu_relax(); }
    }
    for (int index = 0; index < window_count; index++)
        if (windows[index].open) close_window(index);
    launcher_set_open(0);
    vga_clear();
    return 0;
}