#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint32_t port, uint32_t val) {
    asm volatile ("outl %0, %w1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint32_t port) {
    uint32_t ret;
    asm volatile ("inl %w1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void memcpy_safe(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static inline void memset_safe(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = val;
    }
}

#endif
