#ifndef PIUX_USER_STRING_H
#define PIUX_USER_STRING_H

#include <stddef.h>

size_t u_strlen(const char *text);
size_t u_strnlen(const char *text, size_t limit);
void *u_memcpy(void *destination, const void *source, size_t size);
void *u_memset(void *destination, int value, size_t size);
int u_strcmp(const char *left, const char *right);
int u_strncmp(const char *left, const char *right, size_t limit);
char *u_strncpy(char *destination, const char *source, size_t limit);
char *u_strchr(const char *text, int character);

#endif
