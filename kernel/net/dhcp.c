#include <stdint.h>
#include "dhcp.h"
#include "eth.h"
#include "ip.h"
#include "net.h"
#include "udp.h"

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_REQUESTED_IP 50
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_END 255

static uint16_t dhcp_add_option(uint8_t *packet, uint16_t offset, uint8_t type, uint8_t length, const void *value) {
    packet[offset++] = type;
    packet[offset++] = length;
    for (uint8_t index = 0; index < length; index++) packet[offset++] = ((const uint8_t *)value)[index];
    return offset;
}

static int dhcp_read_options(const uint8_t *packet, uint16_t length, uint8_t wanted,
                             uint8_t *value, uint8_t max_length) {
    uint16_t offset = 240;
    while (offset < length) {
        uint8_t type = packet[offset++];
        uint8_t option_length;
        if (type == DHCP_OPTION_END) break;
        if (type == 0) continue;
        if (offset >= length) break;
        option_length = packet[offset++];
        if (offset + option_length > length) break;
        if (type == wanted) {
            if (option_length > max_length) option_length = max_length;
            for (uint8_t index = 0; index < option_length; index++) value[index] = packet[offset + index];
            return option_length;
        }
        offset = (uint16_t)(offset + option_length);
    }
    return 0;
}

static uint32_t dhcp_read_ip(const uint8_t *value) {
    return (uint32_t)value[0] << 24 | (uint32_t)value[1] << 16 | (uint32_t)value[2] << 8 | value[3];
}

int dhcp_configure(void) {
    uint8_t packet[300];
    uint8_t response[512];
    uint8_t value[16];
    uint8_t mac[6];
    uint32_t xid = 0x50495801U;
    uint32_t offered_ip;
    uint8_t server_id[4];
    uint32_t netmask = 0;
    uint32_t gateway = 0;
    uint32_t dns = 0;
    uint32_t source_ip;
    uint16_t source_port;
    uint16_t offset;
    int response_length = 0;

    eth_get_local_mac(mac);
    for (int index = 0; index < (int)sizeof(packet); index++) packet[index] = 0;
    packet[0] = 1; packet[1] = 1; packet[2] = 6; packet[3] = 0;
    packet[4] = (uint8_t)(xid >> 24); packet[5] = (uint8_t)(xid >> 16);
    packet[6] = (uint8_t)(xid >> 8); packet[7] = (uint8_t)xid;
    packet[10] = 0x80; packet[11] = 0;
    for (int index = 0; index < 6; index++) packet[28 + index] = mac[index];
    packet[236] = 0x63; packet[237] = 0x82; packet[238] = 0x53; packet[239] = 0x63;
    offset = 240;
    packet[offset++] = DHCP_OPTION_MESSAGE_TYPE; packet[offset++] = 1; packet[offset++] = DHCP_DISCOVER;
    packet[offset++] = DHCP_OPTION_END;
    if (udp_send(0xFFFFFFFFU, 67, 68, packet, offset) < 0) return -1;

    for (int attempt = 0; attempt < 200000; attempt++) {
        net_poll();
        response_length = udp_last_received(&source_ip, &source_port, response, sizeof(response));
        if (response_length >= 244 && source_port == 67 && response[0] == 2 &&
            ((uint32_t)response[4] << 24 | (uint32_t)response[5] << 16 | (uint32_t)response[6] << 8 | response[7]) == xid &&
            response[236] == 0x63 && response[237] == 0x82) break;
        response_length = 0;
    }
    if (response_length < 244) return -1;
    offered_ip = dhcp_read_ip(response + 16);
    if (dhcp_read_options(response, (uint16_t)response_length, 54, value, sizeof(value)) != 4) return -1;
    for (int index = 0; index < 4; index++) server_id[index] = value[index];
    if (dhcp_read_options(response, (uint16_t)response_length, 1, value, sizeof(value)) == 4) netmask = dhcp_read_ip(value);
    if (dhcp_read_options(response, (uint16_t)response_length, 3, value, sizeof(value)) >= 4) gateway = dhcp_read_ip(value);
    if (dhcp_read_options(response, (uint16_t)response_length, 6, value, sizeof(value)) >= 4) dns = dhcp_read_ip(value);

    for (int index = 0; index < (int)sizeof(packet); index++) packet[index] = 0;
    packet[0] = 1; packet[1] = 1; packet[2] = 6; packet[4] = (uint8_t)(xid >> 24); packet[5] = (uint8_t)(xid >> 16);
    packet[6] = (uint8_t)(xid >> 8); packet[7] = (uint8_t)xid; packet[10] = 0x80;
    for (int index = 0; index < 6; index++) packet[28 + index] = mac[index];
    packet[236] = 0x63; packet[237] = 0x82; packet[238] = 0x53; packet[239] = 0x63;
    offset = 240;
    packet[offset++] = DHCP_OPTION_MESSAGE_TYPE; packet[offset++] = 1; packet[offset++] = DHCP_REQUEST;
    offset = dhcp_add_option(packet, offset, DHCP_OPTION_REQUESTED_IP, 4, &response[16]);
    offset = dhcp_add_option(packet, offset, DHCP_OPTION_SERVER_ID, 4, server_id);
    packet[offset++] = DHCP_OPTION_END;
    if (udp_send(0xFFFFFFFFU, 67, 68, packet, offset) < 0) return -1;

    for (int attempt = 0; attempt < 200000; attempt++) {
        net_poll();
        response_length = udp_last_received(&source_ip, &source_port, response, sizeof(response));
        if (response_length >= 244 && source_port == 67 && response[0] == 2 &&
            ((uint32_t)response[4] << 24 | (uint32_t)response[5] << 16 | (uint32_t)response[6] << 8 | response[7]) == xid &&
            dhcp_read_options(response, (uint16_t)response_length, 53, value, sizeof(value)) == 1 && value[0] == DHCP_ACK) break;
        response_length = 0;
    }
    if (response_length < 244) return -1;
    ip_set_config(offered_ip, netmask, gateway);
    if (dns) ip_set_dns(dns);
    return 0;
}