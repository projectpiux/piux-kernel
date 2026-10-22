#include "video.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_FRAMEBUFFER  (1u << 12)
#define VGA_MEMORY 0xB8000
#define VIDEO_BACKBUFFER_WIDTH 1920
#define VIDEO_BACKBUFFER_HEIGHT 1080

int video_framebuffer_ready;
int video_columns = 80;
int video_rows = 25;
int video_pixel_width;
int video_pixel_height;
static uint8_t *framebuffer;
static uint32_t framebuffer_pitch;
static uint8_t framebuffer_bpp;
static uint8_t framebuffer_bytes_per_pixel;
static uint8_t red_position;
static uint8_t red_mask_size;
static uint8_t green_position;
static uint8_t green_mask_size;
static uint8_t blue_position;
static uint8_t blue_mask_size;
static uint32_t framebuffer_width;
static uint32_t framebuffer_height;
static uint32_t video_origin_x;
static uint32_t video_origin_y;
static uint8_t video_backbuffer[VIDEO_BACKBUFFER_WIDTH * VIDEO_BACKBUFFER_HEIGHT * 4];
static uint32_t video_backbuffer_pitch;
static int video_frame_active;
static uint32_t video_frame_x;
static uint32_t video_frame_y;
static uint32_t video_frame_width;
static uint32_t video_frame_height;

static uint32_t framebuffer_color(uint32_t rgb) {
    if (framebuffer_bpp == 32) return rgb;
    return (((rgb >> 16) * ((1u << red_mask_size) - 1u) / 255u) << red_position) |
           (((rgb >> 8) * ((1u << green_mask_size) - 1u) / 255u) << green_position) |
           ((rgb * ((1u << blue_mask_size) - 1u) / 255u) << blue_position);
}

struct multiboot_info {
    uint32_t flags;
    uint32_t memory_lower;
    uint32_t memory_upper;
    uint32_t boot_device;
    uint32_t command_line;
    uint32_t modules_count;
    uint32_t modules_address;
    uint32_t symbols[4];
    uint32_t memory_map_length;
    uint32_t memory_map_address;
    uint32_t drives_length;
    uint32_t drives_address;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_segment;
    uint16_t vbe_interface_offset;
    uint16_t vbe_interface_length;
    uint64_t framebuffer_address;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t color_info[6];
} __attribute__((packed));

static uint8_t glyph_row(char character, int row) {
    static const char *letters[] = {
        "01110 10001 10001 11111 10001 10001 10001", "11110 10001 10001 11110 10001 10001 11110",
        "01111 10000 10000 10000 10000 10000 01111", "11110 10001 10001 10001 10001 10001 11110",
        "11111 10000 10000 11110 10000 10000 11111", "11111 10000 10000 11110 10000 10000 10000",
        "01111 10000 10000 10111 10001 10001 01111", "10001 10001 10001 11111 10001 10001 10001",
        "11111 00100 00100 00100 00100 00100 11111", "00111 00010 00010 00010 10010 10010 01100",
        "10001 10010 10100 11000 10100 10010 10001", "10000 10000 10000 10000 10000 10000 11111",
        "10001 11011 10101 10101 10001 10001 10001", "10001 11001 10101 10011 10001 10001 10001",
        "01110 10001 10001 10001 10001 10001 01110", "11110 10001 10001 11110 10000 10000 10000",
        "01110 10001 10001 10001 10101 10010 01101", "11110 10001 10001 11110 10100 10010 10001",
        "01111 10000 10000 01110 00001 00001 11110", "11111 00100 00100 00100 00100 00100 00100",
        "10001 10001 10001 10001 10001 10001 01110", "10001 10001 10001 10001 10001 01010 00100",
        "10001 10001 10001 10101 10101 11011 10001", "10001 10001 01010 00100 01010 10001 10001",
        "10001 10001 01010 00100 00100 00100 00100", "11111 00001 00010 00100 01000 10000 11111"
    };
    static const char *lowercase[] = {
        "00000 00000 01110 00001 01111 10001 01111", "10000 10000 11110 10001 10001 10001 11110",
        "00000 00000 01111 10000 10000 10000 01111", "00001 00001 01111 10001 10001 10001 01111",
        "00000 00000 01110 10001 11111 10000 01110", "00110 01001 01000 11100 01000 01000 01000",
        "00000 00000 01111 10001 01111 00001 01110", "10000 10000 10110 11001 10001 10001 10001",
        "00100 00000 01100 00100 00100 00100 01110", "00010 00000 00110 00010 00010 10010 01100",
        "10000 10000 10010 10100 11000 10100 10010", "01100 00100 00100 00100 00100 00100 01110",
        "00000 00000 11010 10101 10101 10101 10101", "00000 00000 10110 11001 10001 10001 10001",
        "00000 00000 01110 10001 10001 10001 01110", "00000 00000 11110 10001 11110 10000 10000",
        "00000 00000 01111 10001 01111 00001 00001", "00000 00000 10111 11000 10000 10000 10000",
        "00000 00000 01111 10000 01110 00001 11110", "01000 01000 11100 01000 01000 01001 00110",
        "00000 00000 10001 10001 10001 10011 01101", "00000 00000 10001 10001 10001 01010 00100",
        "00000 00000 10001 10101 10101 11011 10001", "00000 00000 10001 01010 00100 01010 10001",
        "00000 00000 10001 10001 01111 00001 01110", "00000 00000 11111 00010 00100 01000 11111"
    };
    static const char *digits[] = {
        "01110 10001 10011 10101 11001 10001 01110", "00100 01100 00100 00100 00100 00100 01110",
        "01110 10001 00001 00010 00100 01000 11111", "11110 00001 00001 01110 00001 00001 11110",
        "00010 00110 01010 10010 11111 00010 00010", "11111 10000 10000 11110 00001 00001 11110",
        "00110 01000 10000 11110 10001 10001 01110", "11111 00001 00010 00100 01000 01000 01000",
        "01110 10001 10001 01110 10001 10001 01110", "01110 10001 10001 01111 00001 00010 01100"
    };
    const char *pattern = 0;
    if (character >= 'A' && character <= 'Z') pattern = letters[character - 'A'];
    if (character >= 'a' && character <= 'z') pattern = lowercase[character - 'a'];
    if (character >= '0' && character <= '9') pattern = digits[character - '0'];
    if (character == '!') pattern = "00100 00100 00100 00100 00100 00000 00100";
    if (character == '.') pattern = "00000 00000 00000 00000 00000 00110 00110";
    if (character == ',') pattern = "00000 00000 00000 00000 00000 00110 00100";
    if (character == ':') pattern = "00000 00110 00110 00000 00110 00110 00000";
    if (character == '-') pattern = "00000 00000 00000 11111 00000 00000 00000";
    if (character == '_') pattern = "00000 00000 00000 00000 00000 00000 11111";
    if (character == '/') pattern = "00001 00010 00100 01000 10000 00000 00000";
    if (character == '>') pattern = "10000 01000 00100 00010 00100 01000 10000";
    if (character == '<') pattern = "00001 00010 00100 01000 00100 00010 00001";
    if (character == '+') pattern = "00000 00100 00100 11111 00100 00100 00000";
    if (character == '=') pattern = "00000 11111 00000 11111 00000 00000 00000";
    if (character == '?') pattern = "01110 10001 00001 00010 00100 00000 00100";
    if (character == '(') pattern = "00010 00100 01000 01000 01000 00100 00010";
    if (character == ')') pattern = "01000 00100 00010 00010 00010 00100 01000";
    if (character == '*') pattern = "00000 10101 01110 11111 01110 10101 00000";
    if (character == '|') pattern = "00100 00100 00100 00100 00100 00100 00100";
    if (character == '[') pattern = "01110 01000 01000 01000 01000 01000 01110";
    if (character == ']') pattern = "01110 00010 00010 00010 00010 00010 01110";
    if (character == '#') pattern = "01010 11111 01010 01010 11111 01010 00000";
    if (character == '%') pattern = "11001 11010 00010 00100 01000 01011 10011";
    if (character == '"') pattern = "01010 01010 00000 00000 00000 00000 00000";
    if (character == '\'') pattern = "00100 00100 00000 00000 00000 00000 00000";
    if (character == '$') pattern = "00100 01111 10100 01110 00101 11110 00100";
    if (character == '&') pattern = "01100 10010 10100 01000 10101 10010 01101";
    if (character == ';') pattern = "00110 00110 00000 00110 00100 01000 00000";
    if (character == '@') pattern = "01110 10001 10111 10101 10111 10000 01110";
    if (character == '\\') pattern = "10000 01000 00100 00010 00001 00000 00000";
    if (character == '^') pattern = "00100 01010 10001 00000 00000 00000 00000";
    if (character == '`') pattern = "00100 00010 00000 00000 00000 00000 00000";
    if (character == '{') pattern = "00010 00100 00100 01000 00100 00100 00010";
    if (character == '}') pattern = "01000 00100 00100 00010 00100 00100 01000";
    if (character == '~') pattern = "00000 01001 10110 00000 00000 00000 00000";
    if (!pattern || row < 0 || row >= 7) return 0;
    return (uint8_t)(pattern[row * 6] - '0') << 4 |
           (uint8_t)(pattern[row * 6 + 1] - '0') << 3 |
           (uint8_t)(pattern[row * 6 + 2] - '0') << 2 |
           (uint8_t)(pattern[row * 6 + 3] - '0') << 1 |
           (uint8_t)(pattern[row * 6 + 4] - '0');
}

static uint32_t color_rgb(uint8_t color, int foreground) {
    static const uint32_t palette[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    return palette[foreground ? (color & 0x0F) : ((color >> 4) & 0x0F)];
}

void video_init(uint32_t magic, uint32_t multiboot_address) {
    video_framebuffer_ready = 0;
    video_pixel_width = 0;
    video_pixel_height = 0;
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) return;
    struct multiboot_info *info = (struct multiboot_info *)multiboot_address;
    if (!(info->flags & MULTIBOOT_INFO_FRAMEBUFFER) ||
        info->framebuffer_width == 0 || info->framebuffer_height == 0 ||
        (info->framebuffer_bpp != 16 && info->framebuffer_bpp != 24 && info->framebuffer_bpp != 32) ||
        info->framebuffer_type != 1 || info->framebuffer_address == 0 ||
        info->framebuffer_pitch < info->framebuffer_width * (info->framebuffer_bpp / 8)) return;
    framebuffer = (uint8_t *)(uint32_t)info->framebuffer_address;
    framebuffer_pitch = info->framebuffer_pitch;
    framebuffer_bpp = info->framebuffer_bpp;
    framebuffer_bytes_per_pixel = framebuffer_bpp / 8;
    red_position = info->color_info[0];
    red_mask_size = info->color_info[1];
    green_position = info->color_info[2];
    green_mask_size = info->color_info[3];
    blue_position = info->color_info[4];
    blue_mask_size = info->color_info[5];
    framebuffer_width = info->framebuffer_width;
    framebuffer_height = info->framebuffer_height;
    video_pixel_width = (int)framebuffer_width;
    video_pixel_height = (int)framebuffer_height;
    video_origin_x = 0;
    video_origin_y = 0;
    video_columns = framebuffer_width / VIDEO_CELL_WIDTH;
    video_rows = framebuffer_height / VIDEO_CELL_HEIGHT;
    video_framebuffer_ready = 1;
    video_clear();
}

void video_clear(void) {
    if (!video_framebuffer_ready) {
        uint16_t *text = (uint16_t *)VGA_MEMORY;
        for (int i = 0; i < 80 * 25; i++) text[i] = (0x07 << 8) | ' ';
        return;
    }
    for (uint32_t y = 0; y < framebuffer_height; y++)
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            uint8_t *pixel = framebuffer + y * framebuffer_pitch + x * framebuffer_bytes_per_pixel;
            for (uint8_t byte = 0; byte < framebuffer_bytes_per_pixel; byte++) pixel[byte] = 0;
        }
}

int video_begin_frame(void) {
    if (!video_framebuffer_ready || framebuffer_width > VIDEO_BACKBUFFER_WIDTH ||
        framebuffer_height > VIDEO_BACKBUFFER_HEIGHT) return 0;
    video_backbuffer_pitch = framebuffer_width * framebuffer_bytes_per_pixel;
    for (uint32_t y = 0; y < framebuffer_height; y++)
        for (uint32_t byte = 0; byte < video_backbuffer_pitch; byte++)
            video_backbuffer[y * video_backbuffer_pitch + byte] = framebuffer[y * framebuffer_pitch + byte];
    video_frame_x = 0;
    video_frame_y = 0;
    video_frame_width = framebuffer_width;
    video_frame_height = framebuffer_height;
    video_frame_active = 1;
    return 1;
}

int video_begin_region_frame(int x, int y, int width, int height) {
    int right = x + width;
    int bottom = y + height;
    if (!video_framebuffer_ready || framebuffer_width > VIDEO_BACKBUFFER_WIDTH ||
        framebuffer_height > VIDEO_BACKBUFFER_HEIGHT || width <= 0 || height <= 0) return 0;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > (int)framebuffer_width) right = framebuffer_width;
    if (bottom > (int)framebuffer_height) bottom = framebuffer_height;
    if (right <= x || bottom <= y) return 0;
    video_backbuffer_pitch = framebuffer_width * framebuffer_bytes_per_pixel;
    video_frame_x = (uint32_t)x;
    video_frame_y = (uint32_t)y;
    video_frame_width = (uint32_t)(right - x);
    video_frame_height = (uint32_t)(bottom - y);
    for (uint32_t row = video_frame_y; row < video_frame_y + video_frame_height; row++) {
        uint32_t offset = video_frame_x * framebuffer_bytes_per_pixel;
        uint32_t bytes = video_frame_width * framebuffer_bytes_per_pixel;
        for (uint32_t byte = 0; byte < bytes; byte++)
            video_backbuffer[row * video_backbuffer_pitch + offset + byte] =
                framebuffer[row * framebuffer_pitch + offset + byte];
    }
    video_frame_active = 1;
    return 1;
}

void video_present(void) {
    if (!video_frame_active) return;
    for (uint32_t y = 0; y < framebuffer_height; y++)
        for (uint32_t byte = 0; byte < video_backbuffer_pitch; byte++)
            framebuffer[y * framebuffer_pitch + byte] = video_backbuffer[y * video_backbuffer_pitch + byte];
    video_frame_active = 0;
}

void video_present_region(void) {
    if (!video_frame_active) return;
    for (uint32_t row = video_frame_y; row < video_frame_y + video_frame_height; row++) {
        uint32_t offset = video_frame_x * framebuffer_bytes_per_pixel;
        uint32_t bytes = video_frame_width * framebuffer_bytes_per_pixel;
        for (uint32_t byte = 0; byte < bytes; byte++)
            framebuffer[row * framebuffer_pitch + offset + byte] =
                video_backbuffer[row * video_backbuffer_pitch + offset + byte];
    }
    video_frame_active = 0;
}

void video_put_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    uint8_t *target;
    uint32_t target_pitch;
    uint8_t *address;
    uint32_t value;
    if (!video_framebuffer_ready || x < 0 || y < 0 ||
        (uint32_t)x >= framebuffer_width || (uint32_t)y >= framebuffer_height || alpha == 0) return;
    target = video_frame_active ? video_backbuffer : framebuffer;
    target_pitch = video_frame_active ? video_backbuffer_pitch : framebuffer_pitch;
    address = target + y * target_pitch + x * framebuffer_bytes_per_pixel;
    value = framebuffer_color(color);
    if (alpha < 255) return;
    for (uint8_t byte = 0; byte < framebuffer_bytes_per_pixel; byte++) address[byte] = (uint8_t)(value >> (byte * 8));
}

void video_capture_region(int x, int y, int width, int height, uint8_t *buffer) {
    uint8_t *source_buffer;
    uint32_t source_pitch;
    if (!video_framebuffer_ready || buffer == 0) return;
    source_buffer = video_frame_active ? video_backbuffer : framebuffer;
    source_pitch = video_frame_active ? video_backbuffer_pitch : framebuffer_pitch;
    for (int row = 0; row < height; row++) for (int column = 0; column < width; column++) {
        uint8_t *destination = buffer + (row * width + column) * 4;
        int source_x = x + column;
        int source_y = y + row;
        for (uint8_t byte = 0; byte < 4; byte++) destination[byte] = 0;
        if (source_x < 0 || source_y < 0 || (uint32_t)source_x >= framebuffer_width ||
            (uint32_t)source_y >= framebuffer_height) continue;
        {
            const uint8_t *source = source_buffer + source_y * source_pitch + source_x * framebuffer_bytes_per_pixel;
            for (uint8_t byte = 0; byte < framebuffer_bytes_per_pixel; byte++) destination[byte] = source[byte];
        }
    }
}

void video_restore_region(int x, int y, int width, int height, const uint8_t *buffer) {
    uint8_t *destination_buffer;
    uint32_t destination_pitch;
    if (!video_framebuffer_ready || buffer == 0) return;
    destination_buffer = video_frame_active ? video_backbuffer : framebuffer;
    destination_pitch = video_frame_active ? video_backbuffer_pitch : framebuffer_pitch;
    for (int row = 0; row < height; row++) for (int column = 0; column < width; column++) {
        int destination_x = x + column;
        int destination_y = y + row;
        if (destination_x < 0 || destination_y < 0 || (uint32_t)destination_x >= framebuffer_width ||
            (uint32_t)destination_y >= framebuffer_height) continue;
        {
            uint8_t *destination = destination_buffer + destination_y * destination_pitch + destination_x * framebuffer_bytes_per_pixel;
            const uint8_t *source = buffer + (row * width + column) * 4;
            for (uint8_t byte = 0; byte < framebuffer_bytes_per_pixel; byte++) destination[byte] = source[byte];
        }
    }
}

void video_scroll(void) {
    if (!video_framebuffer_ready) return;
    int width = video_columns * VIDEO_CELL_WIDTH;
    int height = video_rows * VIDEO_CELL_HEIGHT;
    for (int y = VIDEO_CELL_HEIGHT; y < height; y++)
        for (int x = 0; x < width; x++)
            for (uint8_t byte = 0; byte < framebuffer_bytes_per_pixel; byte++)
                framebuffer[(video_origin_y + y - VIDEO_CELL_HEIGHT) * framebuffer_pitch +
                            (video_origin_x + x) * framebuffer_bytes_per_pixel + byte] =
                    framebuffer[(video_origin_y + y) * framebuffer_pitch +
                                (video_origin_x + x) * framebuffer_bytes_per_pixel + byte];
    for (int column = 0; column < video_columns; column++) video_put_cell(column, video_rows - 1, ' ', 0x07);
}

void video_put_cell(int column, int row, char character, uint8_t color) {
    if (!video_framebuffer_ready) {
        if (column < 0 || column >= 80 || row < 0 || row >= 25) return;
        ((uint16_t *)VGA_MEMORY)[row * 80 + column] = (uint16_t)(color << 8) | (uint8_t)character;
        return;
    }
    if (column < 0 || column >= video_columns || row < 0 || row >= video_rows) return;
    uint32_t foreground = color_rgb(color, 1);
    uint32_t background = color_rgb(color, 0);
    uint32_t pixel_value = 0;
    uint32_t foreground_rgb = foreground;
    uint32_t background_rgb = background;
    uint32_t background_value;
    if (framebuffer_bpp == 32) {
        pixel_value = foreground_rgb;
        background_value = background_rgb;
    } else {
        uint32_t red = ((foreground_rgb >> 16) * ((1u << red_mask_size) - 1u) / 255u) << red_position;
        uint32_t green = ((foreground_rgb >> 8) * ((1u << green_mask_size) - 1u) / 255u) << green_position;
        uint32_t blue = (foreground_rgb * ((1u << blue_mask_size) - 1u) / 255u) << blue_position;
        pixel_value = red | green | blue;
        red = ((background_rgb >> 16) * ((1u << red_mask_size) - 1u) / 255u) << red_position;
        green = ((background_rgb >> 8) * ((1u << green_mask_size) - 1u) / 255u) << green_position;
        blue = (background_rgb * ((1u << blue_mask_size) - 1u) / 255u) << blue_position;
        background_value = red | green | blue;
    }
    int glyph_top = (VIDEO_CELL_HEIGHT - 7) / 2;
    for (int y = 0; y < VIDEO_CELL_HEIGHT; y++) {
        uint8_t bits = glyph_row(character, y - glyph_top);
        for (int x = 0; x < VIDEO_CELL_WIDTH; x++) {
            int pixel = (y >= glyph_top && y < glyph_top + 7 && x >= 1 && x <= 5 &&
                         (bits & (1 << (5 - x)))) != 0;
            uint32_t value = pixel ? pixel_value : background_value;
            uint8_t *target = video_frame_active ? video_backbuffer : framebuffer;
            uint32_t target_pitch = video_frame_active ? video_backbuffer_pitch : framebuffer_pitch;
            uint8_t *address = target + (video_origin_y + row * VIDEO_CELL_HEIGHT + y) * target_pitch +
                               (video_origin_x + column * VIDEO_CELL_WIDTH + x) * framebuffer_bytes_per_pixel;
            for (uint8_t byte = 0; byte < framebuffer_bytes_per_pixel; byte++) address[byte] = (uint8_t)(value >> (byte * 8));
        }
    }
}
