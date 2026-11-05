#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

void video_fill_rect(int x, int y, int width, int height, uint32_t color, uint8_t alpha);
void video_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height);
void video_blit(int x, int y, int width, int height, const uint32_t *argb, int stride);

struct video_gpu_info {
    uint8_t bus, slot, func;
    uint8_t class_code, subclass;
    uint16_t vendor, device;
    uint32_t bar0;
};

struct video_surface_info {
    uint8_t *base;
    uint32_t pitch, width, height;
    uint8_t bpp;
    uint8_t red_position, red_size;
    uint8_t green_position, green_size;
    uint8_t blue_position, blue_size;
};

struct video_driver {
    const char *name;
    int (*probe)(const struct video_gpu_info *gpu);
    int (*set_mode)(uint32_t width, uint32_t height, uint8_t bpp, struct video_surface_info *out);
    int (*fill_rect)(int x, int y, int width, int height, uint32_t pixel);
    int (*copy_rect)(int src_x, int src_y, int dst_x, int dst_y, int width, int height);
    void (*sync)(void);
};

int video_register_driver(const struct video_driver *driver);
const char *video_driver_name(void);
const struct video_gpu_info *video_gpu_info(void);
int video_set_mode(uint32_t width, uint32_t height, uint8_t bpp);

#ifdef __cplusplus
}
#endif

#endif