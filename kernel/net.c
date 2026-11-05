#include <stddef.h>
#include <stdint.h>

#if defined(PIUX_MODULE_NET)

#include "net.h"
#include "ip.h"
#include "udp.h"
#include "eth.h"

#if defined(PIUX_MODULE_NET)
#include "piux_module.h"
#include "rtl8139.h"
#include "dhcp.h"

extern int (*net_resolve_host_dispatch)(const char *text, uint32_t *out_ip);
extern uint32_t (*net_parse_ip_dispatch)(const char *text);
extern void (*net_format_ip_dispatch)(uint32_t address, char *buffer);
#endif

uint32_t net_parse_ip(const char *text) {
    uint32_t result = 0;
    uint32_t part = 0;
    int shift = 24;
    for (int index = 0; ; index++) {
        char character = text[index];
        if (character >= '0' && character <= '9') {
            part = part * 10 + (uint32_t)(character - '0');
        } else if (character == '.' || character == '\0') {
            result |= (part & 0xFFU) << shift;
            shift -= 8;
            part = 0;
            if (character == '\0') break;
        } else {
            break;
        }
    }
    return result;
}

static int net_dns_query(const char *name, uint32_t *out_ip) {
    uint8_t packet[256];
    uint8_t response[512];
    uint16_t identifier = 0x5049;
    uint16_t offset = 12;
    uint16_t label_start = offset;
    uint16_t label_length = 0;
    uint32_t source_ip;
    uint16_t source_port;
    int response_length;

    for (int index = 0; ; index++) {
        char character = name[index];
        if (character == '.' || character == '\0') {
            if (label_length == 0 || label_length > 63 || (uint32_t)offset + label_length + 1 >= sizeof(packet)) return 0;
            packet[label_start] = (uint8_t)label_length;
            offset = (uint16_t)(offset + label_length + 1);
            label_start = offset;
            label_length = 0;
            if (character == '\0') break;
        } else {
            if (label_length == 0) offset++;
            packet[offset + label_length] = (uint8_t)character;
            label_length++;
        }
    }
    packet[offset++] = 0;
    if ((uint32_t)offset + 4 > sizeof(packet)) return 0;

    packet[0] = (uint8_t)(identifier >> 8); packet[1] = (uint8_t)identifier;
    packet[2] = 1; packet[3] = 0;
    packet[4] = 0; packet[5] = 1; packet[6] = 0; packet[7] = 0;
    packet[8] = 0; packet[9] = 0; packet[10] = 0; packet[11] = 0;
    packet[offset++] = 0; packet[offset++] = 1;
    packet[offset++] = 0; packet[offset++] = 1;

    if (udp_send(ip_get_dns(), 53, 49153, packet, offset) < 0) return 0;
    response_length = 0;
    for (int attempt = 0; attempt < 200000; attempt++) {
        net_poll();
        response_length = udp_last_received(&source_ip, &source_port, response, sizeof(response));
        if (response_length >= 12 && source_ip == ip_get_dns() && source_port == 53 &&
            ((uint16_t)response[0] << 8 | response[1]) == identifier &&
            (response[2] & 0x80) && !(response[3] & 0x0F)) break;
        response_length = 0;
    }
    if (response_length < 12) return 0;

    offset = 12;
    while (offset < (uint16_t)response_length && response[offset] != 0) {
        if ((response[offset] & 0xC0) == 0xC0) { offset += 2; break; }
        offset = (uint16_t)(offset + response[offset] + 1);
    }
    if (offset + 4 > (uint16_t)response_length) return 0;
    offset += 4;
    for (uint16_t answer = 0; answer < ((uint16_t)response[6] << 8 | response[7]); answer++) {
        uint16_t type;
        uint16_t data_length;
        if (offset >= (uint16_t)response_length) return 0;
        if ((response[offset] & 0xC0) == 0xC0) offset += 2;
        else while (offset < (uint16_t)response_length && response[offset] != 0) offset = (uint16_t)(offset + response[offset] + 1);
        if (offset + 10 > (uint16_t)response_length) return 0;
        type = (uint16_t)(response[offset] << 8 | response[offset + 1]);
        data_length = (uint16_t)(response[offset + 8] << 8 | response[offset + 9]);
        offset += 10;
        if (type == 1 && data_length == 4 && offset + 4 <= (uint16_t)response_length) {
            *out_ip = (uint32_t)response[offset] << 24 | (uint32_t)response[offset + 1] << 16 |
                      (uint32_t)response[offset + 2] << 8 | response[offset + 3];
            return 1;
        }
        offset = (uint16_t)(offset + data_length);
    }
    return 0;
}

int net_resolve_host(const char *text, uint32_t *out_ip) {
    int has_letters = 0;

    if (text == NULL || out_ip == NULL) return 0;

    for (int index = 0; text[index]; index++) {
        unsigned char c = (unsigned char)text[index];
        if ((c >= '0' && c <= '9') || c == '.') continue;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            has_letters = 1;
            continue;
        }
        return 0;
    }

    if (!has_letters) {
        *out_ip = net_parse_ip(text);
        return 1;
    }

    return net_dns_query(text, out_ip);
}

void net_format_ip(uint32_t address, char *buffer) {
    int offset = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint32_t octet = (address >> shift) & 0xFFU;
        if (octet >= 100) buffer[offset++] = (char)('0' + octet / 100);
        if (octet >= 10) buffer[offset++] = (char)('0' + (octet / 10) % 10);
        buffer[offset++] = (char)('0' + octet % 10);
        if (shift > 0) buffer[offset++] = '.';
    }
    buffer[offset] = '\0';
}

#if defined(PIUX_MODULE_NET)

int net_module_init(void) {
    if (rtl8139_init() == 0) dhcp_configure();
    return 0;
}

uint32_t (*net_parse_ip_dispatch)(const char *text) = net_parse_ip;
int (*net_resolve_host_dispatch)(const char *text, uint32_t *out_ip) = net_resolve_host;
void (*net_format_ip_dispatch)(uint32_t address, char *buffer) = net_format_ip;

static const char *const net_module_names[] = {
    "net_parse_ip_dispatch", "net_resolve_host_dispatch", "net_format_ip_dispatch",
    "dhcp_configure"
};

static const void *const net_module_addresses[] = {
    PIUX_EXPORT(&net_parse_ip_dispatch), PIUX_EXPORT(&net_resolve_host_dispatch),
    PIUX_EXPORT(&net_format_ip_dispatch), PIUX_EXPORT(&dhcp_configure)
};

void piux_module_exports(const char *const **names, const void *const **addresses,
                         uint32_t *count) {
    *names = net_module_names;
    *addresses = net_module_addresses;
    *count = sizeof(net_module_names) / sizeof(net_module_names[0]);
}

static const char *const net_module_deps[] = { };

PIUX_MODULE(
    .name = "net",
    .version = PIUX_MODULE_VERSION,
    .deps = 0,
    .dep_count = 0,
    .init = net_module_init,
    .description = "ARP/IPv4/ICMP/UDP/TCP/DHCP protocol stack"
);

#endif

#endif
