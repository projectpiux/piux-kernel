#ifndef KSYM_H
#define KSYM_H

#include <stdint.h>

#define KSYM_NAME_SIZE   48
#define KSYM_MAX_SYMBOLS 6144

void ksym_forget_owner(int owner);

#endif
