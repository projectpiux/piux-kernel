#include "taskbar.h"
#include "../apps/app.h"
#include "video.h"
#include "pixelgfx.h"
#include "pixelfont.h"

/* Taskbar in pixel nativi: fascia alta TASKBAR_HEIGHT px sul fondo schermo,
   logo cliccabile a sinistra, pulsanti finestra, hint a destra. */

#define TASKBAR_HEIGHT       26
#define TASKBAR_LOGO_WIDTH   84
#define TASKBAR_TEXT_PAD     6
#define TASKBAR_BUTTON_PAD   10

#define TASKBAR_BG     0x202020u
#define TASKBAR_LOGO_BG 0x0B60B0u
#define TASKBAR_TEXT    0xF0F0F0u
#define TASKBAR_ACTIVE  0x0B60B0u

int taskbar_height(void) { return TASKBAR_HEIGHT; }

static int button_width(const desktop_window_t *window) {
    int width = pixel_text_width(window->application->name);
    return width + TASKBAR_TEXT_PAD * 2;
}

void taskbar_draw(const desktop_window_t *windows, int count, int active_window) {
    int top = video_pixel_height - TASKBAR_HEIGHT;
    int x;
    if (top < 0) return;
    pixel_fill_rect(0, top, video_pixel_width, TASKBAR_HEIGHT, TASKBAR_BG);
    pixel_fill_rect(0, top, TASKBAR_LOGO_WIDTH, TASKBAR_HEIGHT, TASKBAR_LOGO_BG);
    {
        int text_y = top + (TASKBAR_HEIGHT - PIXFONT_HEIGHT) / 2;
        pixel_draw_text(TASKBAR_TEXT_PAD, text_y, "Piux", 0xFFFFFFu);
    }
    x = TASKBAR_LOGO_WIDTH + TASKBAR_BUTTON_PAD;
    for (int index = 0; index < count; index++) {
        int width;
        if (!windows[index].open) continue;
        if (x + button_width(&windows[index]) > video_pixel_width - 130) break;
        width = button_width(&windows[index]);
        if (index == active_window)
            pixel_fill_rect(x, top + 2, width, TASKBAR_HEIGHT - 4, TASKBAR_ACTIVE);
        {
            int text_y = top + (TASKBAR_HEIGHT - PIXFONT_HEIGHT) / 2;
            pixel_draw_text(x + TASKBAR_TEXT_PAD, text_y,
                            windows[index].application->name, TASKBAR_TEXT);
        }
        x += width + TASKBAR_BUTTON_PAD;
    }
    {
        int text_y = top + (TASKBAR_HEIGHT - PIXFONT_HEIGHT) / 2;
        pixel_draw_text(video_pixel_width - 124, text_y, "Ctrl+Alt+Q exit", 0x808080u);
    }
}

int taskbar_hit_test(int x, int y, int rows, const desktop_window_t *windows, int count) {
    int top = video_pixel_height - TASKBAR_HEIGHT;
    int cursor = TASKBAR_LOGO_WIDTH + TASKBAR_BUTTON_PAD;
    (void)rows;
    if (y < top || x < 0 || x >= video_pixel_width) return -1;
    if (x < TASKBAR_LOGO_WIDTH) return -2;
    for (int index = 0; index < count; index++) {
        int width;
        if (!windows[index].open) continue;
        width = button_width(&windows[index]);
        if (cursor + width > video_pixel_width - 130) return -1;
        if (x >= cursor && x < cursor + width) return index;
        cursor += width + TASKBAR_BUTTON_PAD;
    }
    return -1;
}
