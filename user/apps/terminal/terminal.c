#include <stdint.h>
#include "terminal.h"
#include "../../desktop/desktop.h"
#include "../../../kernel/auth.h"
#include "../../../kernel/ext2.h"
#include "../../../kernel/keyboard.h"
#include "../../../kernel/shell.h"
#include "../../../kernel/video.h"

#define TERMINAL_MAX_LINES 48
#define TERMINAL_LINE_LENGTH 96
#define TERMINAL_MAX_INPUT 80

typedef struct {
    char lines[TERMINAL_MAX_LINES][TERMINAL_LINE_LENGTH];
    int line_count;
    char input[TERMINAL_MAX_INPUT];
    int input_length;
    int line_width;
} terminal_state_t;

extern ext2_filesystem_t fs;
static terminal_state_t terminal;
static terminal_state_t *output_terminal;

static int text_length(const char *text) {
    int length = 0;
    while (text[length]) length++;
    return length;
}

static void copy_text(char *destination, const char *source, int capacity) {
    int index = 0;
    while (source[index] && index < capacity - 1) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void terminal_new_line(terminal_state_t *state) {
    if (state->line_count == TERMINAL_MAX_LINES) {
        for (int row = 1; row < TERMINAL_MAX_LINES; row++)
            copy_text(state->lines[row - 1], state->lines[row], TERMINAL_LINE_LENGTH);
        state->line_count--;
    }
    state->lines[state->line_count][0] = '\0';
    state->line_count++;
}

static void terminal_putc(char character) {
    terminal_state_t *state = output_terminal;
    int row;
    int length;
    if (!state) return;
    if (character == '\n') {
        terminal_new_line(state);
        return;
    }
    if (character == '\r') return;
    if (state->line_count == 0) terminal_new_line(state);
    row = state->line_count - 1;
    length = text_length(state->lines[row]);
    if (character == '\b') {
        if (length == 0 && state->line_count > 1) {
            state->lines[row][0] = '\0';
            state->line_count--;
            row--;
            length = text_length(state->lines[row]);
        }
        if (length > 0) state->lines[row][length - 1] = '\0';
        return;
    }
    if (length >= state->line_width || length >= TERMINAL_LINE_LENGTH - 1) {
        terminal_new_line(state);
        row = state->line_count - 1;
        length = 0;
    }
    state->lines[row][length] = character;
    state->lines[row][length + 1] = '\0';
}

static void terminal_puts(const char *text) {
    for (int index = 0; text[index]; index++) terminal_putc(text[index]);
}

static void terminal_prompt(void) {
    const char *username = auth_current_username();
    terminal_puts(username[0] ? username : "piux");
    terminal_puts(" ");
    terminal_puts(ext2_is_mounted() ? fs.current_path : "~");
    terminal_puts("> ");
}

static int terminal_initialize(app_context_t *context) {
    terminal_state_t *state = &terminal;
    for (int row = 0; row < TERMINAL_MAX_LINES; row++) state->lines[row][0] = '\0';
    state->line_count = 0;
    state->input_length = 0;
    state->input[0] = '\0';
    state->line_width = context->content.width;
    if (state->line_width >= TERMINAL_LINE_LENGTH) state->line_width = TERMINAL_LINE_LENGTH - 1;
    if (state->line_width < 1) return -1;
    output_terminal = state;
    terminal_puts("Piux graphical terminal\n");
    terminal_prompt();
    context->state = state;
    return 0;
}

static void terminal_render(app_context_t *context) {
    terminal_state_t *state = context->state;
    int visible;
    int first;
    if (!state) return;
    state->line_width = context->content.width;
    if (state->line_width >= TERMINAL_LINE_LENGTH) state->line_width = TERMINAL_LINE_LENGTH - 1;
    visible = context->content.height;
    if (visible <= 0) return;
    first = state->line_count - visible;
    if (first < 0) first = 0;
    for (int row = 0; row < visible; row++) {
        int line = first + row;
        if (line >= state->line_count) break;
        for (int column = 0; state->lines[line][column] &&
             column < context->content.width; column++)
            video_put_cell(context->content.x + column, context->content.y + row,
                           state->lines[line][column], 0x07);
    }
}

static void terminal_clear_output(void) {
    for (int row = 0; row < TERMINAL_MAX_LINES; row++) terminal.lines[row][0] = '\0';
    terminal.line_count = 0;
}

static int input_is_clear(const char *input) {
    int start = 0;
    if (input[0] == 's' && input[1] == 'u' && input[2] == 'd' &&
        input[3] == 'o' && input[4] == ' ') {
        if (!auth_current_user_is_sudoer()) return 0;
        start = 5;
    }
    return input[start] == 'c' && input[start + 1] == 'l' &&
           input[start + 2] == 'e' && input[start + 3] == 'a' &&
           input[start + 4] == 'r' &&
           (input[start + 5] == '\0' || input[start + 5] == ' ');
}

static void terminal_submit(void) {
    char command[TERMINAL_MAX_INPUT];
    copy_text(command, terminal.input, sizeof(command));
    terminal_putc('\n');
    if (input_is_clear(command)) terminal_clear_output();
    terminal.input_length = 0;
    terminal.input[0] = '\0';
    shell_execute_command_line(command, terminal_puts, terminal_putc);
    terminal_prompt();
}

static void terminal_replace_input(const char *text) {
    while (terminal.input_length > 0) {
        terminal.input_length--;
        terminal_putc('\b');
    }
    copy_text(terminal.input, text, sizeof(terminal.input));
    terminal.input_length = text_length(terminal.input);
    for (int index = 0; index < terminal.input_length; index++)
        terminal_putc(terminal.input[index]);
}

static void terminal_handle_event(app_context_t *context, const app_event_t *event) {
    static char history[8][TERMINAL_MAX_INPUT];
    static int history_count;
    static int history_position;
    (void)context;
    if (event->type != APP_EVENT_KEY) return;
    if (event->key == '\n') {
        if (terminal.input_length > 0) {
            if (history_count < 8) copy_text(history[history_count++], terminal.input, TERMINAL_MAX_INPUT);
            else {
                for (int index = 1; index < 8; index++)
                    copy_text(history[index - 1], history[index], TERMINAL_MAX_INPUT);
                copy_text(history[7], terminal.input, TERMINAL_MAX_INPUT);
            }
        }
        history_position = history_count;
        terminal_submit();
    } else if (event->key == '\b' && terminal.input_length > 0) {
        terminal.input[--terminal.input_length] = '\0';
        terminal_putc('\b');
    } else if (event->key == KEY_ARROW_UP && history_count > 0) {
        if (history_position > 0) history_position--;
        terminal_replace_input(history[history_position]);
    } else if (event->key == KEY_ARROW_DOWN && history_position < history_count) {
        history_position++;
        terminal_replace_input(history_position == history_count ? "" : history[history_position]);
    } else if (event->key >= 32 && event->key < 127 &&
               terminal.input_length < TERMINAL_MAX_INPUT - 1) {
        terminal.input[terminal.input_length++] = (char)event->key;
        terminal.input[terminal.input_length] = '\0';
        terminal_putc((char)event->key);
    }
}

static void terminal_shutdown(app_context_t *context) {
    (void)context;
    output_terminal = 0;
}

const app_definition_t terminal_application = {
    "terminal",
    "Terminal",
    terminal_initialize,
    terminal_render,
    terminal_handle_event,
    terminal_shutdown
};
