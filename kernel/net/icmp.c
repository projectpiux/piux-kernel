#include <stdint.h>
#include "icmp.h"
#include "ip.h"
#include "net.h"
#include "eth.h"

#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_ECHO_PACKET_SIZE 64

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} icmp_header_t;

static int reply_pending;
static uint16_t reply_identifier;
static uint16_t reply_sequence;
static int reply_received;

int icmp_send_echo_request(uint32_t destination_ip, uint16_t identifier, uint16_t sequence) {
    uint8_t packet[ICMP_ECHO_PACKET_SIZE];
    icmp_header_t *header = (icmp_header_t *)packet;
    uint16_t payload_length = sizeof(packet) - sizeof(icmp_header_t);

    header->type = ICMP_TYPE_ECHO_REQUEST;
    header->code = 0;
    header->checksum = 0;
    header->identifier = net_htons(identifier);
    header->sequence = net_htons(sequence);
    for (uint16_t index = 0; index < payload_length; index++) packet[sizeof(icmp_header_t) + index] = (uint8_t)('a' + (index % 26));
    header->checksum = net_htons(net_checksum(packet, sizeof(packet), 0));

    reply_pending = 1;
    reply_identifier = identifier;
    reply_sequence = sequence;
    reply_received = 0;

    return ip_send(destination_ip, IP_PROTO_ICMP, packet, sizeof(packet));
}

int icmp_wait_echo_reply(int max_attempts) {
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        net_poll();
        if (reply_received) return 0;
    }
    return -1;
}

void icmp_handle_packet(const uint8_t *data, uint16_t length, uint32_t source_ip) {
    const icmp_header_t *header;

    if (length < sizeof(icmp_header_t)) return;
    header = (const icmp_header_t *)data;

    if (header->type == ICMP_TYPE_ECHO_REPLY) {
        if (reply_pending && net_htons(header->identifier) == reply_identifier &&
            net_htons(header->sequence) == reply_sequence) {
            reply_received = 1;
        }
        return;
    }

    if (header->type == ICMP_TYPE_ECHO_REQUEST) {
        uint8_t packet[74];
        icmp_header_t *reply = (icmp_header_t *)packet;
        uint16_t copy_length = (uint16_t)(length - sizeof(icmp_header_t));
        if (copy_length > sizeof(packet) - sizeof(icmp_header_t)) copy_length = (uint16_t)(sizeof(packet) - sizeof(icmp_header_t));

        reply->type = ICMP_TYPE_ECHO_REPLY;
        reply->code = 0;
        reply->checksum = 0;
        reply->identifier = header->identifier;
        reply->sequence = header->sequence;
        for (uint16_t index = 0; index < copy_length; index++) packet[sizeof(icmp_header_t) + index] = data[sizeof(icmp_header_t) + index];
        reply->checksum = net_htons(net_checksum(packet, (uint32_t)(sizeof(icmp_header_t) + copy_length), 0));

        ip_send(source_ip, IP_PROTO_ICMP, packet, (uint16_t)(sizeof(icmp_header_t) + copy_length));
    }
}
