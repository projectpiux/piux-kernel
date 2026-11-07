#ifndef PIUX_AUTH_H
#define PIUX_AUTH_H

#include <stdint.h>

typedef void (*auth_clear_t)(void);
typedef void (*auth_puts_t)(const char *);
typedef void (*auth_putc_t)(char);

#define AUTH_USERNAME_SIZE 32

int auth_create_user(const char *username, const char *password, int sudoer);
int auth_login(auth_clear_t clear, auth_puts_t puts, auth_putc_t putc);
const char *auth_current_username(void);
uint32_t auth_current_uid(void);
int auth_current_user_is_sudoer(void);

#endif
