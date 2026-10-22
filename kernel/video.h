#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#define VIDEO_CELL_WIDTH 8
#define VIDEO_CELL_HEIGHT 16

extern int video_framebuffer_ready;
extern int video_columns;
extern int video_rows;
extern int video_pixel_width;
extern int video_pixel_height;

void video_init(uint32_t magic, uint32_t multiboot_address);
void video_clear(void);
void video_scroll(void);
void video_put_cell(int column, int row, char character, uint8_t color);
void video_put_pixel(int x, int y, uint32_t color, uint8_t alpha);
void video_capture_region(int x, int y, int width, int height, uint8_t *buffer);
void video_restore_region(int x, int y, int width, int height, const uint8_t *buffer);
int video_begin_frame(void);
int video_begin_region_frame(int x, int y, int width, int height);
void video_present(void);
void video_present_region(void);

#endif
