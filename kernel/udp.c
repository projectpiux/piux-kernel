#include <stdint.h>
#include "udp.h"
#include "ip.h"
#include "net.h"

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

#define UDP_RECV_BUFFER_SIZE 512

static uint8_t last_buffer[UDP_RECV_BUFFER_SIZE];
static uint16_t last_length;
static uint32_t last_source_ip;
static uint16_t last_source_port;
static int last_valid;

int udp_send(uint32_t destination_ip, uint16_t destination_port, uint16_t source_port, const void *data, uint16_t length) {
    uint8_t packet[1024];
    udp_header_t *header = (udp_header_t *)packet;
    if (length > sizeof(packet) - sizeof(udp_header_t)) return -1;

    header->source_port = net_htons(source_port);
    header->destination_port = net_htons(destination_port);
    header->length = net_htons((uint16_t)(sizeof(udp_header_t) + length));
    header->checksum = 0; 

    for (uint16_t index = 0; index < length; index++) packet[sizeof(udp_header_t) + index] = ((const uint8_t *)data)[index];

    return ip_send(destination_ip, IP_PROTO_UDP, packet, (uint16_t)(sizeof(udp_header_t) + length));
}

void udp_handle_packet(const uint8_t *data, uint16_t length, uint32_t source_ip) {
    const udp_header_t *header;
    uint16_t payload_length;

    if (length < sizeof(udp_header_t)) return;
    header = (const udp_header_t *)data;
    payload_length = (uint16_t)(net_htons(header->length) - sizeof(udp_header_t));
    if (payload_length > UDP_RECV_BUFFER_SIZE) payload_length = UDP_RECV_BUFFER_SIZE;

    for (uint16_t index = 0; index < payload_length; index++) last_buffer[index] = data[sizeof(udp_header_t) + index];
    last_length = payload_length;
    last_source_ip = source_ip;
    last_source_port = net_htons(header->source_port);
    last_valid = 1;
}

int udp_last_received(uint32_t *source_ip, uint16_t *source_port, uint8_t *buffer, uint16_t max_length) {
    uint16_t copy_length;
    if (!last_valid) return -1;
    copy_length = last_length > max_length ? max_length : last_length;
    for (uint16_t index = 0; index < copy_length; index++) buffer[index] = last_buffer[index];
    *source_ip = last_source_ip;
    *source_port = last_source_port;
    last_valid = 0;
    return copy_length;
}
