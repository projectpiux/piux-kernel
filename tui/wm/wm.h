// this is the main .h header for the window manager of Piux

#ifndef PIUX_WM_H
#define PIUX_WM_H

#include <stdint.h>

typedef void (*pwm_clear_t)(void);
typedef void (*pwm_puts_t)(const char *);
typedef void (*pwm_putc_t)(char);

int pwm_run(pwm_clear_t clear, pwm_puts_t puts, pwm_putc_t putc);
int pwm_command_read_char(void);
int pwm_command_clear(void);
int pwm_command_is_active(void);
int pwm_command_reload(void);
int pwm_command_stop(void);
int pwm_command_refresh(void);

#endif
