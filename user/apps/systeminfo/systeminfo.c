#include <stdint.h>
#include "systeminfo.h"
#include "../../lib/api.h"

#define PIXEL_CHAR_ADVANCE (PIXFONT_WIDTH + PIXFONT_SCALE)
#define PIXEL_LINE_HEIGHT  (PIXFONT_HEIGHT + 2)

static syscall_system_info_t system_info;

static void put_number(int value, char *buffer, int capacity) {
    char reversed[16];
    int length = 0;
    int digits = 0;
    unsigned int number = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    if (number == 0) reversed[digits++] = '0';
    else {
        while (number > 0 && digits < (int)sizeof(reversed)) {
            reversed[digits++] = (char)('0' + number % 10);
            number /= 10;
        }
    }
    if (value < 0 && length < capacity - 1) buffer[length++] = '-';
    while (digits > 0 && length < capacity - 1) buffer[length++] = reversed[--digits];
    buffer[length] = '\0';
}

static void append_text(char *destination, int capacity, const char *text) {
    int length = 0;
    while (destination[length] && length < capacity - 1) length++;
    for (int index = 0; text[index] && length < capacity - 1; index++)
        destination[length++] = text[index];
    destination[length] = '\0';
}

static void append_number(char *destination, int capacity, int value) {
    char number[16];
    put_number(value, number, sizeof(number));
    append_text(destination, capacity, number);
}

static int systeminfo_initialize(app_context_t *context) {
    (void)context;
    return user_get_system_info(&system_info);
}

static int next_row_y(const app_context_t *context, int *row, int row_height, int max_rows) {
    if (*row >= max_rows) return -1;
    return context->content.y + (*row)++ * row_height;
}

static void systeminfo_render(app_context_t *context) {
    char line[96];
    int row_height = PIXEL_LINE_HEIGHT + 2;
    int max_rows = context->content.height / row_height;
    int text_x = context->content.x + 8;
    int row;
    int row_counter = 0;
    pixel_fill_rect(context->content.x, context->content.y,
                    context->content.width, context->content.height, 0x000000u);
#define ROW() next_row_y(context, &row_counter, row_height, max_rows)
    row = ROW();
    if (row >= 0) pixel_draw_text(text_x, row, "Piux system information", 0x7FBAFFu);
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "OS: ");
        append_text(line, sizeof(line), system_info.os_name[0] ? system_info.os_name : "unavailable");
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "Version: ");
        append_text(line, sizeof(line), system_info.os_version[0] ? system_info.os_version : "unavailable");
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) pixel_draw_text(text_x, row, "Architecture: x86, 32-bit", 0xE0E0E0u);
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "Display: ");
        if (video_framebuffer_ready) {
            append_number(line, sizeof(line), video_pixel_width);
            append_text(line, sizeof(line), " x ");
            append_number(line, sizeof(line), video_pixel_height);
            append_text(line, sizeof(line), " pixels");
        } else {
            append_number(line, sizeof(line), video_columns);
            append_text(line, sizeof(line), " x ");
            append_number(line, sizeof(line), video_rows);
            append_text(line, sizeof(line), " text cells");
        }
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "Memory total: ");
        if (system_info.memory_total_kb) append_number(line, sizeof(line), (int)system_info.memory_total_kb);
        else append_text(line, sizeof(line), "unavailable");
        append_text(line, sizeof(line), " KB");
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "Kernel footprint estimate: ");
        if (system_info.memory_used_kb) append_number(line, sizeof(line), (int)system_info.memory_used_kb);
        else append_text(line, sizeof(line), "unavailable");
        append_text(line, sizeof(line), " KB");
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "Memory free estimate: ");
        if (system_info.memory_total_kb) append_number(line, sizeof(line), (int)system_info.memory_free_kb);
        else append_text(line, sizeof(line), "unavailable");
        append_text(line, sizeof(line), " KB");
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) {
        line[0] = '\0';
        append_text(line, sizeof(line), "Current process: ");
        if (system_info.process_name[0]) append_text(line, sizeof(line), system_info.process_name);
        else append_text(line, sizeof(line), "unavailable");
        append_text(line, sizeof(line), " (PID ");
        append_number(line, sizeof(line), system_info.pid);
        append_text(line, sizeof(line), ")");
        pixel_draw_text(text_x, row, line, 0xE0E0E0u);
    }
    row = ROW();
    if (row >= 0) {
        if (system_info.filesystem_mounted) {
            line[0] = '\0';
            append_text(line, sizeof(line), "ext2: ");
            append_number(line, sizeof(line), (int)system_info.filesystem_blocks);
            append_text(line, sizeof(line), " blocks, ");
            append_number(line, sizeof(line), (int)system_info.filesystem_block_size);
            append_text(line, sizeof(line), " bytes/block");
            pixel_draw_text(text_x, row, line, 0xE0E0E0u);
        } else {
            pixel_draw_text(text_x, row, "ext2: not mounted", 0x808080u);
        }
    }
    if (system_info.filesystem_mounted) {
        row = ROW();
        if (row >= 0) {
            line[0] = '\0';
            append_text(line, sizeof(line), "ext2 free blocks: ");
            append_number(line, sizeof(line), (int)system_info.filesystem_free_blocks);
            pixel_draw_text(text_x, row, line, 0xE0E0E0u);
        }
    }
#undef ROW
}

static void systeminfo_handle_event(app_context_t *context, const app_event_t *event) {
    (void)context;
    (void)event;
}

static void systeminfo_shutdown(app_context_t *context) {
    (void)context;
}

const app_definition_t systeminfo_application = {
    "systeminfo",
    "System Info",
    systeminfo_initialize,
    systeminfo_render,
    systeminfo_handle_event,
    systeminfo_shutdown
};
