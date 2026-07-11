#ifndef MOWKOW64_KEYBOARD64_H
#define MOWKOW64_KEYBOARD64_H

#include <fifo64.h>

void init_keyboard64(struct FIFO64 *fifo);
void inthandler21_64(void);

#endif
