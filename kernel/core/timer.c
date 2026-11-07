#include <stdint.h>
#include "timer.h"
#include "io.h"
#include "process.h"

static volatile uint32_t ticks;

static void pic_wait(void) { outb(0x80, 0); }

void timer_init(uint32_t frequency) {
    uint32_t divisor;
    asm volatile ("cli");
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
    if (frequency == 0) frequency = 100;
    divisor = 1193180U / frequency;
    if (divisor == 0 || divisor > 0xffff) divisor = 0xffff;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xff);
    outb(0x40, divisor >> 8);

    outb(0x20, 0x11); pic_wait();
    outb(0xa0, 0x11); pic_wait();
    outb(0x21, 0x20); pic_wait();
    outb(0xa1, 0x28); pic_wait();
    outb(0x21, 0x04); pic_wait();
    outb(0xa1, 0x02); pic_wait();
    outb(0x21, 0x01); pic_wait();
    outb(0xa1, 0x01); pic_wait();
    outb(0x21, 0xfe);
    outb(0xa1, 0xff);
    asm volatile ("sti");
}

uint32_t timer_ticks(void) { return ticks; }
void timer_tick(void) {
    ticks++;

    if (ticks % 2 == 0) process_idle_yield();
}