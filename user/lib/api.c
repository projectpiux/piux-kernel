#include "api.h"

#define VIDEO_BATCH_CAPACITY 512
#define VIDEO_OP_CELL 1
#define VIDEO_OP_PIXEL 2
#define VIDEO_OP_RECT 3

int video_framebuffer_ready;
int video_columns;
int video_rows;
int video_pixel_width;
int video_pixel_height;

static syscall_video_op_t video_batch[VIDEO_BATCH_CAPACITY];
static uint32_t video_batch_count;

static void flush_video_batch(void) {
    if (!video_batch_count) return;
    syscall_invoke(SYS_VIDEO_DRAW, (uint32_t)(uintptr_t)video_batch,
                   video_batch_count, 0);
    video_batch_count = 0;
}

static void append_video_op(const syscall_video_op_t *operation) {
    if (video_batch_count == VIDEO_BATCH_CAPACITY) flush_video_batch();
    video_batch[video_batch_count++] = *operation;
}

void user_api_init(void) {
    syscall_video_info_t info;
    if (syscall_invoke(SYS_VIDEO_INFO, (uint32_t)(uintptr_t)&info, 0, 0) < 0) return;
    video_framebuffer_ready = info.framebuffer_ready;
    video_columns = info.columns;
    video_rows = info.rows;
    video_pixel_width = info.pixel_width;
    video_pixel_height = info.pixel_height;
}

int user_get_system_info(syscall_system_info_t *info) {
    return syscall_invoke(SYS_SYSTEM_INFO, (uint32_t)(uintptr_t)info, 0, 0);
}

int user_get_auth_info(syscall_auth_info_t *info) {
    return syscall_invoke(SYS_AUTH_INFO, (uint32_t)(uintptr_t)info, 0, 0);
}

int user_shell_execute(const char *command, char *output, uint32_t capacity) {
    return syscall_invoke(SYS_SHELL_EXEC, (uint32_t)(uintptr_t)command,
                          (uint32_t)(uintptr_t)output, capacity);
}

void video_clear(void) {
    flush_video_batch();
    syscall_invoke(SYS_VIDEO_CLEAR, 0, 0, 0);
}

void vga_clear(void) { video_clear(); }

void video_put_cell(int column, int row, char character, uint8_t color) {
    syscall_video_op_t operation = { VIDEO_OP_CELL, column, row, 0, 0, color, character, 0, 0 };
    append_video_op(&operation);
}

void video_put_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    syscall_video_op_t operation = { VIDEO_OP_PIXEL, x, y, color, alpha, 0, 0, 0, 0 };
    append_video_op(&operation);
}

void video_fill_rect(int x, int y, int width, int height, uint32_t color, uint8_t alpha) {
    syscall_video_op_t operation = { VIDEO_OP_RECT, x, y, color, alpha, 0, 0, (uint32_t)width, (uint32_t)height };
    append_video_op(&operation);
}

static void video_region_call(uint32_t number, int x, int y, int width, int height,
                              uint8_t *buffer) {
    syscall_video_region_t region = { x, y, width, height, buffer };
    flush_video_batch();
    syscall_invoke(number, (uint32_t)(uintptr_t)&region, 0, 0);
}

void video_capture_region(int x, int y, int width, int height, uint8_t *buffer) {
    video_region_call(SYS_VIDEO_CAPTURE, x, y, width, height, buffer);
}

void video_restore_region(int x, int y, int width, int height, const uint8_t *buffer) {
    video_region_call(SYS_VIDEO_RESTORE, x, y, width, height, (uint8_t *)buffer);
}

int video_begin_frame(void) {
    flush_video_batch();
    return syscall_invoke(SYS_VIDEO_BEGIN_FRAME, 0, 0, 0);
}

int video_begin_region_frame(int x, int y, int width, int height) {
    syscall_video_region_t region = { x, y, width, height, 0 };
    flush_video_batch();
    return syscall_invoke(SYS_VIDEO_BEGIN_REGION, (uint32_t)(uintptr_t)&region, 0, 0);
}

void video_present(void) {
    flush_video_batch();
    syscall_invoke(SYS_VIDEO_PRESENT, 0, 0, 0);
}

void video_present_region(void) {
    flush_video_batch();
    syscall_invoke(SYS_VIDEO_PRESENT, 1, 0, 0);
}

int keyboard_try_read_char(void) {
    return syscall_invoke(SYS_KEYBOARD_POLL, 0, 0, 0);
}

int keyboard_is_ctrl_pressed(void) {
    return (syscall_invoke(SYS_KEYBOARD_MODIFIERS, 0, 0, 0) & 1) != 0;
}

int keyboard_is_alt_pressed(void) {
    return (syscall_invoke(SYS_KEYBOARD_MODIFIERS, 0, 0, 0) & 2) != 0;
}

void mouse_init(void) {
    syscall_invoke(SYS_MOUSE_INIT, 0, 0, 0);
}

int mouse_poll(mouse_event_t *event) {
    return syscall_invoke(SYS_MOUSE_POLL, (uint32_t)(uintptr_t)event, 0, 0);
}

/* Testo a pixel con il font condiviso 6x13, scalabile. scale=2 -> 12x26. */
void pixel_draw_text_scaled(int x, int y, const char *text, uint32_t color, int scale) {
    int advance;
    if (!text || scale < 1) return;
    advance = (PIXFONT_WIDTH + 1) * scale;
    for (int index = 0; text[index]; index++) {
        for (int row = 0; row < PIXFONT_HEIGHT; row++) {
            for (int column = 0; column < PIXFONT_WIDTH; column++) {
                if (pixfont_pixel(text[index], column, row)) {
                    /* blocco scale x scale per pixel del glifo */
                    video_fill_rect(x + index * advance + column * scale,
                                    y + row * scale, scale, scale, color, 255);
                }
            }
        }
    }
}

void pixel_draw_text(int x, int y, const char *text, uint32_t color) {
    pixel_draw_text_scaled(x, y, text, color, PIXFONT_SCALE);
}

int pixel_text_width_scaled(const char *text, int scale) {
    int length = 0;
    if (text) while (text[length]) length++;
    return length * (PIXFONT_WIDTH + 1) * scale;
}

int pixel_text_width(const char *text) {
    return pixel_text_width_scaled(text, PIXFONT_SCALE);
}