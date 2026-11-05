#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "ext2.h"

static const char scancode_ascii_us[] = {
    0,      27,     '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',    '0',    '-',    '=',    '\b',
    '\t',   'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',    'o',    'p',    '[',    ']',    '\n',
    0,      'a',    's',    'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',    '\'',   '`',
    0,      '\\',   'z',    'x',    'c',    'v',    'b',    'n',    'm',    ',',    '.',    '/',    0,
    '*',    0,      ' '
};

static const char scancode_ascii_shift_us[] = {
    0,      27,     '!',    '@',    '#',    '$',    '%',    '^',    '&',    '*',    '(',    ')',    '_',    '+',    '\b',
    '\t',   'Q',    'W',    'E',    'R',    'T',    'Y',    'U',    'I',    'O',    'P',    '{',    '}',    '\n',
    0,      'A',    'S',    'D',    'F',    'G',    'H',    'J',    'K',    'L',    ':',    '"',    '~',
    0,      '|',    'Z',    'X',    'C',    'V',    'B',    'N',    'M',    '<',    '>',    '?',    0,
    '*',    0,      ' '
};

static const char scancode_ascii_it[] = {
    0,      27,     '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',    '0',    '\'',   '+',    '\b',
    '\t',   'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',    'o',    'p',    '\x5E', '+',    '\n',
    0,      'a',    's',    'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',    '\'',   '\\',
    0,      '<',    'z',    'x',    'c',    'v',    'b',    'n',    'm',    ',',    '.',    '-',    0,
    '*',    0,      ' '
};

static const char scancode_ascii_shift_it[] = {
    0,      27,     '!',    '"',   '#',    '$',    '%',    '&',    '/',    '(',    ')',    '=',    '?',    '^',    '\b',
    '\t',   'Q',    'W',    'E',    'R',    'T',    'Y',    'U',    'I',    'O',    'P',    '*',    '*',    '\n',
    0,      'A',    'S',    'D',    'F',    'G',    'H',    'J',    'K',    'L',    ':',    '"',    '|',
    0,      '>',    'Z',    'X',    'C',    'V',    'B',    'N',    'M',    ';',    ':',    '_',    0,
    '*',    0,      ' '
};

static keyboard_layout_t current_layout = KEYBOARD_LAYOUT_US;
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static uint8_t last_scancode = 0;
static int extended_scancode = 0;

static const char *keyboard_lookup_table(int shift) {
    if (current_layout == KEYBOARD_LAYOUT_IT) {
        return shift ? scancode_ascii_shift_it : scancode_ascii_it;
    }
    return shift ? scancode_ascii_shift_us : scancode_ascii_us;
}

void keyboard_set_layout(keyboard_layout_t layout) {
    if (layout == KEYBOARD_LAYOUT_US || layout == KEYBOARD_LAYOUT_IT) {
        current_layout = layout;
    }
}

keyboard_layout_t keyboard_get_layout(void) {
    return current_layout;
}

static int keyboard_parse_layout_value(const char *text) {
    const char *cursor = text;
    if (text == 0) return -1;
    while (*cursor && *cursor != '=') cursor++;
    if (*cursor != '=') return -1;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if ((cursor[0] == 'i' || cursor[0] == 'I') &&
        (cursor[1] == 't' || cursor[1] == 'T') &&
        (cursor[2] == '\0' || cursor[2] == '\n' || cursor[2] == '\r' || cursor[2] == ' ' || cursor[2] == '\t')) {
        return KEYBOARD_LAYOUT_IT;
    }
    if ((cursor[0] == 'u' || cursor[0] == 'U') &&
        (cursor[1] == 's' || cursor[1] == 'S') &&
        (cursor[2] == '\0' || cursor[2] == '\n' || cursor[2] == '\r' || cursor[2] == ' ' || cursor[2] == '\t')) {
        return KEYBOARD_LAYOUT_US;
    }
    return -1;
}

void keyboard_load_layout_from_config(void) {
    char buffer[32];
    int length;
    if (!ext2_is_mounted()) return;
    if (ext2_find_inode_by_path("/.config/keyboard.conf") < 0) return;
    length = ext2_read_file_by_path("/.config/keyboard.conf", buffer, sizeof(buffer) - 1);
    if (length <= 0) return;
    buffer[length] = '\0';
    {
        int parsed = keyboard_parse_layout_value(buffer);
        if (parsed >= 0) keyboard_set_layout((keyboard_layout_t)parsed);
    }
}

int keyboard_try_read_char(void) {
    if (!(inb(0x64) & 1) || (inb(0x64) & 0x20)) return -1;
    {
        uint8_t scancode = inb(0x60);
        last_scancode = scancode;

        if (scancode == 0xE0) {
            extended_scancode = 1;
            return -1;
        }

        if (extended_scancode) {
            extended_scancode = 0;
            if (scancode & 0x80) return -1;
            if (scancode == 0x48) return KEY_ARROW_UP;
            if (scancode == 0x50) return KEY_ARROW_DOWN;
            if (scancode == 0x4B) return KEY_ARROW_LEFT;
            if (scancode == 0x4D) return KEY_ARROW_RIGHT;
            if (scancode == 0x53) return KEY_DELETE;
            return -1;
        }

        if (scancode & 0x80) {
            uint8_t released = scancode & ~0x80;

            if (released == 0x2A || released == 0x36) {
                shift_pressed = 0;
                return -1;
            }

            if (released == 0x1D) {
                ctrl_pressed = 0;
                return -1;
            }

            if (released == 0x38) {
                alt_pressed = 0;
                return -1;
            }

            return -1;
        }

        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            return -1;
        }

        if (scancode == 0x1D) {
            ctrl_pressed = 1;
            return -1;
        }

        if (scancode == 0x38) {
            alt_pressed = 1;
            return -1;
        }

        if (ctrl_pressed) {
            if (scancode == 0x10) return 17;
            if (scancode == 0x2E) return 3;
            if (scancode == 0x1F) return 19;
            if (scancode == 0x2D) return 24;
            if (scancode == 0x1E) return 1;
            if (scancode == 0x30) return 26;
            if (scancode == 0x17) return 9;
            if (scancode == 0x23) return 16;
        }

        if (scancode < 60) {
            const char *table = keyboard_lookup_table(shift_pressed);
            char ascii = table[scancode];

            if (ascii != 0) {
                return ascii;
            }
        }
    }
    return -1;
}

extern void net_poll(void);

int keyboard_read_char(void) {
    int character;
    while ((character = keyboard_try_read_char()) < 0) {
        net_poll(); 
    }
    return character;
}

int keyboard_is_shift_pressed(void) {
    return shift_pressed;
}

int keyboard_is_ctrl_pressed(void) {
    return ctrl_pressed;
}

int keyboard_is_alt_pressed(void) {
    return alt_pressed;
}

uint8_t keyboard_get_last_scancode(void) {
    return last_scancode;
}
