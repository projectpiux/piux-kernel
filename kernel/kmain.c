// this is the main kernel of Piux

#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "ext2.h"
#include "vfs.h"
#include "syscall.h"
#include "process.h"
#include "memory.h"
#include "gdt.h"
#include "timer.h"
#include "auth.h"
#include "video.h"
#include "system.h"
#include "shell.h"
#include "rtl8139.h"
#include "dhcp.h"
#include "../bin/commands.h"
#include "../tui/installer/installer.h"
#include "../tui/wm/wm.h"
#include "../user/desktop/desktop.h"

#define VGA_MEMORY 0xB8000
#define VGA_WIDTH (video_framebuffer_ready ? video_columns : 80)
#define VGA_HEIGHT (video_framebuffer_ready ? video_rows : 25)

uint16_t *vga_buffer = (uint16_t *)VGA_MEMORY;
uint32_t cursor_x = 0;
uint32_t cursor_y = 0;

static void vga_update_cursor(void) {
    if (video_framebuffer_ready) return;
    uint16_t position = (uint16_t)(cursor_y * VGA_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, position & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, position >> 8);
}

extern ext2_filesystem_t fs;
extern int ramfs_init(void);


// made my nugget for antodev

// these values must be parsed out of the fadt table at boot, but now it is simply overkill
uint32_t PM1a_CNT_BLK = 0; 
uint32_t PM1b_CNT_BLK = 0;
// use S5 package (shutdown)
uint8_t SLP_TYPa = 0;
uint8_t SLP_TYPb = 0;

#define ACPI_SLP_EN (1 << 13) // Bit 13 is the Sleep Enable bit
// safe poweroff function
static void power_off(void) {
    // for the concept, if PM1a_CNT_BLK is 0, the table is not parsed, but lets not error yet since we use an hardcoded table
    // disable interrupts (WITHOUT HANGING!) so the cpu doesnt randomly context switch
    __asm__ __volatile__("cli"); // so GAS doesnt optimize the code
    // use __asm__ so C does not move this to another function (for optimization resions of qemu and bochs
    // we would normally execute this block of code if PM1a_CNT_BLK, but again...
    uint16_t pm1a_cmd = (SLP_TYPa << 10) | ACPI_SLP_EN;
    outw((uint16_t)PM1a_CNT_BLK, pm1a_cmd);
    uint16_t pm1b_cmd = (SLP_TYPb << 10) | ACPI_SLP_EN;
     outw((uint16_t)PM1b_CNT_BLK, pm1b_cmd);
  // now for old versions of qemu/bochs
    outw(0xB004, 0x2000);
    // new versions of qemu
    outw(0x604, 0x2000);
    // for new & old versions of oracle slop (sun microsystsms bstter >:])
    outw(0x4004, 0x3400);
    // for "generic" cloud hypervisors
    outb(0x600, 0x34);
    // simply panic if all the used methods here go wrong.
    for(;;) __asm__ __volatile__("cli; hlt");
}

static void reboot_system(void) {
    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void vga_scroll(void) {
    if (video_framebuffer_ready) {
        video_scroll();
        return;
    }
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_buffer[i] = (0x07 << 8) | ' ';
    }
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            vga_scroll();
            cursor_y = VGA_HEIGHT - 1;
        }
        vga_update_cursor();
        return;
    }
    
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
        if (video_framebuffer_ready) video_put_cell(cursor_x, cursor_y, ' ', 0x07);
        else vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (0x07 << 8) | ' ';
        vga_update_cursor();
        return;
    }
    
    if (c == '\r') {
        cursor_x = 0;
        vga_update_cursor();
        return;
    }
    
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            vga_scroll();
            cursor_y = VGA_HEIGHT - 1;
        }
    }
    
    if (video_framebuffer_ready) video_put_cell(cursor_x, cursor_y, c, 0x07);
    else vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (0x07 << 8) | (unsigned char)c;
    cursor_x++;
    vga_update_cursor();
}

void vga_puts(const char *str) {
    for (int i = 0; str[i]; i++) {
        vga_putc(str[i]);
    }
}

void vga_clear(void) {
    if (video_framebuffer_ready) {
        video_clear();
        cursor_x = 0;
        cursor_y = 0;
        return;
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (0x07 << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_backspace(void) {
    if (cursor_x > 0) {
        cursor_x--;
    } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    if (video_framebuffer_ready) video_put_cell(cursor_x, cursor_y, ' ', 0x07);
    else vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (0x07 << 8) | ' ';
    vga_update_cursor();
}

void vga_cursor_left(void) {
    if (cursor_x > 0) cursor_x--;
    vga_update_cursor();
}

void vga_cursor_right(void) {
    if (cursor_x < VGA_WIDTH - 1) cursor_x++;
    vga_update_cursor();
}

int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

int strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

void strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void draw_boot_menu(int selected) {
    vga_clear();
    
    vga_puts("[ OK ] System correctly booted...\n\n");
    vga_puts("Welcome to Piux!\n\n");
    vga_puts("Piux is an open source UNIX-like kernel written from scratch,\n");
    vga_puts("very lightweight and minimal.\n\n");
    vga_puts("Currently, the project is experimental, incomplete, and unstable.\n");
    vga_puts("Contribute on GitHub to improve and add features:\n");
    vga_puts("https://github.com/projectpiux/piux-kernel\n\n");
    vga_puts("Piux is distributed under the GNU GPLv3 license.\n\n");
    vga_puts("-----------------------------\n");
    vga_puts("Move with up/down arrow keys:\n\n");
    vga_puts(selected == 0 ? "> Install the system (TUI)\n" : "  Install the system (TUI)\n");
    vga_puts(selected == 1 ? "> Shell-only\n" : "  Shell-only\n");
    vga_puts(selected == 2 ? "> Shutdown\n" : "  Shutdown\n");
}

static int mark_first_boot_complete(void) {
    if (ext2_create_file(".piux-first-boot") < 0) return -1;
    return ext2_write_file_by_name(".piux-first-boot", "1", 1);
}

static int first_boot_menu(void) {
    int selected = 0;

    if (!ext2_is_mounted() || ext2_find_inode(".piux-first-boot") >= 0) return 0;

    draw_boot_menu(selected);
    while (1) {
        int key = keyboard_read_char();

        if (key == KEY_ARROW_UP && selected > 0) {
            selected--;
            draw_boot_menu(selected);
        } else if (key == KEY_ARROW_DOWN && selected < 2) {
            selected++;
            draw_boot_menu(selected);
        } else if (key == '\n') {
            if (selected == 0) {
                if (installer_run(vga_clear, vga_puts, vga_putc, power_off, reboot_system) == 1) {
                    mark_first_boot_complete();
                    return 1;
                }
            }
            if (selected == 1) {
                mark_first_boot_complete();
                return 1;
            }
            vga_clear();
            vga_puts("Shutting down...\n");
            power_off();
        }
    }
}

static void redraw_input_line(char *buffer, int length, int cursor, int old_length, int old_cursor) {
    int i;
    int drawn_length = length > old_length ? length : old_length;
    for (i = 0; i < old_cursor; i++) vga_cursor_left();
    for (i = 0; i < length; i++) vga_putc(buffer[i]);
    for (; i < old_length; i++) vga_putc(' ');
    for (i = drawn_length; i > cursor; i--) vga_cursor_left();
}

command_t* find_command(const char *name) {
    for (int i = 0; commands[i].name != 0; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return 0;
}

int shell_execute_command_line(const char *line, void (*puts)(const char *), void (*putc)(char)) {
    char command_line[80];
    char command_name[80];
    char parameter[80];
    int length = 0;
    int space_pos = -1;
    while (line[length] && length < (int)sizeof(command_line) - 1) {
        command_line[length] = line[length];
        length++;
    }
    command_line[length] = '\0';
    if (length == 0) return 0;
    if (length > 5 && command_line[0] == 's' && command_line[1] == 'u' &&
        command_line[2] == 'd' && command_line[3] == 'o' && command_line[4] == ' ') {
        if (!auth_current_user_is_sudoer()) {
            puts("Permission denied: user is not a sudoer.\n");
            return -1;
        }
        for (int index = 0; index <= length - 5; index++)
            command_line[index] = command_line[index + 5];
        length -= 5;
    }
    for (int index = 0; index < length; index++)
        if (command_line[index] == ' ') {
            space_pos = index;
            break;
        }
    if (space_pos > 0) {
        for (int index = 0; index < space_pos; index++)
            command_name[index] = command_line[index];
        command_name[space_pos] = '\0';
        int parameter_length = length - space_pos - 1;
        for (int index = 0; index < parameter_length; index++)
            parameter[index] = command_line[space_pos + index + 1];
        parameter[parameter_length] = '\0';
    } else {
        for (int index = 0; index <= length; index++) command_name[index] = command_line[index];
        parameter[0] = '\0';
    }
    {
        command_t *command = find_command(command_name);
        if (command) {
            vfs_console_putc_t previous_output = vfs_set_console_output(putc);
            command->handler(parameter, puts, putc);
            vfs_set_console_output(previous_output);
            return 1;
        }
    }
    puts("Unknown command: ");
    puts(command_name);
    putc('\n');
    return -1;
}

void shell(void) {
    char buffer[80];
    char history[16][80];
    int history_count = 0;
    int history_pos;
    int pos;
    int length;
    
    while (1) {
        if (auth_current_username()[0]) vga_puts(auth_current_username());
        else vga_puts("piux");
        if (ext2_is_mounted()) {
            vga_puts(" ");
            vga_puts(fs.current_path);
        }
        vga_puts("> ");
        pos = 0;
        length = 0;
        history_pos = history_count;
        
        while (1) {
            int c = keyboard_read_char();
            
            if (c == '\n') {
                vga_putc('\n');
                buffer[length] = '\0';
                break;
            }

            if (c == KEY_ARROW_LEFT && pos > 0) {
                pos--;
                vga_cursor_left();
                continue;
            }

            if (c == KEY_ARROW_RIGHT && pos < length) {
                pos++;
                vga_cursor_right();
                continue;
            }

            if (c == KEY_ARROW_UP && history_pos > 0) {
                int old_length = length;
                history_pos--;
                strcpy(buffer, history[history_pos]);
                length = strlen(buffer);
                pos = length;
                redraw_input_line(buffer, length, pos, old_length, old_length);
                continue;
            }

            if (c == KEY_ARROW_DOWN && history_pos < history_count) {
                int old_length = length;
                history_pos++;
                if (history_pos < history_count) strcpy(buffer, history[history_pos]);
                else buffer[0] = '\0';
                length = strlen(buffer);
                pos = length;
                redraw_input_line(buffer, length, pos, old_length, old_length);
                continue;
            }

            if (c == '\b' && pos > 0) {
                for (int i = pos - 1; i < length; i++) buffer[i] = buffer[i + 1];
                pos--;
                length--;
                redraw_input_line(buffer, length, pos, length + 1, pos + 1);
                continue;
            }

            if (c == KEY_DELETE && pos < length) {
                for (int i = pos; i < length; i++) buffer[i] = buffer[i + 1];
                length--;
                redraw_input_line(buffer, length, pos, length + 1, pos);
                continue;
            }

            if (c >= 32 && c < 127 && length < 79) {
                for (int i = length; i > pos; i--) buffer[i] = buffer[i - 1];
                buffer[pos++] = (char)c;
                length++;
                redraw_input_line(buffer, length, pos, length - 1, pos - 1);
            }
        }
        
        if (buffer[0] == '\0') {
            continue;
        }

        if (length > 5 && buffer[0] == 's' && buffer[1] == 'u' && buffer[2] == 'd' &&
            buffer[3] == 'o' && buffer[4] == ' ') {
            if (!auth_current_user_is_sudoer()) {
                vga_puts("Permission denied: user is not a sudoer.\n");
                continue;
            }
            for (int i = 0; i <= length - 5; i++) buffer[i] = buffer[i + 5];
            length -= 5;
        }

        if (history_count < 16) {
            strcpy(history[history_count++], buffer);
        } else {
            for (int i = 1; i < 16; i++) strcpy(history[i - 1], history[i]);
            strcpy(history[15], buffer);
        }
        
        shell_execute_command_line(buffer, vga_puts, vga_putc);
    }
}

static int startup_word_is(const char *line, const char *word) {
    int index = 0;
    while (word[index] && line[index] == word[index]) index++;
    return !word[index] && (line[index] == '\0' || line[index] == ' ' || line[index] == '\t');
}

// Runs the first command of the [STARTUP] block in /sbin/init (default: pde).
static void run_startup(void) {
    static char init[1024];
    int length = ext2_is_mounted() ? ext2_read_file_by_path("/sbin/init", init, sizeof(init) - 1) : -1;
    char command[128];
    int in_startup = 0;
    int position = 0;
    command[0] = '\0';
    if (length > 0) {
        init[length] = '\0';
        while (init[position] && !command[0]) {
            char *line = &init[position];
            int end = 0;
            while (line[end] && line[end] != '\n') end++;
            position += end + (line[end] ? 1 : 0);
            line[end] = '\0';
            if (end > 0 && line[end - 1] == '\r') line[--end] = '\0';
            while (*line == ' ' || *line == '\t') line++;
            if (!*line || *line == '#') continue;
            if (*line == '[') {
                in_startup = startup_word_is(line, "[STARTUP]");
                continue;
            }
            if (in_startup) {
                int i = 0;
                while (line[i] && i < (int)sizeof(command) - 1) { command[i] = line[i]; i++; }
                command[i] = '\0';
            }
        }
    }
    if (!command[0] && length <= 0) {
        command[0] = 'p'; command[1] = 'd'; command[2] = 'e'; command[3] = '\0';
    }
    if (!command[0]) return;
    if (startup_word_is(command, "pwm") || startup_word_is(command, "pmw"))
        pwm_run(vga_clear, vga_puts, vga_putc);
    else if (startup_word_is(command, "pde") || startup_word_is(command, "desktop"))
        desktop_run();
    else
        shell_execute_command_line(command, vga_puts, vga_putc);
}

void kernel_main(uint32_t magic, uint32_t addr) {
    asm volatile ("cli");
    system_info_init(magic, addr);
    video_init(magic, addr);
    vga_clear();
    vga_update_cursor();

    memory_init();
    gdt_init();
    ramfs_init();
    ext2_mount();
    keyboard_load_layout_from_config();
    vfs_init();
    process_init();
    syscall_init();
    timer_init(100);
    if (rtl8139_init() == 0) dhcp_configure();

    if (!first_boot_menu()) {
        vga_puts("Welcome to Piux!\n");
        vga_puts("Type 'help' for essential commands explaination use.\n\n");
    }

    if (ext2_find_inode_by_path("/.config/passwd") >= 0) {
        auth_login(vga_clear, vga_puts, vga_putc);
    }
    
    run_startup();
    shell();
}
