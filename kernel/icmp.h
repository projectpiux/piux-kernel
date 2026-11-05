#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

void icmp_handle_packet(const uint8_t *data, uint16_t length, uint32_t source_ip);
int icmp_send_echo_request(uint32_t destination_ip, uint16_t identifier, uint16_t sequence);

int icmp_wait_echo_reply(int max_attempts);

#endif
