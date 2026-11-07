#ifndef TLS_H
#define TLS_H

#include <stdint.h>

int tls_connect(uint32_t ip, uint16_t port, const char *server_name);
int tls_send(const void *data, uint16_t length);
int tls_receive(uint8_t *buffer, uint16_t max_length);
int tls_is_closed(void);
int tls_last_error(void);
void tls_close(void);

#endif