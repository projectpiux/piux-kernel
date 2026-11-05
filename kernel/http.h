#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

int http_get_to_fd(uint32_t ip, uint16_t port, const char *path, const char *host, int output_fd);
int https_get_to_fd(uint32_t ip, uint16_t port, const char *path, const char *host, int output_fd);

#endif
