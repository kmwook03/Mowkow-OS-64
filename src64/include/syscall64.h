#ifndef MOWKOW64_SYSCALL64_H
#define MOWKOW64_SYSCALL64_H

#define SYS_EXIT   1
#define SYS_WRITE  2
#define SYS_READ   3
#define SYS_OPEN   4
#define SYS_CLOSE  5
#define SYS_SEEK   6
#define SYS_ALLOC  7
#define SYS_FREE   8
#define SYS_TICKS  9
#define SYS_TTY    10

/* SYS_OPEN 플래그 (따로 SYS_CREATE를 두지 않는다) */
#define O_CREAT 1
#define O_TRUNC 2

/*
 * SYS_TTY: 하나의 번호에 연산 코드를 실어 쓴다(roadmap64.md 결정 10).
 * rdi = 연산, rsi 이후가 인자. 커서 이동/지우기/속성 등 그리기 연산은
 * 아직 없다.
 */
#define TTY_MODE    0   /* rsi: 1이면 raw, 0이면 cooked */
#define TTY_READKEY 1   /* 키 이벤트 하나를 기다렸다가 묶어서 돌려준다 */
#define TTY_SIZE    2   /* 칸 단위 크기와 세대 값 */
#define TTY_MOVE    3   /* rsi=행, rdx=열 */
#define TTY_CLEAR   4   /* rsi=행, rdx=열, r10=행 수, r8=열 수 */
#define TTY_ATTR    5   /* rsi=글자색, rdx=배경색 (팔레트 인덱스) */
#define TTY_FLUSH   6   /* 모아 둔 갱신 영역을 화면에 올린다 */

/*
 * TTY_SIZE의 반환값. 칸 너비는 8픽셀이라 한글 한 글자가 두 칸이다.
 * 세대 값이 지난번과 다르면 크기가 바뀌고 화면이 지워졌다는 뜻이다.
 */
#define TTY_SIZE_COLS(v) ((unsigned int) ((v) & 0xffff))
#define TTY_SIZE_ROWS(v) ((unsigned int) (((v) >> 16) & 0xffff))
#define TTY_SIZE_GEN(v)  ((unsigned int) (((v) >> 32) & 0xffffffffUL))

/*
 * TTY_READKEY가 돌려주는 값의 구성.
 * payload는 CHAR/PREEDIT면 유니코드 코드포인트, KEY면 keyboard64.h의
 * KEY64_* 값이다. PREEDIT의 payload 0은 조합 중이던 글자가 사라졌다는 뜻.
 */
#define TTY_KEY_PAYLOAD(v) ((unsigned int) ((v) & 0xffffffffUL))
#define TTY_KEY_KIND(v)    ((unsigned int) (((v) >> 32) & 0xff))
#define TTY_KEY_MODS(v)    ((unsigned int) (((v) >> 40) & 0xff))

#define TTY_KIND_CHAR    1   /* 완성된 문자 */
#define TTY_KIND_PREEDIT 2   /* 조합 중인 한글 음절 */
#define TTY_KIND_KEY     3   /* 화살표 등 문자가 아닌 키 */
#define TTY_KIND_RESIZE  4   /* 크기가 바뀌고 화면이 지워졌다. 다시 그려라. */

#define TTY_MOD_SHIFT 0x01
#define TTY_MOD_CTRL  0x02
#define TTY_MOD_ALT   0x04

#endif
