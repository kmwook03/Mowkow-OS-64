/**
 * @file bootpack.c
 * 
 * @brief OS 시스템 설정 초기화 및 메인 루프(입출력 처리) 실행
 * 
 * - 시스템 초기화: GDT/IDT, PIC, PIT, FDC, 키보드, 마우스, 메모리 관리자, 그래픽 디스플레이, 시스템 폰트 불러오기
 */

#include "../include/bootpack.h"
#include "../include/utf8.h"
#include "../include/fd.h"
#include <stdio.h>

#define KEYCMD_LED		0xed

void keywin_off(struct SHEET *key_win);
void keywin_on(struct SHEET *key_win);
void close_constask(struct TASK *task);
void close_console(struct SHEET *sht);

/**
 * @brief OS 메인 함수
 * 
 * 시스템 초기화 및 메인 루프 실행
 * 
 * 키보드 및 마우스 이벤트 처리
 * 
 * @return: void
 */
void HariMain(void)
{
	// Declare Structures
	struct BOOTINFO *binfo = (struct BOOTINFO *) ADR_BOOTINFO;  		// 부트로더로부터 부트 정보 (0x0ff0 - 0x0fff)
	struct SHTCTL *shtctl; 												// 시트 컨트롤
	struct FIFO32 fifo;                                         		// 태스크 간 통신용 FIFO 버퍼
	struct FIFO32 keycmd;								 				// 키보드 명령용 FIFO 버퍼
	struct MOUSE_DEC mdec;												// 마우스 디코딩 데이터
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;				// 메모리 관리자
	struct SHEET *sht_back, *sht_mouse;									// sheets: background, mouse
	struct TASK *task_a, *task;											// tasks: task_a, task
	struct CONSOLE *cons;												// console
	struct SHEET *sht = 0, *key_win, *sht2;								// window sheet

	char s[40];															
	int fifobuf[128], keycmd_buf[32];									// FIFO 버퍼
	unsigned char *buf_back, buf_mouse[256];							// 시트용 버퍼: 배경, 마우스, 콘솔

	int mx, my, i, new_mx = -1, new_my = 0, new_wx = 0x7fffffff, new_wy = 0; // 마우스와 커서용 변수
	unsigned int memtotal;												// 총 메모리 크기
	int j, x, y, mmx = -1, mmy = -1, mmx2 = 0;							// 마우스에 의한 윈도우 이동 모드용 변수

	int key_shift = 0, key_ctrl = 0, key_leds = (binfo->leds >> 4) & 7, keycmd_wait = -1;	// 키보드 상태 변수
	static char keytable0[0x80] = {
		0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0x7F, 0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0x0a, 0x1D, 'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`',   0,   '\\', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', 8, '9', '-', 4, '5', 6, '+', '1',
		2, '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0x5c, 0,  0,   0,   0,   0,   0,   0,   0,   0,   0x5c, 0,  0
	};
	static char keytable1[0x80] = {
		0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0x7F, 0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0x0a, 0x1D, 'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,   '|', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   '_', 0,   0,   0,   0,   0,   0,   0,   0,   0,   '|', 0,   0
	};

	// 시스템 초기화
	init_gdtidt();  														// GDT/IDT 초기화
	init_pic(); 															// PIC 초기화
	io_sti(); 															    // CPU interrupts (set IF flag) 허용
	fifo32_init(&fifo, 128, fifobuf, 0); 									// FIFO buffer 초기화
	*((int *) 0x0fec) = (int) &fifo;										// FIFO 주소 (0x0fec)
	init_pit(); 															// PIT 초기화
	io_out8(PIC0_IMR, io_in8(PIC0_IMR) & 0xbf);
	init_fdc(); 															// FDC 초기화

	init_keyboard(&fifo, 256); 											    // 키보드 초기화
	enable_mouse(&fifo, 512, &mdec); 										// 마우스 활성화

	// 주의: 추가적인 인터럽트가 필요할 경우, 하단의 io_out8(PIC0_IMR, ...) 및 io_out8(PIC1_IMR, ...) 부분도 수정해야 함
	io_out8(PIC0_IMR, 0xb8); 												// PIT, PIC1, 키보드, FDC 허용(10111000)
	io_out8(PIC1_IMR, 0xef); 												// 마우스 허용(11101111)

	fifo32_init(&keycmd, 32, keycmd_buf, 0); 								// 키보드 명령 FIFO 버퍼

    // 메모리 관리자 초기화
	memtotal = memtest(0x00400000, 0xbfffffff);								// 메모리 크기 테스트
	memman_init(memman);													// 메모리 관리자 초기화
	memman_free(memman, 0x00001000, 0x0009e000); 							// free1: 0x00001000 - 0x0009efff 
	memman_free(memman, 0x00400000, memtotal - 0x00400000);					// free2: 0x00400000 - (memtotal - 0x00400000)

    // 그래픽 디스플레이 초기화
	init_palette();															// 색상 팔레트 초기화
	shtctl = shtctl_init(memman, binfo->vram, binfo->scrnx, binfo->scrny);  // 시트 컨트롤 초기화
	task_a = task_init(memman);												// 메인 태스크 초기화
	fifo.task = task_a;														// FIFO 버퍼에 메인 태스크 설정
    task_run(task_a, 1, 2);													// 메인 태스크 실행
	*((int *) 0x0fe4) = (int) shtctl;										// shtctl 주소 저장 (0x0fe4)
	task_a->langmode = 1;													// 메인 태스크는 영어 모드로 시작

    // 배경 시트 그리기
	sht_back  = sheet_alloc(shtctl);														// 배경 시트 할당
    buf_back  = (unsigned char *) memman_alloc_4k(memman, binfo->scrnx * binfo->scrny);		// 배경 버퍼 할당
    sheet_setbuf(sht_back, buf_back, binfo->scrnx, binfo->scrny, -1);						// 배경 시트 버퍼 설정
    init_screen8(buf_back, binfo->scrnx, binfo->scrny);                                     // 배경 화면 초기화

	// 폰트 설정
	unsigned char *korean;
	FDHANDLE fh;
	int k_size = 0;
	int k;

	if (fd_open(&fh, "H04.FNT")) {	// 한글 폰트 있는지 확인
		k_size = fh.finfo->size;
		fd_close(&fh);
	}

	korean = (unsigned char *) memman_alloc_4k(memman, 4096 + k_size);

	if (fd_open(&fh, "E2.FNT")) {	// 영문 폰트가 있으면 불러오기
		fd_read(&fh, korean, fh.finfo->size);
		fd_close(&fh);
	} else {
		extern char hankaku[4096];	// 없으면 기본 영문 폰트 사용
		for (k=0; k<4096; k++) {
			korean[k] = hankaku[k];
		}
	}

	if (k_size > 0 && fd_open(&fh, "H04.FNT")) {	// 한글 폰트가 있으면 불러오기
		fd_read(&fh, korean + 4096, k_size);
		fd_close(&fh);
	} else {										// 없으면 한글 폰트 영역을 0으로 초기화
		for (k=0; k<k_size; k++) {
			*(korean + 4096 + k) = 0;
		}
	}

	system_font = korean;
	*((int *) 0x0fe8) = (int) (korean + 4096);												// 한글 폰트 주소 저장 (0x0fe8)

    // 콘솔 시트 그리기
	key_win = open_console(shtctl, memtotal, 1);												// 첫 번째 콘솔 창 열기

    // 마우스 시트 그리기
	sht_mouse = sheet_alloc(shtctl);
    sheet_setbuf(sht_mouse, buf_mouse, 16, 16, 99);
    init_mouse_cursor8(buf_mouse, 99); 					// 마우스 커서
	mx = (binfo->scrnx - 16) / 2;
	my = (binfo->scrny - 28 - 16) / 2;
    
	// 시트 초기 위치로 이동 및 디스플레이 순서 설정
	sheet_slide(sht_back, 0, 0);
	sheet_slide(key_win, 32, 4);
	sheet_slide(sht_mouse, mx, my);
	
	// 시트 디스플레이 순서 설정
	sheet_updown(sht_back,     0);
	sheet_updown(key_win,      1);
	sheet_updown(sht_mouse,    2); // 마우스는 항상 제일 위에 디스플레이

	// 키보드 활성화된 윈도우로 설정
	keywin_on(key_win);

	fifo32_put(&keycmd, KEYCMD_LED);										// 키보드 LED 명령
	fifo32_put(&keycmd, key_leds);											// 현재 LED 상태 전송

	putfonts_sht(sht_back,  8,  8, COL8_FFFFFF, COL8_008484, "머꼬 OS v1.0", 12);

	// 메인 루프
	for (;;) {
		// 키보드 LED 상태 전송
		if (fifo32_status(&keycmd) > 0 && keycmd_wait < 0) {
			keycmd_wait = fifo32_get(&keycmd);
			wait_KBC_sendready();
			io_out8(PORT_KEYCMD, keycmd_wait);
		}
		io_cli(); // CPU 인터럽트 비활성화
		// FIFO 버퍼 상태 확인
		if (fifo32_status(&fifo) == 0) { 						// fifo가 비어있음
			if (new_mx >= 0) { 										// 마우스 이동이 있으면
				io_sti(); 												// CPU 인터럽트 활성화
				sheet_slide(sht_mouse, new_mx, new_my); 				// 마우스 시트 이동
				new_mx = -1; 											// 마우스 x 위치 초기화
			} else if (new_wx != 0x7fffffff) { 						// 윈도우 이동이 있으면
				io_sti(); 												// CPU 인터럽트 활성화
				sheet_slide(sht, new_wx, new_wy); 						// 윈도우 시트 이동
				new_wx = 0x7fffffff; 									// 윈도우 x 위치 초기화
			} else {												// 할 일이 없으면, 휴면
            	task_sleep(task_a); 									// 태스크 a 휴면
				io_sti(); 												// CPU 인터럽트 활성화
			}
		} else { 												// fifo가 비어있지 않음
			i = fifo32_get(&fifo); 									// FIFO 버퍼에서 데이터 가져오기
			io_sti(); 												// CPU 인터럽트 활성화
			if (key_win != 0 && key_win->flags == 0) { 				// key_win이 닫혀 있음
				if (shtctl->top == 1) { 								// 배경 시트만 남음
					key_win = 0; 											// 활성 윈도우 없음
				} else {												// 다음 최상위 시트로 전환
					key_win = shtctl->sheets[shtctl->top - 1]; 				// 다음 최상위 시트
					keywin_on(key_win); 									// 해당 윈도우 키 입력 활성화
				}
			}
			// 키보드 데이터 처리 로직
			// 키보드 데이터는 256 이상으로 구분하여 처리 
			// (0-255: 타이머, 256-511: 키보드, 512-767: 마우스, 768-1023: 콘솔 종료, 1024-2023: 콘솔 태스크 종료, 2024-2279: 콘솔만 종료)
			if (256 <= i && i <= 511) {
                if (i < 256+0x80) { 					// 문자로 변환
					if (key_shift == 0) { 				// 소문자
						s[0] = keytable0[i - 256];
					} else { 							// 대문자
						s[0] = keytable1[i - 256];
					}
				} else { 								// 특수 키
					s[0] = 0;
				}
				if ('A' <= s[0] && s[0] <= 'Z') { 					// 알파벳 문자
					if (((key_leds & 4) == 0 && key_shift == 0) ||
						((key_leds & 4) != 0 && key_shift != 0)) {
							s[0] += 0x20; // 소문자로 변환
					}
				}
				if (s[0] != 0 && key_win != 0) { 						// 출력 가능한 문자
                    fifo32_put(&key_win->task->fifo, s[0] + 256);		// 활성된 윈도우에 문자 전송
                }
				if (i == 256 + 0x0f && key_win != 0) { 					// tab 키 눌림

					// 교재의 tab 키는 윈도우 전환이었으나, 나노에서 tab 사용시 기능이 충돌되어 비활성화
					// keywin_off(key_win);
					// j = key_win->height - 1;
					// if (j == 0) {
					// 	j = shtctl->top - 1;
					// }
					// key_win = shtctl->sheets[j];
					// keywin_on(key_win);

					fifo32_put(&key_win->task->fifo, 0xFE + 256); // 0xFE: tab 키에 대한 가상 제어 문자
				}
				if (i == 256 + 0x2a) { // left shift 눌림
					key_shift |= 1;
				}
				if (i == 256 + 0x36) { // right shift 눌림
					key_shift |= 2;
				}
				if (i == 256 + 0xaa) { // left shift 해제
					key_shift &= ~1;
				}
				if (i == 256 + 0xb6) { // right shift 해제
					key_shift &= ~2;
				}
				if (i == 256 + 0x1d) { // Ctrl 눌림
					key_ctrl |= 1;
				}
				if (i == 256 + 0x9d) { // Ctrl 해제
					key_ctrl &= ~1;
				}
				if (i == 256 + 0x3a) { // CapsLock 눌림
					key_leds ^= 4;
					fifo32_put(&keycmd, KEYCMD_LED);
					fifo32_put(&keycmd, key_leds);
				}
				if (i == 256 + 0x45) { // NumLock 눌림
					key_leds ^= 2;
					fifo32_put(&keycmd, KEYCMD_LED);
					fifo32_put(&keycmd, key_leds);
				}
				if (i == 256 + 0x46) { // ScrollLock 눌림
					key_leds ^= 1;
					fifo32_put(&keycmd, KEYCMD_LED);
					fifo32_put(&keycmd, key_leds);
				}
				if (i == 256 + 0x39 && key_shift != 0) {
					// 한영 전환
					fifo32_put(&key_win->task->fifo, 256 + 0xFF); // 0xFF: Shift + Space 가상 제어 문자
				}
				if (i == 256 + 0x57) { // F11 눌림
					sheet_updown(shtctl->sheets[1], shtctl->top - 1); // 콘솔을 제일 위로 가져옴(마우스 바로 아래)
				}
				if (i == 256 + 0xfa) { // 키보드 데이터 수신 완료
					keycmd_wait = -1;
				}
				if (i == 256 + 0xfe) { // 키보드 데이터 수신 실패
					wait_KBC_sendready();
					io_out8(PORT_KEYCMD, keycmd_wait);
				}
				if (i == 256 + 0x2e && key_ctrl != 0 && key_win != 0) { // Ctrl + C 눌림
					// 프로세스 강제 종료
					task = key_win->task;
					if (task != 0 && task->tss.ss0 != 0) {
						cons_putstr(task->cons, "\nBreak(key) :\n");
						io_cli(); // CPU 인터럽트 비활성화
						task->tss.eax = (int) &(task->tss.esp0);
						task->tss.eip = (int) asm_end_app;
						io_sti(); // CPU 인터럽트 활성화
						task_run(task, -1, 0);
					}
				}
				if (i == 256 + 0x3c && key_shift != 0) { // Shift + F2 눌림
					// 콘솔 창 열기
					if (key_win != 0) {
						keywin_off(key_win);
					}
					key_win = open_console(shtctl, memtotal, 1);
					sheet_slide(key_win, 32, 4);
					sheet_updown(key_win, shtctl->top);
					keywin_on(key_win);
				}
			} else if (512 <= i && i <= 767) { // 마우스 데이터
				if (mouse_decode(&mdec, i - 512) != 0) {
                    // 커서
					mx += mdec.x;
					my += mdec.y;
					if (mx < 0) {
						mx = 0;
					}
					if (my < 0) {
						my = 0;
					}
					if (mx > binfo->scrnx - 1) {
						mx = binfo->scrnx - 1;
					}
					if (my > binfo->scrny - 1) {
						my = binfo->scrny - 1;
					}
					new_mx = mx;
					new_my = my;
                    if ((mdec.btn & 0x01) != 0) { // 좌클릭
						if (mmx < 0) {
							// 일반 모드
							// (mx, my)에서 가장 위에 있는 윈도우를 확인
							for (j=shtctl->top-1; j>0; j--) {
								sht = shtctl->sheets[j];
								x = mx - sht->vx0;
								y = my - sht->vy0;
								if (0 <= x && x < sht->bxsize && 0 <= y && y < sht->bysize) {
									if (sht->buf[y * sht->bxsize + x] != sht->col_inv) {
										sheet_updown(sht, shtctl->top - 1);
										if (sht != key_win) {
											keywin_off(key_win);
											key_win = sht;
											keywin_on(key_win);
										}
										if (3<=x && x<sht->bxsize-3 && 3<=y && y<21) { // 타이틀 바 클릭
											// move mode로 변환
											mmx = mx;
											mmy = my;
											mmx2 = sht->vx0;
											new_wy = sht->vy0;
										}
										if (sht->bxsize-21 <= x && x < sht->bxsize-5 && 5 <= y && y < 19) {
											// 닫기 버튼 클릭
											if ((sht->flags & 0x10) != 0) { // 앱 윈도우일 경우
												task = sht->task;
												cons_putstr(task->cons, "\nBreak(mouse) :\n");
												io_cli(); // CPU 인터럽트 비활성화
												task->tss.eax = (int) &(task->tss.esp0);
												task->tss.eip = (int) asm_end_app;
												io_sti(); // CPU 인터럽트 활성화
												task_run(task, -1, 0);
											} else { // 콘솔 윈도우일 경우
												task = sht->task;
												sheet_updown(sht, -1); // 윈도우 숨기기
												keywin_off(key_win);
												key_win = shtctl->sheets[shtctl->top-1];
												keywin_on(key_win);
												io_cli(); // CPU 인터럽트 비활성화
												fifo32_put(&task->fifo, 4);
												io_sti(); // CPU 인터럽트 활성화
											}
										}
										break;
									}
								}
							}
						} else {
							// move mode
							x = mx - mmx;
							y = my - mmy;
							new_wx = (mmx2 + x + 2) & ~3;
							new_wy = new_wy + y;
							mmy = my;
						}
                    } else {
						mmx = -1;
						if (new_wx != 0x7fffffff) {
							sheet_slide(sht, new_wx, new_wy);
							new_wx = 0x7fffffff;
						}
					}
				}
			} else if (768 <= i && i <= 1023) {	// 콘솔 종료 신호
				close_console(shtctl->sheets0 + (i - 768));
			} else if (1024 <= i && i <= 2023) { // 콘솔 태스크 종료 신호
				close_constask(taskctl->tasks0 + (i - 1024));
			} else if (2024 <= i && i <= 2279) { // 콘솔만 종료
				sht2 = shtctl->sheets0 + (i - 2024);
				memman_free_4k(memman, (int) sht2->buf, 256 * 165);
				sheet_free(sht2);
			}
		}
	}
}

/**
 * @brief 비활성화된 키보드 윈도우 처리
 * 
 * @param key_win: 비활성화할 시트 포인터
 */
void keywin_off(struct SHEET *key_win)
{
	change_wtitle8(key_win, 0);
	if ((key_win->flags & 0x20) != 0) { 
		fifo32_put(&key_win->task->fifo, 3);	// 커서 끄기
	}
	return;
}

/**
 * @brief 활성화된 키보드 윈도우 처리
 * 
 * @param key_win: 활성화할 시트 포인터
 */
void keywin_on(struct SHEET *key_win)
{
	change_wtitle8(key_win, 1);
	if ((key_win->flags & 0x20) != 0) { 
		fifo32_put(&key_win->task->fifo, 2);	// 커서 켜기
	}
	return;
}

/**
 * @brief 콘솔 태스크 열기
 * 
 * @param sht: 콘솔 시트 포인터
 * @param memtotal: 총 메모리 크기
 * @param langmode: 언어 모드 (0: 영어, 1: 한글)
 * @return struct TASK*: 생성된 콘솔 태스크 포인터
 */
struct TASK *open_constask(struct SHEET *sht, unsigned int memtotal, int langmode)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct TASK *task = task_alloc();
	int *cons_fifo = (int *) memman_alloc_4k(memman, 128 * 4);					// 콘솔 FIFO 버퍼 할당
	task->cons_stack = memman_alloc_4k(memman, 64 * 1024);               		// 콘솔 스택 할당
	task->tss.esp = task->cons_stack + 64 * 1024 - 16;
	task->tss.eip = (int) &console_task;
	task->tss.es = 1 * 8;
	task->tss.cs = 2 * 8;
	task->tss.ss = 1 * 8;
	task->tss.ds = 1 * 8;
	task->tss.fs = 1 * 8;
	task->tss.gs = 1 * 8;
	*((int *) (task->tss.esp + 4)) = (int) sht;									// argument: sht
	*((int *) (task->tss.esp + 8)) = memtotal;                                  // argument: memtotal
	*((int *) (task->tss.esp + 12)) = langmode;                                // argument: langmode
	task_run(task, 2, 2);														// 콘솔 태스크 실행
	fifo32_init(&task->fifo, 128, cons_fifo, task);                 			// 콘솔 FIFO 버퍼 초기화
	return task;
}

/**
 * @brief 콘솔 창 열기
 * 
 * @param shtctl: 시트 컨트롤 포인터
 * @param memtotal: 총 메모리 크기
 * @return struct SHEET*: 생성된 콘솔 시트 포인터
 */
struct SHEET *open_console(struct SHTCTL *shtctl, unsigned int memtotal, int langmode)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct SHEET *sht = sheet_alloc(shtctl);
	unsigned char *buf = (unsigned char *) memman_alloc_4k(memman, CONSOLE_WIDTH * CONSOLE_HEIGHT);						// 콘솔 버퍼 할당
	char *title = "콘솔";
	sheet_setbuf(sht, buf, CONSOLE_WIDTH, CONSOLE_HEIGHT, -1);															// 콘솔 시트 버퍼 설정
	make_window8(buf, CONSOLE_WIDTH, CONSOLE_HEIGHT, title, 0);                                  						// 콘솔 윈도우 그리기
	make_textbox8(sht, 8, 28, CONSOLE_TBOX_WIDTH, CONSOLE_TBOX_HEIGHT, COL8_000000);                            		// 텍스트박스 영역 그리기
	sht->task = open_constask(sht, memtotal, langmode);                                   								// 시트에 태스크 포인터 저장
	sht->flags |= 0x20;                                                         										// 커서 있음
	return sht;
}

/**
 * @brief 콘솔 태스크 닫기
 * 
 * @param task: 닫을 태스크 포인터
 */
void close_constask(struct TASK *task)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	task_sleep(task);
	memman_free_4k(memman, task->cons_stack, 64 * 1024);
	memman_free_4k(memman, (int) task->fifo.buf, 128 * 4);
	task->flags = 0; // 미사용 플래그 설정
	return;
}

/**
 * @brief 콘솔 창 닫기
 * 
 * @param sht: 닫을 시트 포인터
 */
void close_console(struct SHEET *sht)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct TASK *task = sht->task;
	memman_free_4k(memman, (int) sht->buf, CONSOLE_WIDTH * CONSOLE_HEIGHT);
	sheet_free(sht);
	close_constask(task);
	return;
}
