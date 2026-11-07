#include "ustr.h"

size_t u_strlen(const char *text) {
    size_t length = 0;
    if (text) while (text[length]) length++;
    return length;
}

size_t u_strnlen(const char *text, size_t limit) {
    size_t length = 0;
    if (text) while (length < limit && text[length]) length++;
    return length;
}

void *u_memcpy(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t index = 0; index < size; index++) out[index] = in[index];
    return destination;
}

void *u_memset(void *destination, int value, size_t size) {
    unsigned char *out = destination;
    for (size_t index = 0; index < size; index++) out[index] = (unsigned char)value;
    return destination;
}

int u_strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { left++; right++; }
    return (unsigned char)*left - (unsigned char)*right;
}

int u_strncmp(const char *left, const char *right, size_t limit) {
    for (size_t index = 0; index < limit; index++) {
        if (left[index] != right[index]) return (unsigned char)left[index] - (unsigned char)right[index];
        if (!left[index]) return 0;
    }
    return 0;
}

char *u_strncpy(char *destination, const char *source, size_t limit) {
    size_t index = 0;
    if (!limit) return destination;
    if (source) {
        while (source[index] && index < limit - 1) {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
    return destination;
}

char *u_strchr(const char *text, int character) {
    if (!text) return 0;
    for (; *text; text++) {
        if (*text == (char)character) return (char *)text;
    }
    return 0;
}
