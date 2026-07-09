/**
 * @file console.c
 * 
 * @brief 콘솔 관련 함수 구현
 * 
 * - 콘솔 태스크 함수: console_task
 * - 콘솔에 문자 출력: cons_put_utf8
 * - 명령어 처리 함수: cmd_help, cmd_cls, cmd_dir, cmd_type, cmd_exit
 * - 콘솔 종료 처리: cmd_exit
 * - 기타 유틸리티 함수: cons_newline, cons_putchar
 * - 한글 입력 처리 포함
 * - 파일 시스템 관련 함수: cmd_dir
 * - 콘솔 시트 업데이트 및 커서 제어 포함
 * - api 구현 함수: hrb_api
 */

#include "../include/bootpack.h"
#include "../include/utf8.h"
#include <stdio.h>
#include <string.h>

void cons_debug(struct CONSOLE *cons, char *cmdline);

/** 
 * @brief 콘솔 태스크 함수
 * 
 * 키보드 입력 및 명령어 처리 담당
 *
 *  콘솔 시트 업데이트 및 커서 제어 포함
 * 
 * @param sht: 콘솔 시트 포인터
 * @param memtotal: 총 메모리 크기
 * @param langmode: 언어 모드 (0: 영어, 1: 한글)
 * 
 * @return: void
 */
void console_task(struct SHEET *sht, int memtotal, int langmode)
{
    struct TASK *task = task_now();                                     // 현재 태스크 포인터 얻기
    struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;              // 메모리 관리자 포인터
    int i;
	struct CONSOLE cons;                                                // 콘솔 구조체
    FDHANDLE fhandle[8];                                                // 파일 핸들 구조체 배열
    char cmdline[256];                                                  // 명령어 입력 버퍼

    // 콘솔 구조체 초기화
    cons.sht = sht;
    cons.cur_x = 8;
    cons.cur_y = 28;
    cons.cur_c = -1;
    task->cmdline = cmdline;
    cons.cur_width = 8;
    task->cons = &cons;

    if (cons.sht != 0) {
        cons.timer = timer_alloc();
        timer_init(cons.timer, &task->fifo, 1);
        timer_settime(cons.timer, 50);
    }
    task->fhandle = fhandle;        // 파일 핸들 배열 설정
    task->fhandle_count = 8;        // 열린 파일 핸들 개수

    task->langmode = langmode;       // 언어모드 설정
    set_hangul(task, 0, -1, -1, -1); // 한글 오토마타 초기화
    cons_put_utf8(&cons, ">", 1, 1);     // 프롬프트 출력        
    cons.cmd_pos = 0;

    static int k_cnt = 0;

    // 메인 루프
    for (;;) {
        io_cli(); // 인터럽트 금지
        if (fifo32_status(&task->fifo) == 0) {          // FIFO 버퍼가 비어있으면
            task_sleep(task);                           //    태스크 슬립
            io_sti();                                   //    인터럽트 허용
        } else {                                        // FIFO 버퍼에 데이터가 있으면
            i = fifo32_get(&task->fifo);                //    데이터 처리
            io_sti();                                   //    인터럽트 허용    
            if (i <= 1 && cons.sht != 0) { // 커서 깜빡임 처리
                if (i != 0) {
                    timer_init(cons.timer, &task->fifo, 0); // 커서 ON
					if (cons.cur_c >= 0) {
                    	cons.cur_c = COL8_FFFFFF;
					}
                } else {
                    timer_init(cons.timer, &task->fifo, 1); // 커서 OFF
					if (cons.cur_c >= 0) {
                    	cons.cur_c = COL8_000000;
					}
                }
                timer_settime(cons.timer, 50); // 0.5초 간격
            }
			if (i == 2) { // 커서 켜기
				cons.cur_c = COL8_FFFFFF;
			}
			if (i == 3) { // 커서 끄기
                if (cons.sht != 0) {
				    boxfill8(cons.sht->buf, cons.sht->bxsize, COL8_000000, cons.cur_x, cons.cur_y, cons.cur_x + cons.cur_width - 1, cons.cur_y + 15);
                }
				cons.cur_c = -1;
			}
            if (i == 4) {
                cmd_exit(&cons); // 콘솔 종료
            }
			if (256 <= i && i <= 511) {
				if (i == 127 + 256) {                                         // backspace: 지우기
                    if (cons.cur_x > 16) {
                        cons_put_utf8(&cons, " ", 1, 0);                    // 커서 지우기
                                            // 조합 중인 한글 삭제 시도
                        if (hangul_automata_delete(&cons, task) == 1) { 
                            // 한글 오토마타가 처리함
                            continue;
                        }

                        unsigned char last_char = (unsigned char)cmdline[cons.cmd_pos - 1];
                        if (last_char < 0x80) {     // ASCII
                            cons.cur_x -= 8;
                            cons.cmd_pos--;
                        } else {                    // UTF-8 한글
                            cons.cur_x -= 16;
                            cons.cmd_pos -= 3;
                            boxfill8(cons.sht->buf, cons.sht->bxsize, COL8_000000, cons.cur_x, cons.cur_y, cons.cur_x + 15, cons.cur_y + 15);
                            sheet_refresh(cons.sht, cons.cur_x, cons.cur_y, cons.cur_x + 16, cons.cur_y + 16);
                        }
                    }
				} else if (i == 10 + 256) {                     // enter: 줄바꿈
                    if (task->hangul.state != 0) {
                        flush_hangul_to_cmdline(&cons, task, cmdline); // 조합 중인 한글 확정
                        set_hangul(task, 0, -1, -1, -1);                    // 한글 오토마타 초기화
                    }
                    cons_put_utf8(&cons, " ", 1, 0);                // 커서 지우기
					cmdline[cons.cmd_pos] = 0;            // 명령어 라인 종료 문자
                    cons.cmd_pos = 0;

                    // cons_debug(&cons, cmdline);    // 디버그용
                    
                    cons_newline(&cons);                        // 줄바꿈
                    cons_runcmd(cmdline, &cons, memtotal); // 명령어 실행
                    if (cons.sht == 0) {                        // 콘솔 시트가 없으면
                        cmd_exit(&cons);                   // 콘솔 태스크 종료
                    }
                    cons_put_utf8(&cons, ">", 1, 1);                // 프롬프트 출력
				} else if (i == 0xFF) {	// Shift + Space
                    // 언어 모드 변경
                    task->langmode ^= 1;
                    flush_hangul_to_cmdline(&cons, task, cmdline); // 조합 중인 한글 확정
                    set_hangul(task, 0, -1, -1, -1);    // 한글 오토마타 초기화
                } else {
                    // 일반 문자 입출력
                    if (i == 256 + 0x1D) continue; // 조합 문자 무시
                    if (cons.cmd_pos >= 255) continue; // 명령어 버퍼 오버플로우 방지

                    int key = i - 256;                      // 입력된 키 값 (ASCII 코드)

                    if (key == 0xFF) {                 // 가상 제어 문자
                        task->langmode ^= 1;
                    } else if (task->langmode == 1) {                  // 한글 모드
                        if (key < 0x80) {
                            hangul_automata(&cons, task, key, cmdline);      // 한글 오토마타가 처리
                        } else {
                            k_cnt++;
                            cmdline[cons.cmd_pos] = key;
                            cons.cmd_pos++;
                            if (k_cnt == 3) {
                                cons_put_utf8(&cons, &cmdline[cons.cmd_pos - 3], 3, 1); // UTF-8 문자 출력
                                k_cnt = 0;
                            }
                        }
                    } else {                                        // 영어 모드
						cmdline[cons.cmd_pos] = key;  // 명령어 라인에 문자 저장
                        cons.cmd_pos++;
						cons_put_utf8(&cons, (char *)&key, 1, 1);        // 문자 출력
                    }
				}
			}
            // 커서 초기화
            if (cons.sht != 0) {
			    if (cons.cur_c >= 0) {
				    boxfill8(cons.sht->buf, cons.sht->bxsize, cons.cur_c, cons.cur_x, cons.cur_y, cons.cur_x + cons.cur_width - 1, cons.cur_y + 15);
			    }
                sheet_refresh(cons.sht, cons.cur_x, cons.cur_y, cons.cur_x + cons.cur_width, cons.cur_y + 16);
        
            }    
        }
    }
}

void cons_putchar(struct CONSOLE *cons, int chr, char move)
{
    char s[2];
    s[0] = chr;
    s[1] = 0;
    if (s[0] == 0x09) { // 탭 문자 처리
        for (;;) {
            if (cons->sht != 0) {
                putfonts8_asc_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, " ", 1);   // 공백 출력
            }
            cons->cur_x += 8;                                                                               // 커서 이동
            if (cons->cur_x == 8 + 240) {
                cons_newline(cons);                                                                         // 줄바꿈
            }
            if (((cons->cur_x - 8) & 0x1f) == 0) {                                                          // 탭 간격(32픽셀) 도달했으면 break;
                break;
            }
        }
    } else if (s[0] == 0x0a) { // 줄바꿈
        cons_newline(cons);
    } else if (s[0] == 0x0d) { // 뭐 없음
        // do nothing
    } else {
        if (cons->sht != 0) {
            putfonts8_asc_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, s, 1);        // 문자 출력
        }
        if (move != 0) { // 커서 이동
            cons->cur_x += 8;
            if (cons->cur_x == 8 + 240) {
                cons_newline(cons); // 줄바꿈
            }
        }
    }
    return;
}

/**
 * @brief 콘솔에 문자 출력
 * 
 * @param cons: 콘솔 구조체 포인터
 * @param chr: 출력할 문자
 * @param move: 커서 이동 여부 (0: 이동 안함, 1: 이동함)
 * @return: void
 */
void cons_put_utf8(struct CONSOLE *cons, char *s, int len, char move)
{
    if (s[0] == 0x09) {
        for (;;) {
            if (cons->sht != 0) {
                putfonts_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, " ", 1);        // 공백 출력
            }
            cons->cur_x += 8;                                                                               // 커서 이동
            if (cons->cur_x >= 8 + CONSOLE_TBOX_WIDTH) {                                                    
                cons_newline(cons);                                                                         // 줄바꿈
            }
            if (((cons->cur_x - 8) & 0x1f) == 0) {                                                          // 탭 간격(32픽셀) 도달했으면 break;
                break;
            }
        }
    } else if (s[0] == 0x0a) { // 줄바꿈
        cons_newline(cons);
    } else if (s[0] == 0x7F) { // 백스페이스
        if (cons->cur_x > 0) {
            /* 커서를 한 칸(보통 8픽셀) 앞으로 당김 */
            cons->cur_x -= 8;
        }
    }
     else if (s[0] == 0x0d) { // 뭐 없음
        // do nothing
    } else {
        int width = (len == 1) ? 8 : 16;
        if (cons->cur_x + width >= 8 + CONSOLE_TBOX_WIDTH) {
            boxfill8(cons->sht->buf, cons->sht->bxsize, COL8_000000, cons->cur_x, cons->cur_y, cons->cur_x + width - 1, cons->cur_y + 15); // 배경 지우기
            sheet_refresh(cons->sht, cons->cur_x, cons->cur_y, cons->cur_x + width, cons->cur_y + 16);
            cons_newline(cons); // 줄바꿈
        }
        if (cons->sht != 0) {
            putfonts_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, s, len);               // 문자 출력
        }
        if (move != 0) { // 커서 이동
            cons->cur_x += width;
        }
    }

    return;
}

void cons_putstr(struct CONSOLE *cons, char *s)
{
    int len;
    unsigned int unicode;
    while (*s != 0x00) {
        unicode = utf8_to_unicode(s, &len);
        if (len == 0) break; // 변환 실패 시 종료
        cons_put_utf8(cons, s, len, 1);
        s += len;
    }
    return;
}

/**
 * @brief 콘솔 줄바꿈 처리 함수
 * 
 * @param cons: 콘솔 구조체 포인터
 * @return: void
 */
void cons_newline(struct CONSOLE *cons)
{
	int x, y;
    struct SHEET *sht = cons->sht;
	if (cons->cur_y < 28 + CONSOLE_TBOX_HEIGHT - 16) {
        // cons_put_utf8(&cons, " ", 1, 0);
		cons->cur_y += 16;
	} else {
        // 스크롤
        if (sht != 0) {
		    for (y=28; y<28+CONSOLE_TBOX_HEIGHT-16; y++) {
			    for (x=8; x<8+CONSOLE_TBOX_WIDTH; x++) {
				    sht->buf[x + y * sht->bxsize] = sht->buf[x + (y + 16) * sht->bxsize];
			    }
		    }
		
		    for (y=28+CONSOLE_TBOX_HEIGHT-16; y<28+CONSOLE_TBOX_HEIGHT; y++) {
			    for (x=8; x<8+CONSOLE_TBOX_WIDTH; x++) {
				    sht->buf[x + y * sht->bxsize] = COL8_000000;
			    }
		    }
		    sheet_refresh(sht, 8, 28, 8 + CONSOLE_TBOX_WIDTH, 28 + CONSOLE_TBOX_HEIGHT);
        }
    }
    cons->cur_x = 8;
	return;
}

/**
 * @brief 콘솔 명령어 실행 함수
 * 
 * @param cmdline: 명령어 문자열
 * @param cons: 콘솔 구조체 포인터
 * @param memtotal: 총 메모리 크기
 * @return: void 
 */
void cons_runcmd(char *cmdline, struct CONSOLE *cons, int memtotal)
{
    if ((strcmp(cmdline, "mem") == 0 || strcmp(cmdline, "메모리") == 0) && cons->sht != 0) {
        cmd_mem(cons, memtotal);
    } else if ((strcmp(cmdline, "cls") == 0 || strcmp(cmdline, "clear") == 0 || strcmp(cmdline, "지우기") == 0) && cons->sht != 0) {
        cmd_cls(cons);
    } else if ((strcmp(cmdline, "dir") == 0 || strcmp(cmdline, "ls") == 0 || strcmp(cmdline, "목록") == 0) && cons->sht != 0) {
        cmd_dir(cons);
    } else if ((strcmp(cmdline, "exit") == 0 || strcmp(cmdline, "종료") == 0)) {
        cmd_exit(cons);
    } else if (strncmp(cmdline, "start ", 6) == 0 || strncmp(cmdline, "실행 ", 3) == 0) {
        cmd_start(cons, cmdline, memtotal, cons->sht->task->langmode);
    } else if (strncmp(cmdline, "ncst ", 5) == 0 || strncmp(cmdline, "바로실행 ", 5) == 0) {
        cmd_ncst(cons, cmdline, memtotal, cons->sht->task->langmode);
    } else if (strncmp(cmdline, "langmode ", 9) == 0 || strncmp(cmdline, "언어 ", 3) == 0) {
        cmd_langmode(cons, cmdline);
    } else if (strncmp(cmdline, "touch ", 6) == 0) {
        cmd_touch(cons, cmdline);
    } else if (cmdline[0] != 0) {			
        if (cmd_app(cons, cmdline) == 0) {		
            if (cons->sht->task->langmode == 0) {
                cons_putstr(cons, "Bad command.\n\n");
            } else {
                cons_putstr(cons, "잘못된 명령어.\n\n");
            }
        }   
    }

    return;
}

/**
 * @brief mem command (메모리 사용량 표시)
 *
 * @param cons: 콘솔 구조체 포인터
 * @param memtotal: 총 메모리 크기
 * @return: void 
 */
void cmd_mem(struct CONSOLE *cons, int memtotal)
{
    // mem command
    struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
    char s[128];
	sprintf(s, "총 %dMB\n빈 공간 %dKB\n\n", memtotal / (1024 * 1024), memman_total(memman) / 1024);
	cons_putstr(cons, s);
    return;
}

/**
 * @brief cls/clear command (콘솔 화면 지우기)
 * 
 * @param cons: 콘솔 구조체 포인터
 * @return: void
 */
void cmd_cls(struct CONSOLE *cons)
{
    // cls/clear command
    struct SHEET *sht = cons->sht;
    int x, y;
    for (y=28; y<28+CONSOLE_TBOX_HEIGHT; y++) {
        for (x=8; x<8+CONSOLE_TBOX_WIDTH; x++) {
            sht->buf[x + y * sht->bxsize] = COL8_000000;
        }
    }
    sheet_refresh(sht, 8, 28, 8 + CONSOLE_TBOX_WIDTH, 28 + CONSOLE_TBOX_HEIGHT);
    cons->cur_y = 28;
    return;
}

/**
 * @brief dir/ls command (디렉토리 목록 표시)
 * @param cons: 콘솔 구조체 포인터
 * @return: void
 */
void cmd_dir(struct CONSOLE *cons)
{
    // dir/ls command
    FDINFO *finfo = (FDINFO *) (ADR_DISKIMG + 0x002600);
    int i, j;
    char s[30];
    for (i=0; i<224; i++) {
		if (finfo[i].name[0] == 0x00) {
			break;
		}
		if (finfo[i].name[0] != 0xe5) {
			if ((finfo[i].type & 0x18) == 0) {
				sprintf(s, "filename.ext   %7d\n", finfo[i].size);
				for (j=0; j<8; j++) {
			    	s[j] = finfo[i].name[j];
				}
				s[9] = finfo[i].ext[0];
				s[10] = finfo[i].ext[1];
				s[11] = finfo[i].ext[2];
				cons_putstr(cons, s);
			}
		}
	}
	cons_newline(cons);
    return;
}

/**
 * @brief exit command (콘솔 종료 및 태스크 종료)
 * 
 * @param cons: 콘솔 구조체 포인터
 * @return: void
 */
void cmd_exit(struct CONSOLE *cons)
{
    struct TASK *task = task_now();
    struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
    struct FIFO32 *fifo = (struct FIFO32 *) *((int *) 0x0fec);
    if (cons->sht != 0) {
        timer_cancel(cons->timer);
    }
    io_cli(); // disable CPU interrupts
    if (cons->sht != 0) {
        fifo32_put(fifo, cons->sht - shtctl->sheets0 + 768);    // 768 - 1023
    } else {
        fifo32_put(fifo, task - taskctl->tasks0 + 1024);          // 1024 - 2023
    }
    io_sti();
    for (;;) {
        task_sleep(task);
    }
}

/**
 * @brief start command (새 콘솔 생성 후 앱 실행)
 * 
 * @param cons: 콘솔 구조체 포인터
 * @param cmdline: 명령어 문자열
 * @param memtotal: 총 메모리 크기
 * @return: void
 */
void cmd_start(struct CONSOLE *cons, char *cmdline, int memtotal, int langmode)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *sht = open_console(shtctl, memtotal, langmode);
	struct FIFO32 *fifo = &sht->task->fifo;
	int i = 0;
	sheet_slide(sht, 32, 4);
	sheet_updown(sht, shtctl->top);

    int current_langmode = langmode;

    while (cmdline[i] != ' ') {
        i++;
    }
    i++; // skip space
	
    for (; cmdline[i] != 0; i++) {
        unsigned char c = cmdline[i];
        int needed_mode = (c >= 0x80) ? 1 : 0;

        if (current_langmode != needed_mode) {
            fifo32_put(fifo, 0xFF + 256); // 가상 제어 문자 전송
            current_langmode = needed_mode;
        }
        fifo32_put(fifo, c + 256); // ASCII를 key code로 변환해서 버퍼에 넣기
    }
    fifo32_put(fifo, 10 + 256);	// Enter
	return;
}

/**
 * @brief ncst command (새 콘솔에서 명령어 실행)
 *
 * @param cons: 콘솔 구조체 포인터
 * @param cmdline: 명령어 문자열
 * @param memtotal: 총 메모리 크기
 * @return: void
 */
void cmd_ncst(struct CONSOLE *cons, char *cmdline, int memtotal, int langmode)
{
	struct TASK *task = open_constask(0, memtotal, langmode);
	struct FIFO32 *fifo = &task->fifo;
	int i = 0;
    int current_langmode = langmode;

    while (cmdline[i] != ' ') {
        i++;
    }
    i++; // skip space

    for (; cmdline[i] != 0; i++) {
        unsigned char c = cmdline[i];
        int needed_mode = (c >= 0x80) ? 1 : 0;

        if (current_langmode != needed_mode) {
            fifo32_put(fifo, 0xFF + 256); // 가상 제어 문자 전송
            current_langmode = needed_mode;
        }
        fifo32_put(fifo, c + 256); // ASCII를 key code로 변환해서 버퍼에 넣기
    }
    fifo32_put(fifo, 10 + 256);	// Enter
	cons_newline(cons);
	return;
}

/**
 * @brief langmode command (언어 모드 설정)
 * 
 * @param cons: 콘솔 구조체 포인터
 * @param cmdline: 명령어 문자열
 * @return: void
 */
void cmd_langmode(struct CONSOLE *cons, char *cmdline)
{
    struct TASK *task = task_now();
    int i = 0;
    while (cmdline[i] != ' ') {
        i++;
    }
    i++; // skip space
    unsigned char mode = cmdline[i] - '0';
    if (mode <= 1) {
        task->langmode = mode;
        if (mode == 1) {
            cons_putstr(cons, "[English]\n");
        } else {
            cons_putstr(cons, "[한글]\n");
        }
    } else {
        if (task->langmode == 0) {
            cons_putstr(cons, "langmode command error. (0: English, 1: Korean)\n");
        } else {
            cons_putstr(cons, "언어 명령어 오류. (0: 영어, 1: 한글)\n");
        }
    }
    cons_newline(cons);
    return;
}

void cmd_touch(struct CONSOLE *cons, char *cmdline)
{
    char filename[13];
    char *content;
    int i, j;
    FDHANDLE fh;

    unsigned char c;
    for (i=6, j=0; i<30 && cmdline[i] != 0 && cmdline[i] != ' '; i++, j++) {
        c = (unsigned char)cmdline[i];
        if (c >= 'a' && c <= 'z') {
            c -= 0x20; // 대문자로 변환
        }
        filename[j] = c;
    }
    filename[j] = 0; // null-terminate

    if (filename[0] == 0) {
        cons_putstr(cons, "사용법: touch [filename]\n");
        return;
    }

    if (fd_writeopen(&fh, filename) == 0) {
        cons_putstr(cons, "파일 열기 오류.\n");
        return;
    }

    fh.modified = 1;
    fd_close(&fh);
    cons_putstr(cons, "파일 생성됨.\n");
    return;
}

typedef struct HrbHeader {
    unsigned int segsiz;  // 코드 + 데이터 세그먼트 크기
    char signature[4]; // "Hari" 고유 시그니처
    unsigned int reserved; // 예약 필드 (정렬 위함)
    unsigned int esp;     // 초기 ESP 값
    unsigned int datsiz;  // 데이터 세그먼트 크기
    unsigned int datadr;  // 데이터 세그먼트의 .hrb 파일 내 위치
    int jump;
    unsigned int entry;   // 진입점 주소 (파일 내 위치)
    unsigned int headadr;
    int dummy[3];
} HrbHeader;

/**
 * @brief 애플리케이션 실행 명령어 처리 함수
 *
 * @param cons: 콘솔 구조체 포인터
 * @param cmdline: 명령어 문자열
 * @return: 성공 시 1, 실패 시 0
 */
int cmd_app(struct CONSOLE *cons, char *cmdline)
{
    char name[13];
    int i;
    for (i=0; i<13; i++) {
        unsigned char c = (unsigned char)cmdline[i];
        if (c <= ' ') {
            break;
        }
        name[i] = c;
    }
    name[i] = 0; // null-terminate

    FDHANDLE fh;
    if (!fd_open(&fh, name)) {
        // .HRB 확장자 붙여서 다시 시도
        if (strlen(name) <= 8) {
            name[i] = '.';
            name[i+1] = 'H';
            name[i+2] = 'R';
            name[i+3] = 'B';
            name[i+4] = 0;
            if (!fd_open(&fh, name)) {
                return 0; // 파일 못 찾음
            }
        }
    }

    int file_size = fh.finfo->size;
    struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
    char *file_buf = (char *)memman_alloc_4k(memman, file_size);

    fd_read(&fh, file_buf, file_size);
    fd_close(&fh);

    char *exec_buf;
    int exec_size;

    if (file_size >= 4 && strncmp(file_buf + 4, "Hari", 4) == 0) {
        exec_buf = file_buf;
        exec_size = file_size;
    } else {
        // 압축된 앱 압축 해제 후 파일 처리
        if (file_size < 17) {
            cons_putstr(cons, "맞지 않는 hrb 파일 형식.\n");
            memman_free_4k(memman, (int) file_buf, file_size);
            return 0;
        }

        int decomp_size = tek_getsize((unsigned char *)file_buf);
        if (decomp_size > 0) {
            exec_buf = (char *)memman_alloc_4k(memman, decomp_size);
            tek_decomp((unsigned char *)file_buf, (unsigned char *)exec_buf, decomp_size);
            memman_free_4k(memman, (int) file_buf, file_size);

            if (strncmp(exec_buf + 4, "Hari", 4) != 0) {
                cons_putstr(cons, "맞지 않는 hrb 파일 형식.\n");
                memman_free_4k(memman, (int) exec_buf, decomp_size);
                return 0;
            }
            exec_size = decomp_size;
        } else {
            cons_putstr(cons, "파일 압축 해제 오류.\n");
            memman_free_4k(memman, (int) file_buf, file_size);
            return 0;
        }
    }
    

    HrbHeader header = *(HrbHeader *)(exec_buf);

    char *data_seg = (char *)memman_alloc_4k(memman, header.segsiz);

    struct TASK *task = task_now();
    task->ds_base = (int) data_seg;

    memcpy(data_seg + header.esp, exec_buf + header.datadr, header.datsiz);
    
    int data_end_offset = header.esp + header.datsiz;
    if (data_end_offset < header.segsiz) memset(data_seg + data_end_offset, 0, header.segsiz - data_end_offset); // BSS 영역 0으로 초기화

    set_segmdesc(task->ldt + 0, exec_size - 1, (int) exec_buf, AR_CODE32_ER + 0x60);
    set_segmdesc(task->ldt + 1, header.segsiz - 1, (int) data_seg, AR_DATA32_RW + 0x60);
    start_app(0x1B, 0 * 8 + 4, header.esp, 1 * 8 + 4, &(task->tss.esp0));

    struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
    for (i=0; i<MAX_SHEETS; ++i) {
        struct SHEET *sht = &shtctl->sheets0[i];
        if ((sht->flags & 0x11) == 0x11 && sht->task == task) {
            sheet_free(sht);
        }
    }

    for (i=0; i<task->fhandle_count; ++i) { fd_close(&task->fhandle[i]); }
    timer_cancelall(&task->fifo);

    memman_free_4k(memman, (int) exec_buf, exec_size);
    memman_free_4k(memman, (int) data_seg, header.segsiz);
    return 1;
}

static FDHANDLE *_api_fopen(struct TASK *task, const char* filename, int flag)
{
    FDHANDLE *fh = 0;
    int i;
    for (i=0; i<task->fhandle_count; ++i) {
        if (task->fhandle[i].finfo == 0) {
            fh = &task->fhandle[i];
            break;
        }
    }

    if (fh == 0) return 0;

    if (flag & 1) {
        if (fd_writeopen(fh, filename)) return fh;
    } else {
        if (fd_open(fh, filename)) return fh;
    }

    fh->finfo = 0; // 실패 시 핸들 초기화
    
    return 0;
}

/**
 * @brief HRB API 함수
 * 
 * 레지스터를 통해 호출되며 다양한 시스템 기능 제공
 * 
 * @param: edi, esi, ebp, esp, ebx, edx, ecx, eax
 * @return: eax 레지스터 값 포인터
 */
int *hrb_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax)
{
    struct TASK *task = task_now();
    int ds_base = task->ds_base;
    struct CONSOLE *cons = task->cons;
    struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
    struct SHEET *sht;
    struct FIFO32 *sys_fifo = (struct FIFO32 *) *((int *) 0x0fec);
    int *reg = &eax + 1; // next address of eax
    // reg[0] : edi, reg[1] : esi, reg[2] : ebp, reg[3] : esp
    // reg[4] : ebx, reg[5] : edx, reg[6] : ecx, reg[7] : eax
    int i;
    // struct FILEHANDLE *fh;
    // FDHANDLE *fd;
    struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;

    if (edx == 1) { // api_putchar()
        cons_putchar(cons, eax & 0xff, 1);
    } else if (edx == 2) { // api_putstr(char *s)
        cons_putstr(cons, (char *) ebx + ds_base);
    } else if (edx == 3) { // api_putstr_len(char *s, int l)
        int len = ecx;
        cons_put_utf8(cons, (char *) ebx + ds_base, len, 1);
    } else if (edx == 4) { // api_end()
        return &(task->tss.esp0);
    } else if (edx == 5) { // api_openwin(char *buf, int xsiz, int ysiz, int col_inv, char *title)
        sht = sheet_alloc(shtctl);
        sht->task = task;
        sht->flags |= 0x10;
        sheet_setbuf(sht, (char *) ebx + ds_base, esi, edi, eax);
        make_window8((char *) ebx + ds_base, esi, edi, (char *) ecx + ds_base, 0);
        sheet_slide(sht, ((shtctl->xsize - esi) / 2) & ~3, (shtctl->ysize - edi) / 2); // center and 4 pixel align (for optimization)
        sheet_updown(sht, shtctl->top);
        reg[7] = (int) sht;
    } else if (edx == 6) { // api_putstrwin(int win, int x, int y, int col, int len, char *str)
        sht = (struct SHEET *) (ebx & 0xfffffffe);
        putfonts(sht->buf, sht->bxsize, esi, edi, eax, (char *) ebp + ds_base);
        if ((ebx & 1) == 0) {
            sheet_refresh(sht, esi, edi, esi + ecx * 8, edi + 16);
        }
    } else if (edx == 7) { // api_boxfilwin(int win, int x0, int y0, int x1, int y1, int cal)
        sht = (struct SHEET *) (ebx & 0xfffffffe);
        boxfill8(sht->buf, sht->bxsize, ebp, eax, ecx, esi, edi);
        if ((ebx & 1) == 0) {
            sheet_refresh(sht, eax, ecx, esi + 1, edi + 1);
        }
    } else if (edx == 8) { // api_initmalloc(void)
        memman_init((struct MEMMAN *) (ebx + ds_base));
        ecx &= 0xfffffff0; // 16 byte align
        memman_free((struct MEMMAN *) (ebx + ds_base), eax, ecx);
    } else if (edx == 9) { // api_malloc(int size)
        ecx = (ecx + 0x0f) & 0xfffffff0; // 16 byte align
        reg[7] = memman_alloc((struct MEMMAN *) (ebx + ds_base), ecx);
    } else if (edx == 10) { // api_free(char *addr, int size)
        ecx = (ecx + 0x0f) & 0xfffffff0; // 16 byte align
        memman_free((struct MEMMAN *) (ebx + ds_base), eax, ecx);
    } else if (edx == 11) { // api_point(int win, int x, int y, int col)
        sht = (struct SHEET *) (ebx & 0xfffffffe);
        sht->buf[sht->bxsize * edi + esi] = eax;
        if ((ebx & 1) == 0) {
            sheet_refresh(sht, esi, edi, esi + 1, edi + 1);
        }
    } else if (edx == 12) { // api_refreshwin(int win, int x0, int y0, int x1, int y1)
        sht = (struct SHEET *) ebx;
        sheet_refresh(sht, eax, ecx, esi, edi);
    } else if (edx == 13) {
        sht = (struct SHEET *) (ebx & 0xfffffffe);
        hrb_api_linewin(sht, eax, ecx, esi, edi, ebp);
        if ((ebx & 1) == 0) {
            if (eax > esi) {
                i = eax;
                eax = esi;
                esi = i;
            }
            if (ecx > edi) {
                i = ecx;
                ecx = edi;
                edi = i;
            }
            sheet_refresh(sht, eax, ecx, esi + 1, edi + 1);
        }
    } else if (edx == 14) {
        sheet_free((struct SHEET *) ebx);
    } else if (edx == 15) { // api_getkey(int mode)
        for (;;) {
            io_cli();
            if (fifo32_status(&task->fifo) == 0) {
                if (eax != 0) {
                    task_sleep(task);
                } else {
                    io_sti();
                    reg[7] = -1;
                    return 0;
                }
            }
            i = fifo32_get(&task->fifo);
            io_sti();
            if (i <= 1 && cons->sht != 0) {
                timer_init(cons->timer, &task->fifo, 1);
                timer_settime(cons->timer, 50);
            }
            if (i == 2) {
                cons->cur_c = COL8_FFFFFF;
            }
            if (i == 3) {
                cons->cur_c = -1;
            }
            if (i == 4) {
                timer_cancel(cons->timer);
                io_cli();
                fifo32_put(sys_fifo, cons->sht - shtctl->sheets0 + 2024); // 2024 - 2279
                cons->sht = 0;
                io_sti();
            }
            if (i >= 256) {
                reg[7] = i - 256;
                return 0;
            }
        }
    } else if (edx == 16) {
        reg[7] = (int) timer_alloc();
        ((struct TIMER *) reg[7])->flags2 = 1;
    } else if (edx == 17) {
        timer_init((struct TIMER *) ebx, &task->fifo, eax + 256);
    } else if (edx == 18) {
        timer_settime((struct TIMER *) ebx, eax);
    } else if (edx == 19) {
        timer_free((struct TIMER *) ebx);
    } else if (edx == 20) {
        if (eax == 0) {
            i = io_in8(0x61);
            io_out8(0x61, i & 0x0d); // stop speaker
        } else {
            i = 1193180000 / eax;
            io_out8(0x43, 0xb6);
            io_out8(0x42, i & 0xff);
            io_out8(0x42, i >> 8);
            i = io_in8(0x61);
            io_out8(0x61, (i | 0x03) & 0x0f);
        }
    } else if (edx == 21) { // int api_fopen(char *fname)
        const char* filename = (char *) ebx + ds_base;
        int flag = eax;
        reg[7] = (int)_api_fopen(task, filename, flag);
    } else if (edx == 22) { // api_fclose(int fhandle)
        FDHANDLE *fh = (FDHANDLE *) eax;
        fd_close(fh);
    } else if (edx == 23) { // api_fseek(int fhandle, int offset, int mode)
        FDHANDLE *fh = (FDHANDLE *)eax;
        int origin = ecx;
        int offset = ebx;
        fd_seek(fh, offset, origin);
    } else if (edx == 24) { // api_fsize(int fhandle, int mode)
        FDHANDLE *fh = (FDHANDLE *)eax;
        int mode = ecx;
        switch (mode) {
            case 0: reg[7] = fh->finfo->size; break;
            case 1: reg[7] = fh->pos; break;
            case 3: reg[7] = fh->pos - fh->finfo->size; break;
        }
    } else if (edx == 25) { // api_fread(char *buf, int maxsize, int fhandle)
        FDHANDLE *fh = (FDHANDLE *)eax;
        unsigned char *dst = (unsigned char *)ebx + ds_base;
        int size = ecx;
        int read_bytes = fd_read(fh, dst, size);
        reg[7] = read_bytes;
    } else if (edx == 26) {
        i = 0;
        for (;;) {
            *((char *) ebx + ds_base + i) = task->cmdline[i];
            if (task->cmdline[i] == 0) {
                break;
            }
            if (i >= ecx) {
                break;
            }
            i++;
        }
        reg[7] = i;
    } else if (edx == 27) {
        reg[7] = task->langmode;
    } else if (edx == 28) { // api_fwrite(char *buf, int maxsize, int fhandle)
        FDHANDLE *fh = (FDHANDLE *) eax;
        char *buf = (char *) ebx + ds_base;
        int size = ecx;

        reg[7] = fd_write(fh, buf, size);
    }

    return 0;
}

/** @brief HRB API 선 그리기 함수
 *
 * @param sht: 그릴 시트 포인터
 * @param x0, y0: 시작 좌표
 * @param x1, y1: 끝 좌표
 * @param col: 선 색상
 * @return: void
 */
void hrb_api_linewin(struct SHEET *sht, int x0, int y0, int x1, int y1, int col)
{
    int i, x, y, len, dx, dy;

    dx = x1 - x0;
    dy = y1 - y0;
    x = x0 << 10;
    y = y0 << 10;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    if (dx >= dy) {
        len = dx + 1;
        if (x0 > x1) {
            dx = -1024;
        } else {
            dx = 1024;
        }
        if (y0 <= y1) {
            dy = ((y1 - y0 + 1) << 10) / len;
        } else {
            dy = ((y1 - y0 - 1) << 10) / len;
        }
    } else {
        len = dy + 1;
        if (y0 > y1) {
            dy = -1024;
        } else {
            dy = 1024;
        }
        if (x0 <= x1) {
            dx = ((x1 - x0 + 1) << 10) / len;
        } else {
            dx = ((x1 - x0 - 1) << 10) / len;
        }
    }
    for (i=0; i<len; i++) {
        sht->buf[(y >> 10) * sht->bxsize + (x >> 10)] = col;
        x += dx;
        y += dy;
    }

    return;
}

/** 
 * @brief INT 0C 핸들러 (스택 예외)
 * 
 * @param esp: 스택 포인터
 * @return: 새로운 스택 포인터
 */
int *inthandler0c(int *esp) {
    struct TASK *task = task_now();
    struct CONSOLE *cons = task->cons;
    char s[30];
    cons_putstr(cons, "\nINT 0C : \n Stack Exception.\n");
    sprintf(s, "EIP = %08X\n", esp[11]);
    cons_putstr(cons, s);
    return &(task->tss.esp0); // ABEND
}

/** 
 * @brief INT 0D 핸들러 (일반 보호 예외)
 *
 * @param esp: 스택 포인터
 * @return: 새로운 스택 포인터 
 */
int *inthandler0d(int *esp)
{
    struct TASK *task = task_now();
    struct CONSOLE *cons = task->cons;
    char s[30];
    cons_putstr(cons, "\nINT 0D : General Protected Exception.\n");
    sprintf(s, "EIP = %08X\n", esp[11]);
    cons_putstr(cons, s);
    return &(task->tss.esp0); // ABEND
}

void cons_debug(struct CONSOLE *cons, char *cmdline)
{
    cons_newline(cons);
    cons_putstr(cons, "Debug info:\n");
    unsigned char *c = (unsigned char *)cmdline;
    int i;
    for (i=0; cmdline[i] != 0; i++) {
        char s[40];
        sprintf(s, "%02X ", c[i]);
        cons_putstr(cons, s);
    }
}
