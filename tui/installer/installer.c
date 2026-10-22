#include <stdint.h>
#include "ext2.h"
#include "keyboard.h"
#include "installer.h"
#include "auth.h"
#include "video.h"

static installer_clear_t clear_screen;
static installer_puts_t print_text;
static installer_putc_t print_char;
static installer_reboot_t reboot_system;

static int strings_equal(const char *left, const char *right) {
    int index = 0;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static void print_spaces(int count) {
    for (int i = 0; i < count; i++) print_char(' ');
}

static void print_centered(const char *text) {
    int length = 0;
    while (text[length]) length++;
    int padding = video_columns > length ? (video_columns - length) / 2 : 0;
    print_spaces(padding);
    print_text(text);
    print_char('\n');
}

static void draw_box(const char *title, const char **items, int count, int selected) {
    int item_width = 0;
    for (int i = 0; i < count; i++) {
        int length = 0;
        while (items[i][length]) length++;
        if (length > item_width) item_width = length;
    }
    item_width += 2;
    clear_screen();
    print_text("\n\n");
    print_centered("+--------------------------------------------------+");
    print_centered(title);
    print_centered("+--------------------------------------------------+");
    for (int i = 0; i < count; i++) {
        int padding = video_columns > item_width ? (video_columns - item_width) / 2 : 0;
        print_spaces(padding);
        print_text(i == selected ? "> " : "  ");
        print_text(items[i]);
        print_char('\n');
    }
    print_centered("+--------------------------------------------------+");
    print_centered("[Up/Down] Select  [Enter] Open  [Esc] Back");
}

static int read_line(char *buffer, int size, int secret) {
    int length = 0;
    while (1) {
        int key = keyboard_read_char();
        if (key == '\n') {
            buffer[length] = '\0';
            print_char('\n');
            return length;
        }
        if (key == '\b' && length > 0) {
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

static int choose(const char **items, int count, const char *title) {
    int selected = 0;
    while (1) {
        int key;
        draw_box(title, items, count, selected);
        key = keyboard_read_char();
        if (key == KEY_ARROW_UP && selected > 0) selected--;
        else if (key == KEY_ARROW_DOWN && selected < count - 1) selected++;
        else if (key == '\n') return selected;
        else if (key == 27) return -1;
    }
}

static int ensure_directory(const char *path) {
    if (ext2_find_inode_by_path(path) >= 0) return 0;
    return ext2_create_directory_by_path(path) < 0 ? -1 : 0;
}

static int write_new_file(const char *path, const char *content) {
    if (ext2_find_inode_by_path(path) >= 0) return 0;
    if (ext2_create_file_by_path(path) < 0) return -1;
    return ext2_write_file_by_path(path, content, 0) < 0 ? -1 : 0;
}

static int write_config(const char *path, const char *content) {
    int inode = ext2_find_inode_by_path(path);
    uint32_t length = 0;
    while (content[length]) length++;
    if (inode < 0) {
        if (ext2_create_file_by_path(path) < 0) return -1;
        inode = ext2_find_inode_by_path(path);
    }
    return inode < 0 || ext2_write_file((uint32_t)inode, content, length) < 0 ? -1 : 0;
}

static int append_config_line(const char *path, const char *line) {
    char content[256];
    int length = ext2_read_file_by_path(path, content, sizeof(content) - 1);
    int line_length = 0;
    while (line[line_length]) line_length++;
    if (length < 0) length = 0;
    if (length > 0 && content[length - 1] != '\n') content[length++] = '\n';
    if (length + line_length >= (int)sizeof(content)) return -1;
    for (int i = 0; i < line_length; i++) content[length + i] = line[i];
    length += line_length;
    content[length] = '\0';
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
    const char *layout_text;
    char config[32];
    int choice = choose(layouts, 2, "Step 1 of 3: Keyboard layout");
    int offset = 0;
    if (choice < 0) return -1;
    keyboard_set_layout(choice == 1 ? KEYBOARD_LAYOUT_IT : KEYBOARD_LAYOUT_US);
    layout_text = choice == 1 ? "layout=it\n" : "layout=us\n";
    if (ensure_directory("/.config") < 0) return -1;
    while (layout_text[offset]) {
        config[offset] = layout_text[offset];
        offset++;
    }
    config[offset] = '\0';
    if (write_config("/.config/keyboard.conf", config) < 0) return -1;
    clear_screen();
    print_text(choice == 1 ? "Keyboard layout: Italian QWERTY\n\n" : "Keyboard layout: US QWERTY\n\n");
    print_text("Press Enter to continue.\n");
    keyboard_read_char();
    return 0;
}

static int create_system_tree(void) {
    static const char *directories[] = {
        "/bin", "/boot", "/dev", "/etc", "/home", "/lib", "/mnt",
        "/opt", "/proc", "/root", "/run", "/sbin", "/tmp", "/usr",
        "/var", "/var/log", "/var/tmp", "/.config"
    };
    int count = sizeof(directories) / sizeof(directories[0]);
    for (int i = 0; i < count; i++) {
        if (ensure_directory(directories[i]) < 0) return -1;
    }
    if (write_config("/.config/system.conf", "root=/dev/hda\nfilesystem=ext2\ninit=/sbin/init\n") < 0) return -1;
    if (write_config("/.config/disk.conf", "device=/dev/hda\nfilesystem=ext2\nmountpoint=/\n") < 0) return -1;
    if (write_config("/sbin/init", "# Piux init: a basic init file.\n# Use hashtags for comments or disabled commands.\n# Enjoy!\n\n[STARTUP]\npde\n# pwm\n\n") < 0) return -1;
    if (write_new_file("/etc/fstab", "/dev/hda / ext2 defaults 0 1\n") < 0) return -1;
    return 0;
}

static int install_system(void) {
    clear_screen();
    print_text("Step 2 of 3: Disk and system setup\n\nCreating Unix filesystem tree...\n");
    if (create_system_tree() < 0) {
        print_text("Installation failed: cannot write the Ext2 filesystem.\n");
        print_text("Press Enter to return.");
        keyboard_read_char();
        return -1;
    }
    print_text("Filesystem tree created.\n");
    print_text("Configuration stored in /.config/.\n\n");
    print_text("Press Enter to continue with management.");
    keyboard_read_char();
    return 0;
}

static int create_user_step(void) {
    char username[AUTH_USERNAME_SIZE];
    char password[65];
    char confirmation[65];
    const char *sudo_items[] = { "Yes, make this user a sudoer", "No, standard user" };
    int sudoer;
    clear_screen();
    print_text("Step 3 of 3: Create the first user\n\nUsername: ");
    if (read_line(username, sizeof(username), 0) == 0 || !valid_username(username)) {
        print_text("Invalid username. Press Enter to return.");
        keyboard_read_char();
        return -1;
    }
    print_text("Password: ");
    read_line(password, sizeof(password), 1);
    if (password[0] == '\0') {
        print_text("Password cannot be empty. Press Enter to return.");
        keyboard_read_char();
        return -1;
    }
    print_text("Confirm password: ");
    read_line(confirmation, sizeof(confirmation), 1);
    if (!strings_equal(password, confirmation)) {
        print_text("Passwords do not match. Press Enter to return.");
        keyboard_read_char();
        return -1;
    }
    sudoer = choose(sudo_items, 2, "Step 3 of 3: Administrator access") == 0;
    if (auth_create_user(username, password, sudoer) < 0) {
        print_text("Could not save the user. Press Enter to return.");
        keyboard_read_char();
        return -1;
    }
    {
        char sudoers[128];
        int offset = 0;
        const char *root_rule = "root ALL=(ALL) ALL\n";
        const char *user_rule = " ALL=(ALL) ALL\n";
        for (int i = 0; root_rule[i]; i++) sudoers[offset++] = root_rule[i];
        if (sudoer) {
            for (int i = 0; username[i]; i++) sudoers[offset++] = username[i];
            for (int i = 0; user_rule[i]; i++) sudoers[offset++] = user_rule[i];
        }
        sudoers[offset] = '\0';
        if (write_config("/.config/sudoers", sudoers) < 0) return -1;
    }
    if (sudoer && write_config("/.config/sudo-user", username) < 0) return -1;
    {
        char home[AUTH_USERNAME_SIZE + 7];
        int index = 0;
        home[0] = '/'; home[1] = 'h'; home[2] = 'o'; home[3] = 'm'; home[4] = 'e'; home[5] = '/';
        while (username[index]) { home[index + 6] = username[index]; index++; }
        home[index + 6] = '\0';
        if (ensure_directory(home) < 0 || append_config_line("/.config/users", username) < 0) return -1;
    }
    return 0;
}

static void complete_installation(void) {
    write_config("/.config/installed", "yes\n");
    if (ext2_create_file(".piux-first-boot") >= 0) ext2_write_file_by_name(".piux-first-boot", "1", 1);
    clear_screen();
    print_text("Installation complete.\n");
    print_text("The system will reboot now.\n");
    reboot_system();
}

int installer_run(installer_clear_t clear, installer_puts_t puts,
                  installer_putc_t putc, installer_power_off_t power_off,
                  installer_reboot_t reboot) {
    clear_screen = clear;
    print_text = puts;
    print_char = putc;
    (void)power_off;
    reboot_system = reboot;
    if (select_keyboard_layout() < 0) return 0;
    if (install_system() < 0) return 0;
    if (create_user_step() < 0) return 0;
    complete_installation();
    return 1;
}
