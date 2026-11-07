#include <stdint.h>
#include "../kernel/drivers/rtl8139.h"
#include "../kernel/net/net.h"
#include "../kernel/net/ip.h"
#include "../kernel/net/arp.h"
#include "../kernel/net/icmp.h"
#include "../kernel/net/udp.h"
#include "../kernel/net/http.h"
#include "../kernel/fs/vfs.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

static void print_hex_byte(uint8_t value, vga_putc_t putc) {
    static const char digits[] = "0123456789abcdef";
    putc(digits[(value >> 4) & 0x0F]);
    putc(digits[value & 0x0F]);
}

static void print_mac(const uint8_t mac[6], vga_putc_t putc) {
    for (int index = 0; index < 6; index++) {
        print_hex_byte(mac[index], putc);
        if (index < 5) putc(':');
    }
}

static void print_ip(uint32_t address, vga_puts_t vga_puts) {
    char text[16];
    net_format_ip(address, text);
    vga_puts(text);
}

static uint16_t string_length(const char *text) {
    uint16_t length = 0;
    while (text[length]) length++;
    return length;
}

static void append_str(char *dest, int *length, const char *src) {
    while (*src) dest[(*length)++] = *src++;
    dest[*length] = '\0';
}

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

static int resolve_target(const char *host, uint32_t *target, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    if (!net_resolve_host(host, target)) {
        vga_puts("Unknown host: ");
        vga_puts(host);
        vga_putc('\n');
        return 0;
    }
    return 1;
}

static void print_u32(uint32_t value, vga_putc_t putc) {
    char digits[11];
    int length = 0;
    if (value == 0) {
        putc('0');
        return;
    }
    while (value > 0) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (length > 0) putc(digits[--length]);
}

static void show_status(vga_puts_t vga_puts, vga_putc_t vga_putc) {
    uint8_t mac[6];

    if (!rtl8139_is_ready()) {
        vga_puts("No network interface detected\n");
        return;
    }

    rtl8139_get_mac(mac);
    vga_puts("RTL8139 network interface ready\n");
    vga_puts("MAC address: ");
    print_mac(mac, vga_putc);
    vga_putc('\n');
    vga_puts("IP address:  ");
    print_ip(ip_get_local_ip(), vga_puts);
    vga_putc('\n');
    vga_puts("Netmask:     ");
    print_ip(ip_get_netmask(), vga_puts);
    vga_putc('\n');
    vga_puts("Gateway:     ");
    print_ip(ip_get_gateway(), vga_puts);
    vga_putc('\n');
    vga_puts("DNS server:  ");
    print_ip(ip_get_dns(), vga_puts);
    vga_putc('\n');
    vga_puts("\nUsage:\n");
    vga_puts("  net config <ip> <netmask> <gateway>\n");
    vga_puts("  net arp <host-or-ip>\n");
    vga_puts("  net ping <host-or-ip>\n");
    vga_puts("  net udp <host-or-ip> <port> <text>\n");
    vga_puts("  net get <host-or-ip> <port> <path> <output-file>   (plain HTTP/1.0 GET)\n");
    vga_puts("  net https <host-or-ip> <path> <output-file>        (HTTPS/TLS GET)\n");
    vga_puts("  net fetch <host> <port> <path>            (show HTTP response body)\n");
    vga_puts("  net search <term> [term2 ...]             (Google-like GET query)\n");
    vga_puts("  net send <text>   (raw broadcast diagnostic frame)\n");
}

static int fetch_response_body(uint32_t target, uint16_t port, const char *host, const char *path, char *buffer, uint32_t buffer_size) {
    const char *temp_path = "/tmp/http_fetch.txt";
    int fd;
    int bytes;

    fd = vfs_open(temp_path, VFS_O_WRITE | VFS_O_CREATE);
    if (fd < 0) return -1;
    bytes = http_get_to_fd(target, port, path, host, fd);
    vfs_close(fd);
    if (bytes <= 0) return -1;

    fd = vfs_open(temp_path, VFS_O_READ);
    if (fd < 0) return -1;
    bytes = vfs_read(fd, buffer, buffer_size - 1);
    vfs_close(fd);
    if (bytes < 0) return -1;
    buffer[bytes] = '\0';
    return bytes;
}

static void print_http_body(const char *text, vga_putc_t putc) {
    int index = 0;
    while (text[index] != '\0') {
        putc(text[index]);
        index++;
    }
}

void cmd_net(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    char tokens[6][64];
    int count;

    if (param[0] == '\0') {
        show_status(vga_puts, vga_putc);
        return;
    }

    if (!rtl8139_is_ready()) {
        vga_puts("No network interface detected\n");
        return;
    }

    count = split_tokens(param, tokens, 6);
    if (count == 0) {
        show_status(vga_puts, vga_putc);
        return;
    }

    if (match(tokens[0], "config") && count == 4) {
        uint32_t address = net_parse_ip(tokens[1]);
        uint32_t netmask = net_parse_ip(tokens[2]);
        uint32_t gateway = net_parse_ip(tokens[3]);
        ip_set_config(address, netmask, gateway);
        vga_puts("Network configuration updated\n");
        return;
    }

    if (match(tokens[0], "arp") && count == 2) {
        uint8_t mac[6];
        uint32_t target;
        if (!resolve_target(tokens[1], &target, vga_puts, vga_putc)) return;
        if (arp_resolve(target, mac) < 0) {
            vga_puts("No ARP reply (timeout)\n");
            return;
        }
        vga_puts(tokens[1]);
        vga_puts(" is at ");
        print_mac(mac, vga_putc);
        vga_putc('\n');
        return;
    }

    if (match(tokens[0], "ping") && count == 2) {
        uint32_t target;
        if (!resolve_target(tokens[1], &target, vga_puts, vga_putc)) return;
        icmp_send_echo_request(target, 1, 1);
        if (icmp_wait_echo_reply(500000) == 0) {
            vga_puts("Reply from ");
            vga_puts(tokens[1]);
            vga_putc('\n');
        } else {
            vga_puts("Request timed out\n");
        }
        return;
    }

    if (match(tokens[0], "udp") && count == 4) {
        uint32_t target;
        if (!resolve_target(tokens[1], &target, vga_puts, vga_putc)) return;
        uint16_t port = 0;
        for (int index = 0; tokens[2][index]; index++) port = (uint16_t)(port * 10 + (tokens[2][index] - '0'));
        if (udp_send(target, port, 5000, tokens[3], string_length(tokens[3])) < 0) {
            vga_puts("Failed to send UDP datagram\n");
        } else {
            vga_puts("UDP datagram sent\n");
        }
        return;
    }

    if (match(tokens[0], "get") && count == 5) {
        uint32_t target;
        if (!resolve_target(tokens[1], &target, vga_puts, vga_putc)) return;
        uint16_t port = 0;
        int output_fd;
        int bytes;

        for (int index = 0; tokens[2][index]; index++) port = (uint16_t)(port * 10 + (tokens[2][index] - '0'));

        output_fd = vfs_open(tokens[4], VFS_O_WRITE | VFS_O_CREATE);
        if (output_fd < 0) {
            vga_puts("Cannot create output file\n");
            return;
        }

        bytes = http_get_to_fd(target, port, tokens[3], tokens[1], output_fd);
        vfs_close(output_fd);

        if (bytes < 0) {
            vga_puts("HTTP GET failed (no TCP connection)\n");
            return;
        }

        vga_puts("Downloaded ");
        print_u32((uint32_t)bytes, vga_putc);
        vga_puts(" bytes to ");
        vga_puts(tokens[4]);
        vga_putc('\n');
        return;
    }

    if (match(tokens[0], "https") && count == 4) {
        uint32_t target;
        int output_fd;
        int bytes;
        if (!resolve_target(tokens[1], &target, vga_puts, vga_putc)) return;
        output_fd = vfs_open(tokens[3], VFS_O_WRITE | VFS_O_CREATE);
        if (output_fd < 0) {
            vga_puts("Cannot create output file\n");
            return;
        }
        bytes = https_get_to_fd(target, 443, tokens[2], tokens[1], output_fd);
        vfs_close(output_fd);
        if (bytes < 0) {
            vga_puts("HTTPS GET failed (TLS or certificate validation error)\n");
            return;
        }
        vga_puts("Downloaded ");
        print_u32((uint32_t)bytes, vga_putc);
        vga_puts(" bytes to ");
        vga_puts(tokens[3]);
        vga_putc('\n');
        return;
    }

    if (match(tokens[0], "fetch") && count == 4) {
        uint32_t target;
        uint16_t port = 0;
        char buffer[4096];
        int bytes;

        if (!resolve_target(tokens[1], &target, vga_puts, vga_putc)) return;
        for (int index = 0; tokens[2][index]; index++) port = (uint16_t)(port * 10 + (tokens[2][index] - '0'));

        bytes = fetch_response_body(target, port, tokens[1], tokens[3], buffer, sizeof(buffer));
        if (bytes < 0) {
            vga_puts("Fetch failed\n");
            return;
        }

        vga_puts("HTTP response:\n");
        print_http_body(buffer, vga_putc);
        vga_putc('\n');
        return;
    }

    if (match(tokens[0], "search") && count >= 2) {
        uint32_t target;
        char path[256];
        int length = 0;
        char buffer[4096];
        int bytes;

        if (!resolve_target("google.com", &target, vga_puts, vga_putc)) return;
        append_str(path, &length, "/search?q=");
        for (int i = 1; i < count; i++) {
            if (i > 1) append_str(path, &length, "+");
            for (int j = 0; tokens[i][j]; j++) {
                if (tokens[i][j] == ' ') path[length++] = '+';
                else path[length++] = tokens[i][j];
            }
        }
        path[length] = '\0';

        bytes = fetch_response_body(target, 80, "google.com", path, buffer, sizeof(buffer));
        if (bytes < 0) {
            vga_puts("Search request failed\n");
            return;
        }

        vga_puts("Google-like response:\n");
        print_http_body(buffer, vga_putc);
        vga_putc('\n');
        return;
    }

    if (tokens[0][0] == 's' && tokens[0][1] == 'e' && tokens[0][2] == 'n' && tokens[0][3] == 'd' && param[4] == ' ') {
        const char *payload = param + 5;
        uint8_t frame[1514];
        uint8_t mac[6];
        uint16_t payload_length = string_length(payload);
        uint16_t frame_length;

        if (payload_length > sizeof(frame) - 14) payload_length = sizeof(frame) - 14;
        for (int index = 0; index < 6; index++) frame[index] = 0xFF;
        rtl8139_get_mac(mac);
        for (int index = 0; index < 6; index++) frame[6 + index] = mac[index];
        frame[12] = 0x88;
        frame[13] = 0xB5; 
        for (uint16_t index = 0; index < payload_length; index++) frame[14 + index] = (uint8_t)payload[index];

        frame_length = (uint16_t)(14 + payload_length);
        if (frame_length < 60) frame_length = 60;

        if (rtl8139_send(frame, frame_length) < 0) vga_puts("Failed to send frame\n");
        else vga_puts("Raw test frame sent\n");
        return;
    }

    vga_puts("Usage: net [config <ip> <mask> <gw> | arp <host-or-ip> | ping <host-or-ip> | udp <host-or-ip> <port> <text> | get <host-or-ip> <port> <path> <file> | fetch <host> <port> <path> | search <term> ... | send <text>]\n");
}

