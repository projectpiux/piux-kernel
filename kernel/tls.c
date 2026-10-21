#include <stdint.h>
#include "tls.h"
#include "tcp.h"

static int closed;

int tls_connect(uint32_t ip, uint16_t port, const char *server_name) {
    (void)ip;
    (void)port;
    (void)server_name;
    closed = 1;
    return -1;
}

int tls_send(const void *data, uint16_t length) {
    (void)data;
    (void)length;
    return -1;
}

int tls_receive(uint8_t *buffer, uint16_t max_length) {
    (void)buffer;
    (void)max_length;
    return -1;
}

int tls_is_closed(void) { return closed; }

int tls_last_error(void) { return 1; }

void tls_close(void) {
    tcp_close();
    closed = 1;
}