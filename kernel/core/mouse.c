#include <stdint.h>
#include "io.h"
#include "mouse.h"

static int packet_length = 3;
static uint8_t packet[4];
static int packet_index;
static int mouse_initialized;
#define MOUSE_QUEUE_SIZE 32
static mouse_event_t event_queue[MOUSE_QUEUE_SIZE];
static volatile uint8_t queue_read;
static volatile uint8_t queue_write;

static uint32_t interrupt_save(void) {
    uint32_t flags;
    asm volatile ("pushfl\n\tpopl %0" : "=r"(flags));
    asm volatile ("cli" : : : "memory");
    return flags;
}

static void interrupt_restore(uint32_t flags) {
    asm volatile ("pushl %0\n\tpopfl" : : "r"(flags) : "memory");
}

static void wait_controller(void) {
    for (uint32_t timeout = 0; timeout < 100000; timeout++) if (!(inb(0x64) & 2)) return;
}

static int read_aux(uint8_t *value) {
    if (!(inb(0x64) & 1) || !(inb(0x64) & 0x20)) return 0;
    *value = inb(0x60);
    return 1;
}

static int read_data(uint8_t *value) {
    for (uint32_t timeout = 0; timeout < 100000; timeout++) {
        uint8_t status = inb(0x64);
        if (status & 2) continue;
        if (!(status & 1)) continue;
        *value = inb(0x60);
        return 1;
    }
    return 0;
}

static int read_aux_atomic(uint8_t *value) {
    uint32_t flags = interrupt_save();
    int result = read_aux(value);
    interrupt_restore(flags);
    return result;
}

static int read_controller(uint8_t *value) {
    if (!(inb(0x64) & 1) || (inb(0x64) & 0x20)) return 0;
    *value = inb(0x60);
    return 1;
}

static void send_aux(uint8_t value) {
    wait_controller();
    outb(0x64, 0xD4);
    wait_controller();
    outb(0x60, value);
}

static int read_aux_response(uint8_t *value) {
    for (uint32_t timeout = 0; timeout < 100000; timeout++) if (read_aux(value)) return 1;
    return 0;
}

static void mouse_disable_irqs(void) {
    outb(0xa1, inb(0xa1) | 0x10);
    outb(0x21, inb(0x21) | 0x04);
}

static void mouse_enable_irqs(void) {
    outb(0xa1, inb(0xa1) & (uint8_t)~0x10);
    outb(0x21, inb(0x21) & (uint8_t)~0x04);
}

void mouse_init(void) {
    uint8_t config;
    uint8_t response;
    uint32_t flags;
    int device_id = 0;

    if (mouse_initialized) return;

    flags = interrupt_save();
    mouse_disable_irqs();

    /* Drain anything left in the controller buffers (stale mouse bytes, keyboard
       scancodes and leftovers of a previous init). */
    for (int drained = 0; drained < 128; drained++) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        (void)inb(0x60);
    }

    wait_controller();
    outb(0x64, 0xA8);

    wait_controller();
    outb(0x64, 0x20);
    if (!read_data(&config)) {
        mouse_enable_irqs();
        interrupt_restore(flags);
        return;
    }
    config |= 0x02;
    config &= (uint8_t)~0x20;
    config |= 0x01;
    wait_controller();
    outb(0x64, 0x60);
    wait_controller();
    outb(0x60, config);

    send_aux(0xFF);
    if (!read_aux_response(&response) || response != 0xFA) {
        mouse_enable_irqs();
        interrupt_restore(flags);
        return;
    }
    if (!read_aux_response(&response) || response != 0xAA) {
        mouse_enable_irqs();
        interrupt_restore(flags);
        return;
    }
    read_aux_response(&response);

    packet_length = 3;
    send_aux(0xF3); read_aux_response(&response);
    send_aux(200); read_aux_response(&response);
    send_aux(0xF3); read_aux_response(&response);
    send_aux(100); read_aux_response(&response);
    send_aux(0xF3); read_aux_response(&response);
    send_aux(80); read_aux_response(&response);

    send_aux(0xF2);
    if (read_aux_response(&response) && response == 0xFA && read_aux_response(&response))
        device_id = response;
    if (device_id == 3 || device_id == 4) packet_length = 4;

    send_aux(0xF4);
    read_aux_response(&response);

    /* Throw away any byte that arrived while the device was being configured. */
    for (int drained = 0; drained < 128; drained++) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        (void)inb(0x60);
    }

    packet_index = 0;
    queue_read = 0;
    queue_write = 0;

    mouse_enable_irqs();
    mouse_initialized = 1;
    interrupt_restore(flags);
}

static int mouse_event_available(void) {
    uint8_t status = inb(0x64);
    return (status & 1) && (status & 0x20);
}

void mouse_irq_handler(void) {
    uint8_t value;

    /* The PS/2 controller reports how many bytes are buffered, so honour it
       instead of stopping at the first empty status check. */
    for (int drained = 0; drained < 64; drained++) {
        if (!mouse_event_available()) break;
        if (!read_aux_atomic(&value)) break;
        if (packet_index == 0 && !(value & 0x08)) continue;
        packet[packet_index++] = value;
        if (packet_index < packet_length) continue;
        packet_index = 0;
        {
            uint8_t next_write = (uint8_t)((queue_write + 1) % MOUSE_QUEUE_SIZE);
            mouse_event_t *event;
            if (next_write == queue_read) continue;
            event = &event_queue[queue_write];
            event->buttons = packet[0] & 0x07;
            event->dx = (int8_t)packet[1];
            event->dy = (int8_t)packet[2];
            event->wheel = packet_length == 4 ? (int8_t)(packet[3] & 0x0F) : 0;
            if (event->wheel & 0x08) event->wheel -= 16;
            queue_write = next_write;
        }
    }
}

int mouse_poll(mouse_event_t *event) {
    uint32_t flags;
    if (event == 0) return 0;

    if (queue_read == queue_write) {
        /* This function is called from keyboard paths / syscalls as well, where
           the mouse IRQ might be masked, so pump the controller directly. */
        mouse_irq_handler();
    }
    if (queue_read == queue_write) return 0;

    flags = interrupt_save();
    if (queue_read == queue_write) {
        interrupt_restore(flags);
        return 0;
    }
    *event = event_queue[queue_read];
    queue_read = (uint8_t)((queue_read + 1) % MOUSE_QUEUE_SIZE);
    interrupt_restore(flags);
    return 1;
}