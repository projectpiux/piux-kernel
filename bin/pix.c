#include <stdint.h>
#include "../kernel/rtl8139.h"
#include "../kernel/net.h"
#include "../kernel/vfs.h"
#include "../kernel/ext2.h"
#include "../kernel/http.h"
#include "../kernel/tls.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

/* GitHub tree URL for packages; downloads use its raw.githubusercontent.com equivalent. */
#define PIX_REPOSITORY "https://github.com/projectpiux/pix-pkgmanager/tree/main/pkgs"
#define PIX_HOST "raw.githubusercontent.com"
#define PIX_PORT 443
#define PIX_FALLBACK_IP "185.199.109.133"
static uint32_t repo_ip;

static int split_tokens(const char *input, char tokens[][64], int max_tokens) {
    int count = 0;
    int index = 0;
    while (input[index] && count < max_tokens) {
        int length = 0;
        while (input[index] == ' ') index++;
        if (!input[index]) break;
        while (input[index] && input[index] != ' ' && length < 63) tokens[count][length++] = input[index++];
        tokens[count][length] = '\0';
        count++;
    }
    return count;
}

static int match(const char *text, const char *word) {
    int index = 0;
    while (text[index] && word[index] && text[index] == word[index]) index++;
    return text[index] == '\0' && word[index] == '\0';
}

static void append_str(char *dest, int *length, const char *src) {
    for (int index = 0; src[index]; index++) dest[(*length)++] = src[index];
    dest[*length] = '\0';
}

static void build_pkg_basename(char *out, const char *pkg) {
    int length = 0;
    append_str(out, &length, pkg);
}

static void build_remote_path(char *out, const char *pkg, const char *filename) {
    int length = 0;
    append_str(out, &length, "/projectpiux/pix-pkgmanager/main/pkgs/");
    append_str(out, &length, pkg);
    append_str(out, &length, "/");
    append_str(out, &length, filename);
}

static void build_local_dir(char *out, const char *pkg) {
    int length = 0;
    append_str(out, &length, "/pkgs/");
    append_str(out, &length, pkg);
}

static void build_local_path(char *out, const char *pkg, const char *filename) {
    int length = 0;
    append_str(out, &length, "/pkgs/");
    append_str(out, &length, pkg);
    append_str(out, &length, "/");
    append_str(out, &length, filename);
}

static int valid_package_name(const char *pkg) {
    int length = 0;
    for (int index = 0; pkg[index]; index++) {
        char character = pkg[index];
        length++;
        if (length > 48) return 0;
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '-' || character == '_')) return 0;
    }
    return length > 0;
}

static void print_u32(uint32_t value, vga_putc_t vga_putc) {
    char digits[10];
    int length = 0;
    if (value == 0) {
        vga_putc('0');
        return;
    }
    while (value > 0) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (length > 0) vga_putc(digits[--length]);
}

static void show_usage(vga_puts_t vga_puts) {
    vga_puts("Usage: pix [install <pkg> | remove <pkg> | info <pkg> | where <pkg>]\n");
}

static int fetch_package_file(const char *pkg, const char *filename, const char *local_path) {
    char remote_path[128];
    int fd;
    int bytes;

    build_remote_path(remote_path, pkg, filename);
    fd = vfs_open(local_path, VFS_O_WRITE | VFS_O_CREATE);
    if (fd < 0) return -1;
    bytes = https_get_to_fd(repo_ip, PIX_PORT, remote_path, PIX_HOST, fd);
    vfs_close(fd);
    return bytes;
}

void cmd_pix(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    char tokens[3][64];
    int count;

    if (param[0] == '\0') {
        show_usage(vga_puts);
        return;
    }

    count = split_tokens(param, tokens, 3);
    if (count == 0) {
        show_usage(vga_puts);
        return;
    }

    if (match(tokens[0], "info") && count == 2) {
        char local_path[80];
        char buffer[512];
        int bytes;
        build_local_path(local_path, tokens[1], "info");
        bytes = ext2_read_file_by_path(local_path, buffer, sizeof(buffer) - 1);
        if (bytes < 0) {
            vga_puts("Package '");
            vga_puts(tokens[1]);
            vga_puts("' is not installed\n");
            return;
        }
        buffer[bytes] = '\0';
        vga_puts(buffer);
        vga_putc('\n');
        return;
    }

    if (match(tokens[0], "where") && count == 2) {
        char package_path[80];
        build_local_path(package_path, tokens[1], tokens[1]);
        if (ext2_find_inode_by_path(package_path) < 0) {
            vga_puts("Package '");
            vga_puts(tokens[1]);
            vga_puts("' is not installed\n");
            return;
        }
        vga_puts(package_path);
        vga_putc('\n');
        return;
    }

    if (match(tokens[0], "remove") && count == 2) {
        char package_path[80];
        char info_local[80];
        int removed_any = 0;

        build_local_path(package_path, tokens[1], tokens[1]);
        build_local_path(info_local, tokens[1], "info");

        if (ext2_delete_file_by_path(package_path) == 0) removed_any = 1;
        if (ext2_delete_file_by_path(info_local) == 0) removed_any = 1;

        if (!removed_any) {
            vga_puts("Package '");
            vga_puts(tokens[1]);
            vga_puts("' is not installed\n");
            return;
        }
        vga_puts("Removed '");
        vga_puts(tokens[1]);
        vga_puts("'\n");
        return;
    }

    if (match(tokens[0], "install") && count == 2) {
        char local_dir[80];
        char elf_basename[68];
        char elf_local[80];
        char info_local[80];
        int elf_bytes;
        int info_bytes;
        uint32_t target_ip;

        if (!valid_package_name(tokens[1])) {
            vga_puts("Invalid package name\n");
            return;
        }
        if (!rtl8139_is_ready()) {
            vga_puts("No network interface detected\n");
            return;
        }
        if (!net_resolve_host(PIX_HOST, &target_ip)) target_ip = net_parse_ip(PIX_FALLBACK_IP);
        repo_ip = target_ip;

        if (ext2_find_inode_by_path("/pkgs") < 0) ext2_create_directory_by_path("/pkgs");
        build_local_dir(local_dir, tokens[1]);
        if (ext2_find_inode_by_path(local_dir) < 0 && ext2_create_directory_by_path(local_dir) < 0) {
            vga_puts("Failed to create package directory\n");
            return;
        }

        build_pkg_basename(elf_basename, tokens[1]);
        build_local_path(elf_local, tokens[1], tokens[1]);
        build_local_path(info_local, tokens[1], "info");

        elf_bytes = fetch_package_file(tokens[1], elf_basename, elf_local);
        info_bytes = fetch_package_file(tokens[1], "info", info_local);

        if (elf_bytes <= 0) {
            if (elf_bytes < -1000) {
                vga_puts("HTTPS/TLS failed, error ");
                print_u32((uint32_t)(-elf_bytes - 1000), vga_putc);
                vga_putc('\n');
            } else {
                vga_puts("Package executable not found in ");
                vga_puts(PIX_REPOSITORY);
                vga_putc('\n');
            }
            return;
        }
        if (info_bytes <= 0) {
            vga_puts("Package info not found in ");
            vga_puts(PIX_REPOSITORY);
            vga_putc('\n');
            return;
        }

        vga_puts("Installed '");
        vga_puts(tokens[1]);
        vga_puts(" to ");
        vga_puts(local_dir);
        vga_putc('\n');
        return;
    }

    show_usage(vga_puts);
}
