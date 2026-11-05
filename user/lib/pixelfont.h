#ifndef PIUX_PIXELFONT_H
#define PIUX_PIXELFONT_H

#include <stdint.h>

#include "api.h"

/* Bitmap font 6x13 condiviso da desktop e app. Le dimensioni (PIXFONT_WIDTH/
   PIXFONT_HEIGHT) sono definite in api.h per evitare doppe definizioni. */

int pixfont_pixel(char character, int x, int y);

#endif
