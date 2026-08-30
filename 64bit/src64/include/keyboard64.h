#ifndef MOWKOW64_KEYBOARD64_H
#define MOWKOW64_KEYBOARD64_H

#include <fifo64.h>
#include <stdint.h>

/*
 * 0xe0 접두사가 붙은 확장 스캔코드는 이 비트를 세워서 구분한다.
 * 확장이 아닌 스캔코드는 값이 그대로라 기존 비교문이 그대로 동작한다.
 * 브레이크 코드의 0x80 비트도 하위 바이트에 그대로 남는다.
 */
#define KEY64_EXT 0x0100

#define KEY64_UP      (KEY64_EXT | 0x48)
#define KEY64_DOWN    (KEY64_EXT | 0x50)
#define KEY64_LEFT    (KEY64_EXT | 0x4b)
#define KEY64_RIGHT   (KEY64_EXT | 0x4d)
#define KEY64_HOME    (KEY64_EXT | 0x47)
#define KEY64_END     (KEY64_EXT | 0x4f)
#define KEY64_PGUP    (KEY64_EXT | 0x49)
#define KEY64_PGDN    (KEY64_EXT | 0x51)
#define KEY64_INSERT  (KEY64_EXT | 0x52)
#define KEY64_DELETE  (KEY64_EXT | 0x53)
#define KEY64_KPENTER (KEY64_EXT | 0x1c)
#define KEY64_RCTRL   (KEY64_EXT | 0x1d)
#define KEY64_RALT    (KEY64_EXT | 0x38)

void init_keyboard64(struct FIFO64 *fifo);
void inthandler21_64(void);

/*
 * i8042에서 온 바이트 하나를 키 코드로 조립한다.
 * 키가 완성되면 *out에 넣고 1, 접두사나 무시할 바이트면 0을 돌려준다.
 */
int keyboard64_decode(uint8_t byte, uint16_t *out);

/* Shift/Ctrl은 물리 키 상태다. 콘솔마다 따로 두면 누른 채 포커스가 옮겨갔을
   때 한쪽에 눌림이 남으므로 키보드 계층이 하나만 들고 있는다.
   track이 1을 돌려주면 그 스캔코드는 수식 키라 더 볼 필요가 없다. */
int keyboard64_track_modifier(uint16_t key);
int keyboard64_shift(void);
int keyboard64_ctrl(void);
int keyboard64_alt(void);

#endif
