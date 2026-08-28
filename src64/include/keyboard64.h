#ifndef MOWKOW64_KEYBOARD64_H
#define MOWKOW64_KEYBOARD64_H

#include <fifo64.h>
#include <stdint.h>

void init_keyboard64(struct FIFO64 *fifo);
void inthandler21_64(void);

/* Shift/Ctrl은 물리 키 상태다. 콘솔마다 따로 두면 누른 채 포커스가 옮겨갔을
   때 한쪽에 눌림이 남으므로 키보드 계층이 하나만 들고 있는다.
   track이 1을 돌려주면 그 스캔코드는 수식 키라 더 볼 필요가 없다. */
int keyboard64_track_modifier(uint8_t scancode);
int keyboard64_shift(void);
int keyboard64_ctrl(void);

#endif
