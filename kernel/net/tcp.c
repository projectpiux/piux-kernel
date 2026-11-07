#include <stdint.h>
#include "tcp.h"
#include "ip.h"
#include "net.h"

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_STATE_CLOSED 0
#define TCP_STATE_SYN_SENT 1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_CLOSING 3

#define TCP_RECV_BUFFER_SIZE 1460
#define TCP_MAX_SEGMENT 1400
#define TCP_CONNECT_ATTEMPTS 300000

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence_number;
    uint32_t ack_number;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} tcp_header_t;

static int state;
static uint32_t remote_ip;
static uint16_t remote_port;
static uint16_t local_port = 49152;
static uint32_t send_next;
static uint32_t recv_next;
static int peer_closed;

static uint8_t recv_buffer[TCP_RECV_BUFFER_SIZE];
static uint16_t recv_length;

static int tcp_send_segment(uint8_t flags, const void *data, uint16_t length) {
    uint8_t packet[sizeof(tcp_header_t) + TCP_MAX_SEGMENT];
    tcp_header_t *header = (tcp_header_t *)packet;
    uint32_t pseudo_sum;
    uint16_t segment_length = (uint16_t)(sizeof(tcp_header_t) + length);
    uint32_t local_ip = ip_get_local_ip();

    if (length > TCP_MAX_SEGMENT) return -1;

    header->source_port = net_htons(local_port);
    header->destination_port = net_htons(remote_port);
    header->sequence_number = net_htonl(send_next);
    header->ack_number = net_htonl(recv_next);
    header->data_offset = (uint8_t)((sizeof(tcp_header_t) / 4) << 4);
    header->flags = flags;
    header->window_size = net_htons(4096);
    header->checksum = 0;
    header->urgent_pointer = 0;

    for (uint16_t index = 0; index < length; index++) packet[sizeof(tcp_header_t) + index] = ((const uint8_t *)data)[index];

    pseudo_sum = ((local_ip >> 16) & 0xFFFFU) + (local_ip & 0xFFFFU) +
                 ((remote_ip >> 16) & 0xFFFFU) + (remote_ip & 0xFFFFU) +
                 (uint32_t)IP_PROTO_TCP + (uint32_t)segment_length;
    header->checksum = net_htons(net_checksum(packet, segment_length, pseudo_sum));

    return ip_send(remote_ip, IP_PROTO_TCP, packet, segment_length);
}

int tcp_connect(uint32_t ip, uint16_t port) {
    remote_ip = ip;
    remote_port = port;
    local_port++;
    send_next = 0x1000;
    recv_next = 0;
    peer_closed = 0;
    recv_length = 0;
    state = TCP_STATE_SYN_SENT;

    tcp_send_segment(TCP_FLAG_SYN, 0, 0);

    for (int attempt = 0; attempt < TCP_CONNECT_ATTEMPTS; attempt++) {
        tcp_wait_activity(1);
        if (state == TCP_STATE_ESTABLISHED) return 0;
    }
    state = TCP_STATE_CLOSED;
    return -1;
}

int tcp_send(const void *data, uint16_t length) {
    int result;
    if (state != TCP_STATE_ESTABLISHED) return -1;
    result = tcp_send_segment((uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH), data, length);
    if (result == 0) send_next += length;
    return result;
}

int tcp_receive(uint8_t *buffer, uint16_t max_length) {
    uint16_t copy_length;
    if (recv_length == 0) return 0;
    copy_length = recv_length > max_length ? max_length : recv_length;
    for (uint16_t index = 0; index < copy_length; index++) buffer[index] = recv_buffer[index];
    recv_length = 0;
    return copy_length;
}

int tcp_is_closed(void) {
    return peer_closed || state == TCP_STATE_CLOSED;
}

void tcp_close(void) {
    if (state == TCP_STATE_ESTABLISHED) {
        tcp_send_segment((uint8_t)(TCP_FLAG_FIN | TCP_FLAG_ACK), 0, 0);
        send_next++;
        state = TCP_STATE_CLOSING;
    }
}

int tcp_wait_activity(int max_attempts) {
    extern void net_poll(void);
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        net_poll();
        if (recv_length > 0 || tcp_is_closed()) return 0;
    }
    return -1;
}

void tcp_handle_packet(const uint8_t *data, uint16_t length, uint32_t source_ip) {
    const tcp_header_t *header;
    uint16_t header_length;
    uint16_t payload_length;
    const uint8_t *payload;

    if (length < sizeof(tcp_header_t)) return;
    header = (const tcp_header_t *)data;
    if (source_ip != remote_ip || net_htons(header->source_port) != remote_port ||
        net_htons(header->destination_port) != local_port) return;

    header_length = (uint16_t)(((header->data_offset >> 4) & 0x0F) * 4);
    if (header_length < sizeof(tcp_header_t) || header_length > length) return;
    payload = data + header_length;
    payload_length = (uint16_t)(length - header_length);

    if (state == TCP_STATE_SYN_SENT) {
        if ((header->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            recv_next = net_htonl(header->sequence_number) + 1;
            send_next++;
            state = TCP_STATE_ESTABLISHED;
            tcp_send_segment(TCP_FLAG_ACK, 0, 0);
        }
        return;
    }

    if (state == TCP_STATE_ESTABLISHED || state == TCP_STATE_CLOSING) {
        if (header->flags & TCP_FLAG_RST) {
            state = TCP_STATE_CLOSED;
            peer_closed = 1;
            return;
        }
        if (payload_length > 0 && net_htonl(header->sequence_number) == recv_next) {
            uint16_t copy_length = payload_length > sizeof(recv_buffer) ? sizeof(recv_buffer) : payload_length;
            for (uint16_t index = 0; index < copy_length; index++) recv_buffer[index] = payload[index];
            recv_length = copy_length;
            recv_next += payload_length;
            tcp_send_segment(TCP_FLAG_ACK, 0, 0);
        }
        if (header->flags & TCP_FLAG_FIN) {
            recv_next++;
            tcp_send_segment(TCP_FLAG_ACK, 0, 0);
            peer_closed = 1;
            state = TCP_STATE_CLOSED;
        }
    }
}
