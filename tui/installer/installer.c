#include <stdint.h>
#include "ext2.h"
#include "keyboard.h"
#include "installer.h"
#include "auth.h"
#include "video.h"

#define INSTALLER_TOTAL_STEPS   3
#define INSTALLER_BOX_MIN_WIDTH 52
#define INSTALLER_MAX_ATTEMPTS  3
#define INSTALLER_PASSWORD_SIZE 65
#define INSTALLER_KEY_ESC       27
#define INSTALLER_KEY_DEL       127

static installer_clear_t clear_screen;
static installer_puts_t print_text;
static installer_putc_t print_char;
static installer_reboot_t reboot_system;

static int text_length(const char *text) {
    int length = 0;
    while (text[length]) length++;
    return length;
}

static int strings_equal(const char *left, const char *right) {
    int index = 0;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static int append_text(char *buffer, int size, int *offset, const char *text) {
    int length = text_length(text);
    if (*offset + length >= size) return -1;
    for (int i = 0; i < length; i++) buffer[*offset + i] = text[i];
    *offset += length;
    buffer[*offset] = '\0';
    return 0;
}

static void wipe_buffer(char *buffer, int size) {
    volatile char *p = buffer;
    for (int i = 0; i < size; i++) p[i] = 0;
}

static int is_enter(int key) {
    return key == '\n' || key == '\r';
}

static int screen_columns(void) {
    int columns = (int)video_columns;
    return columns > 0 ? columns : 80;
}

static void print_spaces(int count) {
    for (int i = 0; i < count; i++) print_char(' ');
}

static void print_centered(const char *text) {
    int length = text_length(text);
    int columns = screen_columns();
    print_spaces(columns > length ? (columns - length) / 2 : 0);
    print_text(text);
    print_char('\n');
}

static void box_geometry(int content_width, int *width, int *margin) {
    int columns = screen_columns();
    int w = content_width > INSTALLER_BOX_MIN_WIDTH ? content_width : INSTALLER_BOX_MIN_WIDTH;
    if (w > columns - 2) w = columns - 2;
    if (w < 20) w = 20;
    *width = w;
    *margin = columns > w ? (columns - w) / 2 : 0;
}

static void print_border(int margin, int width) {
    print_spaces(margin);
    print_char('+');
    for (int i = 0; i < width - 2; i++) print_char('-');
    print_char('+');
    print_char('\n');
}

static void print_row(int margin, int width, const char *prefix, const char *text, int centered) {
    int inner = width - 2;
    int prefix_length = text_length(prefix);
    int text_len = text_length(text);
    int max_len = inner - 4;
    int length, left, right;

    if (prefix_length > max_len) prefix_length = max_len;
    if (prefix_length + text_len > max_len) text_len = max_len - prefix_length;
    if (text_len < 0) text_len = 0;

    length = prefix_length + text_len;
    left = centered ? (inner - length) / 2 : 2;
    right = inner - left - length;

    print_spaces(margin);
    print_char('|');
    print_spaces(left);
    for (int i = 0; i < prefix_length; i++) print_char(prefix[i]);
    for (int i = 0; i < text_len; i++) print_char(text[i]);
    print_spaces(right);
    print_char('|');
    print_char('\n');
}

static void print_header(int step, const char *title) {
    char progress[32];
    int offset = 0;
    int width, margin;

    box_geometry(text_length(title) + 6, &width, &margin);

    append_text(progress, sizeof(progress), &offset, "Step ");
    progress[offset++] = (char)('0' + step);
    progress[offset] = '\0';
    append_text(progress, sizeof(progress), &offset, " of 3  [");
    for (int i = 1; i <= INSTALLER_TOTAL_STEPS; i++) progress[offset++] = i <= step ? '#' : '-';
    progress[offset++] = ']';
    progress[offset] = '\0';

    clear_screen();
    print_char('\n');
    print_border(margin, width);
    print_row(margin, width, "", "PIUX INSTALLER", 1);
    print_row(margin, width, "", progress, 1);
    print_border(margin, width);
    print_row(margin, width, "", title, 1);
    print_border(margin, width);
    print_char('\n');
}

static void wait_enter(const char *message) {
    int key;
    print_text(message);
    do {
        key = keyboard_read_char();
    } while (!is_enter(key));
    print_char('\n');
}

static void show_error(const char *message) {
    print_text("\n[!] ");
    print_text(message);
    print_char('\n');
    wait_enter("    Press Enter to return.");
}

static void draw_box(const char *title, const char **items, int count, int selected) {
    int item_width = 0;
    int width, margin;

    for (int i = 0; i < count; i++) {
        int length = text_length(items[i]);
        if (length > item_width) item_width = length;
    }
    {
        int title_width = text_length(title);
        int content = (item_width + 8 > title_width + 6) ? item_width + 8 : title_width + 6;
        box_geometry(content, &width, &margin);
    }

    clear_screen();
    print_text("\n\n");
    print_border(margin, width);
    print_row(margin, width, "", title, 1);
    print_border(margin, width);
    print_row(margin, width, "", "", 0);
    for (int i = 0; i < count; i++) {
        print_row(margin, width, i == selected ? "> " : "  ", items[i], 0);
    }
    print_row(margin, width, "", "", 0);
    print_border(margin, width);
    print_centered("[Up/Down] Select  [Enter] Open  [Esc] Back");
    if (count > 1 && count <= 9) print_centered("[1-9] Quick pick");
}

static int choose(const char **items, int count, const char *title) {
    int selected = 0;
    int redraw = 1;
    while (1) {
        int key;
        if (redraw) draw_box(title, items, count, selected);
        redraw = 0;
        key = keyboard_read_char();
        if (key == KEY_ARROW_UP) {
            selected = selected > 0 ? selected - 1 : count - 1;
            redraw = 1;
        } else if (key == KEY_ARROW_DOWN) {
            selected = selected < count - 1 ? selected + 1 : 0;
            redraw = 1;
        } else if (is_enter(key)) {
            return selected;
        } else if (key == INSTALLER_KEY_ESC) {
            return -1;
        } else if (key >= '1' && key <= '9' && (key - '1') < count) {
            return key - '1';
        }
    }
}

static int read_line(char *buffer, int size, int secret) {
    int length = 0;
    buffer[0] = '\0';
    while (1) {
        int key = keyboard_read_char();
        if (is_enter(key)) {
            buffer[length] = '\0';
            print_char('\n');
            return length;
        }
        if (key == INSTALLER_KEY_ESC) {
            buffer[0] = '\0';
            print_char('\n');
            return -1;
        }
        if ((key == '\b' || key == INSTALLER_KEY_DEL) && length > 0) {
            length--;
            print_char('\b');
            continue;
        }
        if (key >= 32 && key < 127 && length < size - 1) {
            buffer[length++] = (char)key;
            print_char(secret ? '*' : (char)key);
        }
    }
}

static int ensure_directory(const char *path) {
    if (ext2_find_inode_by_path(path) >= 0) return 0;
    return ext2_create_directory_by_path(path) < 0 ? -1 : 0;
}

static int write_config(const char *path, const char *content) {
    int inode = ext2_find_inode_by_path(path);
    uint32_t length = (uint32_t)text_length(content);
    if (inode < 0) {
        if (ext2_create_file_by_path(path) < 0) return -1;
        inode = ext2_find_inode_by_path(path);
    }
    return inode < 0 || ext2_write_file((uint32_t)inode, content, length) < 0 ? -1 : 0;
}

static int write_new_file(const char *path, const char *content) {
    if (ext2_find_inode_by_path(path) >= 0) return 0;
    return write_config(path, content);
}

static int line_exists(const char *content, int length, const char *line) {
    int line_length = text_length(line);
    int start = 0;
    while (start < length) {
        int end = start;
        while (end < length && content[end] != '\n') end++;
        if (end - start == line_length) {
            int same = 1;
            for (int i = 0; i < line_length; i++) {
                if (content[start + i] != line[i]) { same = 0; break; }
            }
            if (same) return 1;
        }
        start = end + 1;
    }
    return 0;
}

static int append_config_line(const char *path, const char *line) {
    char content[256];
    int length = ext2_read_file_by_path(path, content, sizeof(content) - 1);
    if (length < 0) length = 0;
    content[length] = '\0';
    if (line_exists(content, length, line)) return 0;
    if (length > 0 && content[length - 1] != '\n') {
        if (append_text(content, sizeof(content), &length, "\n") < 0) return -1;
    }
    if (append_text(content, sizeof(content), &length, line) < 0) return -1;
    if (append_text(content, sizeof(content), &length, "\n") < 0) return -1;
    return write_config(path, content);
}

static int valid_username(const char *username) {
    if (username[0] == '\0') return 0;
    for (int i = 0; username[i]; i++) {
        char c = username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int select_keyboard_layout(void) {
    const char *layouts[] = { "US QWERTY", "Italian QWERTY" };
    int choice = choose(layouts, 2, "Step 1 of 3: Keyboard layout");
    if (choice < 0) return -1;

    keyboard_set_layout(choice == 1 ? KEYBOARD_LAYOUT_IT : KEYBOARD_LAYOUT_US);

    if (ensure_directory("/.config") < 0 ||
        write_config("/.config/keyboard.conf", choice == 1 ? "layout=it\n" : "layout=us\n") < 0) {
        print_header(1, "Keyboard layout");
        show_error("Cannot save the keyboard layout to the filesystem.");
        return -1;
    }

    print_header(1, "Keyboard layout");
    print_text(choice == 1 ? "  Selected: Italian QWERTY\n\n" : "  Selected: US QWERTY\n\n");
    wait_enter("  Press Enter to continue.");
    return 0;
}

static int report_step(const char *label, int result) {
    int length = text_length(label);
    print_text("  ");
    print_text(label);
    print_spaces(length < 30 ? 30 - length : 1);
    print_text(result >= 0 ? "[ OK ]\n" : "[FAIL]\n");
    return result;
}

static int create_system_tree(void) {
    static const char *directories[] = {
        "/.config", "/bin", "/boot", "/dev", "/etc", "/home", "/lib", "/mnt",
        "/opt", "/proc", "/root", "/run", "/sbin", "/tmp", "/usr",
        "/var", "/var/log", "/var/tmp"
    };
    int count = sizeof(directories) / sizeof(directories[0]);
    for (int i = 0; i < count; i++) {
        if (report_step(directories[i], ensure_directory(directories[i])) < 0) return -1;
    }
    if (report_step("/.config/system.conf",
            write_config("/.config/system.conf", "root=/dev/hda\nfilesystem=ext2\ninit=/sbin/init\n")) < 0) return -1;
    if (report_step("/.config/disk.conf",
            write_config("/.config/disk.conf", "device=/dev/hda\nfilesystem=ext2\nmountpoint=/\n")) < 0) return -1;
    if (report_step("/sbin/init",
            write_config("/sbin/init", "# Piux init: a basic init file.\n# Use hashtags for comments or disabled commands.\n# Enjoy!\n\n[STARTUP]\npde\n# pwm\n\n")) < 0) return -1;
    if (report_step("/etc/fstab",
            write_new_file("/etc/fstab", "/dev/hda / ext2 defaults 0 1\n")) < 0) return -1;
    return 0;
}

static int install_system(void) {
    print_header(2, "Disk and system setup");
    print_text("  Creating Unix filesystem tree...\n\n");
    if (create_system_tree() < 0) {
        print_text("\n[!] Installation failed: cannot write the Ext2 filesystem.\n");
        wait_enter("    Press Enter to return.");
        return -1;
    }
    print_text("\n  Filesystem tree created.\n");
    print_text("  Configuration stored in /.config/.\n\n");
    wait_enter("  Press Enter to continue with management.");
    return 0;
}

static int ask_username(char *username, int size) {
    for (int attempt = 0; attempt < INSTALLER_MAX_ATTEMPTS; attempt++) {
        int length;
        print_text("  Username (a-z, 0-9, _ -): ");
        length = read_line(username, size, 0);
        if (length < 0) return -1;
        if (length == 0 || !valid_username(username)) {
            print_text("  [!] Invalid username: use only a-z, 0-9, '_' and '-'.\n");
        } else if (strings_equal(username, "root")) {
            print_text("  [!] \"root\" is reserved: choose another name.\n");
        } else {
            return 0;
        }
    }
    show_error("Too many invalid attempts.");
    return -1;
}

static int ask_password(char *password, char *confirmation, int size) {
    for (int attempt = 0; attempt < INSTALLER_MAX_ATTEMPTS; attempt++) {
        int length;
        print_text("  Password: ");
        length = read_line(password, size, 1);
        if (length < 0) return -1;
        if (length == 0) {
            print_text("  [!] Password cannot be empty.\n");
            continue;
        }
        print_text("  Confirm password: ");
        length = read_line(confirmation, size, 1);
        if (length < 0) return -1;
        if (strings_equal(password, confirmation)) return 0;
        print_text("  [!] Passwords do not match. Try again.\n");
    }
    show_error("Too many invalid attempts.");
    return -1;
}

static int write_user_files(const char *username, int sudoer) {
    char sudoers[128];
    char home[AUTH_USERNAME_SIZE + 7];
    int offset = 0;

    sudoers[0] = '\0';
    if (append_text(sudoers, sizeof(sudoers), &offset, "root ALL=(ALL) ALL\n") < 0) return -1;
    if (sudoer) {
        if (append_text(sudoers, sizeof(sudoers), &offset, username) < 0) return -1;
        if (append_text(sudoers, sizeof(sudoers), &offset, " ALL=(ALL) ALL\n") < 0) return -1;
    }
    if (write_config("/.config/sudoers", sudoers) < 0) return -1;
    if (sudoer && write_config("/.config/sudo-user", username) < 0) return -1;

    offset = 0;
    home[0] = '\0';
    if (append_text(home, sizeof(home), &offset, "/home/") < 0) return -1;
    if (append_text(home, sizeof(home), &offset, username) < 0) return -1;
    if (ensure_directory(home) < 0) return -1;
    return append_config_line("/.config/users", username);
}

static int create_user_step(void) {
    char username[AUTH_USERNAME_SIZE];
    char password[INSTALLER_PASSWORD_SIZE];
    char confirmation[INSTALLER_PASSWORD_SIZE];
    const char *sudo_items[] = { "Yes, make this user a sudoer", "No, standard user" };
    int sudoer;
    int result;

    print_header(3, "Create the first user");
    print_text("  Press Esc at any prompt to go back.\n\n");

    if (ask_username(username, sizeof(username)) < 0) return -1;

    if (ask_password(password, confirmation, sizeof(password)) < 0) {
        wipe_buffer(password, sizeof(password));
        wipe_buffer(confirmation, sizeof(confirmation));
        return -1;
    }
    wipe_buffer(confirmation, sizeof(confirmation));

    sudoer = choose(sudo_items, 2, "Step 3 of 3: Administrator access") == 0;

    result = auth_create_user(username, password, sudoer);
    wipe_buffer(password, sizeof(password));
    if (result < 0) {
        print_header(3, "Create the first user");
        show_error("Could not save the user.");
        return -1;
    }

    if (write_user_files(username, sudoer) < 0) {
        print_header(3, "Create the first user");
        show_error("Could not write the user configuration.");
        return -1;
    }
    return 0;
}

static int complete_installation(void) {
    int created, written;

    print_header(3, "Finishing installation");

    if (report_step("/.config/installed", write_config("/.config/installed", "yes\n")) < 0) {
        show_error("Cannot mark the system as installed.");
        return -1;
    }

    created = ext2_create_file(".piux-first-boot");
    written = created >= 0 || ext2_find_inode_by_path("/.piux-first-boot") >= 0
              ? ext2_write_file_by_name(".piux-first-boot", "1", 1) : -1;
    report_step("first-boot flag", written);

    print_text("\n  Installation complete.\n");
    print_text("  The system will reboot now.\n\n");
    wait_enter("  Press Enter to reboot.");
    clear_screen();
    reboot_system();
    return 0;
}

int installer_run(installer_clear_t clear, installer_puts_t puts,
                  installer_putc_t putc, installer_power_off_t power_off,
                  installer_reboot_t reboot) {
    (void)power_off;
    if (!clear || !puts || !putc || !reboot) return 0;

    clear_screen = clear;
    print_text = puts;
    print_char = putc;
    reboot_system = reboot;

    if (select_keyboard_layout() < 0) return 0;
    if (install_system() < 0) return 0;
    if (create_user_step() < 0) return 0;
    if (complete_installation() < 0) return 0;
    return 1;
}