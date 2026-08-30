// sheet.c
#ifndef _SHEET_H_
#define _SHEET_H_

struct MEMMAN;

#define MAX_SHEETS  256
struct SHEET {
    unsigned char *buf;                                     // VRAM에 써질 시트 데이터 버퍼
    int bxsize, bysize, vx0, vy0, col_inv, height, flags;   // 시트 크기, 화면 위치, 투명색, 높이, 플래그
    struct SHTCTL *ctl;                                     // 시트 컨트롤러 포인터
    struct TASK *task;                                      // 시트와 관련된 태스크 포인터
};

struct SHTCTL {
    unsigned char *vram, *map;
    int xsize, ysize, top;
    struct SHEET *sheets[MAX_SHEETS]; // 디스플레이 순서대로 정렬된 시트 포인터 배열
    struct SHEET sheets0[MAX_SHEETS]; // 실제 시트 구조체 배열
};

struct SHTCTL *shtctl_init(struct MEMMAN *memman, unsigned char *vram, int xsize, int ysize);
struct SHEET *sheet_alloc(struct SHTCTL *ctl);
void sheet_setbuf(struct SHEET *sht, unsigned char *buf, int xsize, int ysize, int col_inv);
void sheet_updown(struct SHEET *sht, int height);
void sheet_refresh(struct SHEET *sht, int bx0, int by0, int bx1, int by1);
void sheet_slide(struct SHEET *sht, int vx0, int vy0);
void sheet_free(struct SHEET *sht);

#endif  // _SHEET_H_
