#ifndef PIUX_INSTALLER_H
#define PIUX_INSTALLER_H

#include <stdint.h>

typedef void (*installer_puts_t)(const char *);
typedef void (*installer_putc_t)(char);
typedef void (*installer_clear_t)(void);
typedef void (*installer_power_off_t)(void);
typedef void (*installer_reboot_t)(void);

int installer_run(installer_clear_t clear,
                  installer_puts_t puts,
                  installer_putc_t putc,
                  installer_power_off_t power_off,
                  installer_reboot_t reboot);

#endif
