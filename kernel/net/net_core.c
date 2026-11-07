#include <stdint.h>
#include "net.h"

#if !defined(PIUX_MODULE_NET)

uint16_t net_htons(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

uint32_t net_htonl(uint32_t value) {
    return ((value & 0x000000FFU) << 24) |
           ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) |
           ((value & 0xFF000000U) >> 24);
}

uint16_t net_checksum(const void *data, uint32_t length, uint32_t initial_sum) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = initial_sum;
    while (length > 1) {
        sum += (uint32_t)((bytes[0] << 8) | bytes[1]);
        bytes += 2;
        length -= 2;
    }
    if (length == 1) sum += (uint32_t)(bytes[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFU);
}

int (*net_resolve_host_dispatch)(const char *text, uint32_t *out_ip);
uint32_t (*net_parse_ip_dispatch)(const char *text);
void (*net_format_ip_dispatch)(uint32_t address, char *buffer);

uint32_t net_parse_ip(const char *text) {
    if (!net_parse_ip_dispatch) return 0;
    return net_parse_ip_dispatch(text);
}

int net_resolve_host(const char *text, uint32_t *out_ip) {
    if (!net_resolve_host_dispatch) return 0;
    return net_resolve_host_dispatch(text, out_ip);
}

void net_format_ip(uint32_t address, char *buffer) {
    if (net_format_ip_dispatch) net_format_ip_dispatch(address, buffer);
    else if (buffer) buffer[0] = '\0';
}

#endif
