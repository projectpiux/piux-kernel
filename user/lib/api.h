#ifndef PIUX_USER_API_H
#define PIUX_USER_API_H

#include <stdint.h>
#include "../../kernel/syscall.h"

#define KEY_ARROW_UP    0x100
#define KEY_ARROW_DOWN  0x101
#define KEY_ARROW_LEFT  0x102
#define KEY_ARROW_RIGHT 0x103
#define KEY_DELETE      0x104
#define MOUSE_BUTTON_LEFT 0x01

/* Font pixel condiviso: glifi 6x13 disegnati a scala PIXFONT_SCALE (2x = 12x26). */
#define PIXFONT_WIDTH  6
#define PIXFONT_HEIGHT 16
#define PIXFONT_SCALE  1
int pixfont_pixel(char character, int x, int y);

typedef syscall_mouse_event_t mouse_event_t;

extern int video_framebuffer_ready;
extern int video_columns;
extern int video_rows;
extern int video_pixel_width;
extern int video_pixel_height;

void user_api_init(void);
int user_get_system_info(syscall_system_info_t *info);
int user_get_auth_info(syscall_auth_info_t *info);
int user_shell_execute(const char *command, char *output, uint32_t capacity);

void video_clear(void);
void video_put_cell(int column, int row, char character, uint8_t color);
void video_put_pixel(int x, int y, uint32_t color, uint8_t alpha);
void video_fill_rect(int x, int y, int width, int height, uint32_t color, uint8_t alpha);
void pixel_draw_text(int x, int y, const char *text, uint32_t color);
void pixel_draw_text_scaled(int x, int y, const char *text, uint32_t color, int scale);
int pixel_text_width(const char *text);
int pixel_text_width_scaled(const char *text, int scale);
static inline void pixel_fill_rect(int x, int y, int w, int h, uint32_t color) {
    video_fill_rect(x, y, w, h, color, 255);
}
void video_capture_region(int x, int y, int width, int height, uint8_t *buffer);
void video_restore_region(int x, int y, int width, int height, const uint8_t *buffer);
int video_begin_frame(void);
int video_begin_region_frame(int x, int y, int width, int height);
void video_present(void);
void video_present_region(void);

int keyboard_try_read_char(void);
int keyboard_is_ctrl_pressed(void);
int keyboard_is_alt_pressed(void);
void mouse_init(void);
int mouse_poll(mouse_event_t *event);

void vga_clear(void);

#endif