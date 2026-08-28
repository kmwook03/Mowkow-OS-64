#ifndef MOWKOW64_GUI64_H
#define MOWKOW64_GUI64_H

#include <bootinfo64.h>
#include <fifo64.h>
#include <mouse64.h>
#include <sheet64.h>
#include <stdint.h>

struct CONSOLE64;

/* 컴포지터를 세우고 바탕화면 + 콘솔 시트를 만든다.
   콘솔 시트를 돌려주며, 실패하면 NULL (호출자는 LFB 직접 그리기로 되돌아간다). */
struct SHEET64 *gui64_init(const struct BOOTINFO64 *boot_info);

/* 콘솔 창을 전체 화면 <-> 바탕화면 위의 창 모드로 전환한다. */
void gui64_toggle_window(void);

/* 마우스 이동량과 버튼 상태. 커서 이동 + 클릭 포커스 + 타이틀 바 드래그. */
void gui64_mouse_event(int32_t dx, int32_t dy, int32_t btn);

/* 어느 시트가 어느 콘솔의 것인지 알려 준다. 컴포지터는 창만 알고 콘솔을
   모르므로 콘솔 쪽에서 붙여 준다. */
void gui64_bind_console(struct SHEET64 *sht, struct CONSOLE64 *con);

/* 키를 받을 콘솔. 콘솔이 아닌 창이 활성이면 NULL -- 그때는 아무 데도 가지
   않는다 (숨은 콘솔에 몰래 들어가면 안 된다). */
struct CONSOLE64 *gui64_focused_console(void);

/* 콘솔용 창을 하나 연다. 버퍼와 시트를 만들고 창을 그린 뒤 콘솔을 붙이고
   포커스를 준다. 실패하면 -1. 콘솔 0의 전체 화면 창과 달리 창 크기 버퍼만
   잡는다 (화면 크기 버퍼는 콘솔 0 하나로 족하다). */
int gui64_open_console_window(struct CONSOLE64 *con, const char *title);

/* 콘솔 창을 실제로 없앤다. 콘솔이 접힌 뒤 커널이 부른다
   (console64_reap_closed). 닫기 버튼은 이 길로 바로 오지 않는다. */
void gui64_close_console(struct CONSOLE64 *con);

/* F11 -- 맨 아래 창을 위로. */
void gui64_raise_bottom_window(void);

/* 마우스와 창 관리 키(F11)를 먹는다. 처리했으면 1.
   커널 메인 루프와 raw 모드 키 대기 루프가 둘 다 먼저 부른다 -- 그래야
   앱이 콘솔을 쥐고 있는 동안에도 포인터가 살아 있다. */
int gui64_handle_system_event(const struct EVENT64 *event);

/* init_mouse64에 넘길 디코더. 소유자는 gui64다. */
struct MOUSE_DEC64 *gui64_mouse_dec(void);

#endif
