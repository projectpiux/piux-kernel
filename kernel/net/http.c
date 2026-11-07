#include <stdint.h>
#include "http.h"
#include "tcp.h"
#include "../core/tls.h"
#include "../fs/vfs.h"

int http_get_to_fd(uint32_t ip, uint16_t port, const char *path, const char *host, int output_fd) {
    char request[256];
    int request_length = 0;
    const char *parts[5];
    uint8_t carry[4];
    int carry_length = 0;
    int header_done = 0;
    uint32_t total_bytes = 0;

    parts[0] = "GET ";
    parts[1] = path;
    parts[2] = " HTTP/1.0\r\nHost: ";
    parts[3] = host;
    parts[4] = "\r\nConnection: close\r\n\r\n";
    for (int part = 0; part < 5; part++) {
        for (int index = 0; parts[part][index] && request_length < (int)sizeof(request) - 1; index++) {
            request[request_length++] = parts[part][index];
        }
    }

    if (tcp_connect(ip, port) < 0) return -1;
    tcp_send(request, (uint16_t)request_length);

    for (;;) {
        uint8_t chunk[1460 + 4];
        int received;
        int chunk_length;

        tcp_wait_activity(300000);
        for (int index = 0; index < carry_length; index++) chunk[index] = carry[index];
        received = tcp_receive(chunk + carry_length, (uint16_t)(sizeof(chunk) - carry_length));
        chunk_length = carry_length + (received > 0 ? received : 0);
        carry_length = 0;

        if (chunk_length > 0) {
            if (!header_done) {
                int header_end = -1;
                for (int index = 0; index + 3 < chunk_length; index++) {
                    if (chunk[index] == '\r' && chunk[index + 1] == '\n' &&
                        chunk[index + 2] == '\r' && chunk[index + 3] == '\n') {
                        header_end = index + 4;
                        break;
                    }
                }
                if (header_end < 0) {
                    int keep = chunk_length < 3 ? chunk_length : 3;
                    for (int index = 0; index < keep; index++) carry[index] = chunk[chunk_length - keep + index];
                    carry_length = keep;
                } else {
                    int body_length = chunk_length - header_end;
                    header_done = 1;
                    if (body_length > 0) {
                        vfs_write(output_fd, chunk + header_end, (uint32_t)body_length);
                        total_bytes += (uint32_t)body_length;
                    }
                }
            } else {
                vfs_write(output_fd, chunk, (uint32_t)chunk_length);
                total_bytes += (uint32_t)chunk_length;
            }
        }

        if (tcp_is_closed() && received <= 0) break;
    }

    tcp_close();
    return (int)total_bytes;
}

int https_get_to_fd(uint32_t ip, uint16_t port, const char *path, const char *host, int output_fd) {
    char request[256];
    int request_length = 0;
    const char *parts[] = { "GET ", path, " HTTP/1.1\r\nHost: ", host, "\r\nConnection: close\r\n\r\n" };
    int header_done = 0;
    uint8_t carry[4];
    int carry_length = 0;
    uint32_t total_bytes = 0;

    for (int part = 0; part < 5; part++) for (int index = 0; parts[part][index] && request_length < (int)sizeof(request) - 1; index++) request[request_length++] = parts[part][index];
    if (tls_connect(ip, port, host) < 0) return -1000 - tls_last_error();
    if (tls_send(request, (uint16_t)request_length) < 0) return -2000 - tls_last_error();
    for (;;) {
        uint8_t chunk[1460 + 4];
        int received;
        int chunk_length;
        for (int index = 0; index < carry_length; index++) chunk[index] = carry[index];
        received = tls_receive(chunk + carry_length, (uint16_t)(sizeof(chunk) - carry_length));
        chunk_length = carry_length + (received > 0 ? received : 0);
        carry_length = 0;
        if (chunk_length > 0 && !header_done) {
            int end = -1;
            for (int index = 0; index + 3 < chunk_length; index++) if (chunk[index] == '\r' && chunk[index + 1] == '\n' && chunk[index + 2] == '\r' && chunk[index + 3] == '\n') { end = index + 4; break; }
            if (end < 0) { carry_length = chunk_length < 3 ? chunk_length : 3; for (int index = 0; index < carry_length; index++) carry[index] = chunk[chunk_length - carry_length + index]; }
            else { header_done = 1; if (chunk_length > end) { vfs_write(output_fd, chunk + end, (uint32_t)(chunk_length - end)); total_bytes += (uint32_t)(chunk_length - end); } }
        } else if (chunk_length > 0) { vfs_write(output_fd, chunk, (uint32_t)chunk_length); total_bytes += (uint32_t)chunk_length; }
        if (received < 0) return -3000 - tls_last_error();
        if (tls_is_closed() || received == 0) break;
    }
    tls_close();
    return (int)total_bytes;
}
