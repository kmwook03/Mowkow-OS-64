#ifndef _INPUT_H_
#define _INPUT_H_

struct FIFO32;

// keyboard.c
void inthandler21(int *esp);
void wait_KBC_sendready(void);
void init_keyboard(struct FIFO32 *fifo, int data0);

#define PORT_KEYDAT    0x0060
#define PORT_KEYCMD    0x0064

// mouse.c
struct MOUSE_DEC {
    unsigned char buf[3], phase;
    int x, y, btn;
};

void inthandler2c(int *esp);
void enable_mouse(struct FIFO32 *fifo, int data0, struct MOUSE_DEC *mdec);
int mouse_decode(struct MOUSE_DEC *mdec, unsigned char dat);

#endif  // _INPUT_H_
