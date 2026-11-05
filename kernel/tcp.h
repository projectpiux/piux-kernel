#ifndef TCP_H
#define TCP_H

#include <stdint.h>

int tcp_connect(uint32_t ip, uint16_t port);
int tcp_send(const void *data, uint16_t length);

int tcp_receive(uint8_t *buffer, uint16_t max_length);
int tcp_is_closed(void);
void tcp_close(void);
void tcp_handle_packet(const uint8_t *data, uint16_t length, uint32_t source_ip);

int tcp_wait_activity(int max_attempts);

#endif
