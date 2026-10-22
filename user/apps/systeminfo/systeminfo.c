#include <stdint.h>
#include "systeminfo.h"
#include "../../../kernel/ext2.h"
#include "../../../kernel/memory.h"
#include "../../../kernel/process.h"
#include "../../../kernel/system.h"
#include "../../../kernel/video.h"

extern const char _os_infos_start[];
extern const char _os_infos_end[];
extern ext2_filesystem_t fs;
extern int ext2_is_mounted(void);

static char os_name[32];
static char os_version[64];

static void copy_value(char *destination, int capacity, const char *start, const char *end) {
    int length = (int)(end - start);
    if (length >= capacity) length = capacity - 1;
    for (int index = 0; index < length; index++) destination[index] = start[index];
    destination[length] = '\0';
}

static const char *find_label(const char *line, const char *end, const char *label) {
    int label_length = 0;
    while (label[label_length]) label_length++;
    for (const char *cursor = line; cursor + label_length <= end; cursor++) {
        int index = 0;
        while (index < label_length && cursor[index] == label[index]) index++;
        if (index == label_length) return cursor + label_length;
    }
    return 0;
}

static void load_metadata(void) {
    const char *line = _os_infos_start;
    while (line < _os_infos_end) {
        const char *end = line;
        while (end < _os_infos_end && *end != '\n' && *end != '\r') end++;
        const char *value = find_label(line, end, "Name: ");
        if (value) copy_value(os_name, sizeof(os_name), value, end);
        value = find_label(line, end, "Version: ");
        if (value) copy_value(os_version, sizeof(os_version), value, end);
        line = end;
        while (line < _os_infos_end && (*line == '\n' || *line == '\r')) line++;
    }
}

static void draw_text(const app_context_t *context, int row, const char *text) {
    if (row < 0 || row >= context->content.height) return;
    for (int column = 0; text[column] && column < context->content.width; column++)
        video_put_cell(context->content.x + column, context->content.y + row, text[column], 0x07);
}

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
    load_metadata();
    return 0;
}

static void systeminfo_render(app_context_t *context) {
    char line[96];
    const process_t *process = process_current();
    draw_text(context, 0, "Piux system information");
    line[0] = '\0';
    append_text(line, sizeof(line), "OS: ");
    append_text(line, sizeof(line), os_name[0] ? os_name : "unavailable");
    draw_text(context, 1, line);
    line[0] = '\0';
    append_text(line, sizeof(line), "Version: ");
    append_text(line, sizeof(line), os_version[0] ? os_version : "unavailable");
    draw_text(context, 2, line);
    line[0] = '\0';
    append_text(line, sizeof(line), "Architecture: x86, 32-bit");
    draw_text(context, 3, line);
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
    draw_text(context, 4, line);
    line[0] = '\0';
    append_text(line, sizeof(line), "Memory total: ");
    if (system_memory_total_kb) append_number(line, sizeof(line), (int)system_memory_total_kb);
    else append_text(line, sizeof(line), "unavailable");
    append_text(line, sizeof(line), " KB");
    draw_text(context, 5, line);
    line[0] = '\0';
    append_text(line, sizeof(line), "Kernel footprint estimate: ");
    if (system_memory_used_kb) append_number(line, sizeof(line), (int)system_memory_used_kb);
    else append_text(line, sizeof(line), "unavailable");
    append_text(line, sizeof(line), " KB");
    draw_text(context, 6, line);
    line[0] = '\0';
    append_text(line, sizeof(line), "Memory free estimate: ");
    if (system_memory_total_kb) append_number(line, sizeof(line), (int)system_memory_free_kb);
    else append_text(line, sizeof(line), "unavailable");
    append_text(line, sizeof(line), " KB");
    draw_text(context, 7, line);
    line[0] = '\0';
    append_text(line, sizeof(line), "Current process: ");
    if (process) append_text(line, sizeof(line), process->name);
    else append_text(line, sizeof(line), "unavailable");
    append_text(line, sizeof(line), " (PID ");
    append_number(line, sizeof(line), process ? process->pid : 0);
    append_text(line, sizeof(line), ")");
    draw_text(context, 8, line);
    if (ext2_is_mounted()) {
        line[0] = '\0';
        append_text(line, sizeof(line), "ext2: ");
        append_number(line, sizeof(line), (int)fs.superblock.blocks_count);
        append_text(line, sizeof(line), " blocks, ");
        append_number(line, sizeof(line), (int)fs.block_size);
        append_text(line, sizeof(line), " bytes/block");
        draw_text(context, 9, line);
        line[0] = '\0';
        append_text(line, sizeof(line), "ext2 free blocks: ");
        append_number(line, sizeof(line), (int)fs.superblock.free_blocks_count);
        draw_text(context, 10, line);
    } else {
        draw_text(context, 9, "ext2: not mounted");
    }
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
