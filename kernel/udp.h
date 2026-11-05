#ifndef UDP_H
#define UDP_H

#include <stdint.h>

int udp_send(uint32_t destination_ip, uint16_t destination_port, uint16_t source_port, const void *data, uint16_t length);
void udp_handle_packet(const uint8_t *data, uint16_t length, uint32_t source_ip);

int udp_last_received(uint32_t *source_ip, uint16_t *source_port, uint8_t *buffer, uint16_t max_length);

#endif
