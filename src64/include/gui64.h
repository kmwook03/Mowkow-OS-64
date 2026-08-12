#ifndef MOWKOW64_GUI64_H
#define MOWKOW64_GUI64_H

#include <bootinfo64.h>
#include <fifo64.h>
#include <mouse64.h>
#include <sheet64.h>
#include <stdint.h>

/* 컴포지터를 세우고 바탕화면 + 콘솔 시트를 만든다.
   콘솔 시트를 돌려주며, 실패하면 NULL (호출자는 LFB 직접 그리기로 되돌아간다). */
struct SHEET64 *gui64_init(const struct BOOTINFO64 *boot_info);

/* 콘솔 창을 전체 화면 <-> 바탕화면 위의 창 모드로 전환한다. */
void gui64_toggle_window(void);

/* 마우스 이동량과 버튼 상태. 커서 이동 + 클릭 포커스 + 타이틀 바 드래그. */
void gui64_mouse_event(int32_t dx, int32_t dy, int32_t btn);

/* 키 입력을 콘솔이 받아야 하는지. 다른 창이 활성이면 0. */
int gui64_console_has_focus(void);

/* F11 -- 맨 아래 창을 위로. */
void gui64_raise_bottom_window(void);

/* 마우스와 창 관리 키(F11)를 먹는다. 처리했으면 1.
   커널 메인 루프와 raw 모드 키 대기 루프가 둘 다 먼저 부른다 -- 그래야
   앱이 콘솔을 쥐고 있는 동안에도 포인터가 살아 있다. */
int gui64_handle_system_event(const struct EVENT64 *event);

/* init_mouse64에 넘길 디코더. 소유자는 gui64다. */
struct MOUSE_DEC64 *gui64_mouse_dec(void);

#endif
