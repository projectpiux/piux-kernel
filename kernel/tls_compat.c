#include <stddef.h>
#include <stdint.h>
#include "io.h"

void *memcpy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t index = 0; index < length; index++) out[index] = in[index];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out < in) return memcpy(destination, source, length);
    for (size_t index = length; index > 0; index--) out[index - 1] = in[index - 1];
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    for (size_t index = 0; index < length; index++) out[index] = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t index = 0; index < length; index++) if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    return 0;
}

static uint8_t rtc_read(uint8_t register_index) {
    outb(0x70, register_index);
    return inb(0x71);
}

static uint8_t rtc_bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

static uint32_t rtc_days_before_year(uint32_t year) {
    uint32_t days = 0;
    for (uint32_t current = 1970; current < year; current++) {
        days += (current % 4 == 0 && (current % 100 != 0 || current % 400 == 0)) ? 366 : 365;
    }
    return days;
}

static long rtc_unix_time(void) {
    static const uint16_t days_before_month[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    uint8_t status;
    uint32_t full_year;

    for (;;) {
        while (rtc_read(0x0A) & 0x80) { }
        second = rtc_read(0x00);
        minute = rtc_read(0x02);
        hour = rtc_read(0x04);
        day = rtc_read(0x07);
        month = rtc_read(0x08);
        year = rtc_read(0x09);
        century = rtc_read(0x32);
        status = rtc_read(0x0B);
        if (second == rtc_read(0x00)) break;
    }

    if (!(status & 0x04)) {
        second = rtc_bcd_to_binary(second);
        minute = rtc_bcd_to_binary(minute);
        hour = rtc_bcd_to_binary(hour);
        day = rtc_bcd_to_binary(day);
        month = rtc_bcd_to_binary(month);
        year = rtc_bcd_to_binary(year);
        century = rtc_bcd_to_binary(century);
    }
    if (century < 19 || century > 21) century = 20;
    full_year = (uint32_t)century * 100U + year;
    if (full_year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
    return (long)((rtc_days_before_year(full_year) + days_before_month[month - 1] +
                   (month > 2 && (full_year % 4 == 0 && (full_year % 100 != 0 || full_year % 400 == 0))) * 1U +
                   day - 1) * 86400U + (uint32_t)hour * 3600U + (uint32_t)minute * 60U + second);
}

long time(long *result) {
    long now = rtc_unix_time();
    if (result != NULL) *result = now;
    return now;
}

void __stack_chk_fail(void) {
    for (;;) __asm__ __volatile__("cli; hlt");
}
