/*
 * raw 모드 + 그리기 연산 확인용.
 *
 * 화면을 직접 그린다: 반전색 제목 줄, 마지막 이벤트, 조합 중인 한글, 아래
 * 상태 줄. 그린 내용은 tty_flush()에서야 화면에 올라간다(curses의 doupdate와
 * 같은 계약). ^X로 끝낸다. 화면 크기가 바뀌면 TTY_KIND_RESIZE를 받고 다시
 * 그린다.
 */
#include <mowos.h>

static void put_dec(unsigned int v)
{
	char buf[10];
	int i;

	if (v == 0) {
		write(1, "0", 1);
		return;
	}
	i = 0;
	while (v != 0 && i < 10) {
		buf[i++] = (char) ('0' + v % 10);
		v /= 10;
	}
	while (i > 0) {
		write(1, &buf[--i], 1);
	}
}

static void put_hex(unsigned int v)
{
	static const char digits[] = "0123456789abcdef";
	char buf[8];
	int i;

	for (i = 0; i < 8; i++) {
		buf[7 - i] = digits[v & 0xf];
		v >>= 4;
	}
	write(1, buf, 8);
}

static void put_utf8(unsigned int cp)
{
	char b[3];

	if (cp < 0x80) {
		b[0] = (char) cp;
		write(1, b, 1);
	} else if (cp < 0x800) {
		b[0] = (char) (0xc0 | (cp >> 6));
		b[1] = (char) (0x80 | (cp & 0x3f));
		write(1, b, 2);
	} else {
		b[0] = (char) (0xe0 | (cp >> 12));
		b[1] = (char) (0x80 | ((cp >> 6) & 0x3f));
		b[2] = (char) (0x80 | (cp & 0x3f));
		write(1, b, 3);
	}
}

static void put_spaces(int n)
{
	while (n-- > 0) {
		write(1, " ", 1);
	}
}

static void draw(unsigned long ev, unsigned int preedit)
{
	unsigned long s;
	unsigned int rows;
	unsigned int cols;
	unsigned int kind;

	s = tty_size();
	cols = TTY_SIZE_COLS(s);
	rows = TTY_SIZE_ROWS(s);

	tty_clear(0, 0, (int) rows, (int) cols);

	/* 제목 줄: 반전색. 마지막 칸은 비워 둔다 - 거기까지 쓰면 줄이 넘어간다. */
	tty_attr(0, 7);
	tty_move(0, 0);
	write(1, " ktest ", 7);
	put_dec(cols);
	write(1, "x", 1);
	put_dec(rows);
	write(1, " gen ", 5);
	put_dec(TTY_SIZE_GEN(s));
	put_spaces((int) cols - 30);

	tty_attr(7, 0);
	tty_move(2, 2);
	kind = TTY_KEY_KIND(ev);
	if (kind == TTY_KIND_CHAR) {
		write(1, "CHAR ", 5);
	} else if (kind == TTY_KIND_KEY) {
		write(1, "KEY  ", 5);
	} else if (kind == TTY_KIND_PREEDIT) {
		write(1, "PRE  ", 5);
	} else {
		write(1, "---  ", 5);
	}
	put_hex(TTY_KEY_PAYLOAD(ev));
	write(1, " mod ", 5);
	put_hex(TTY_KEY_MODS(ev));

	tty_move(4, 2);
	write(1, "composing: ", 11);
	if (preedit != 0) {
		tty_attr(2, 0);
		put_utf8(preedit);
		tty_attr(7, 0);
	} else {
		write(1, "-", 1);
	}

	tty_move((int) rows - 1, 0);
	tty_attr(0, 7);
	write(1, " ^X quit ", 9);
	put_spaces((int) cols - 10);
	tty_attr(7, 0);

	tty_flush();
}

int main(int argc, char **argv)
{
	unsigned long ev;
	unsigned int preedit;

	(void) argc;
	(void) argv;
	tty_raw(1);
	preedit = 0;
	draw(0, 0);
	for (;;) {
		ev = tty_readkey();
		if (TTY_KEY_KIND(ev) == TTY_KIND_CHAR &&
				TTY_KEY_PAYLOAD(ev) == 24) {        /* ^X */
			break;
		}
		if (TTY_KEY_KIND(ev) == TTY_KIND_PREEDIT) {
			preedit = TTY_KEY_PAYLOAD(ev);
		} else if (TTY_KEY_KIND(ev) == TTY_KIND_CHAR) {
			preedit = 0;
		}
		draw(ev, preedit);
	}
	/* 일부러 raw를 끄지 않고 나간다. 커널이 되돌리는지 보려는 것. */
	return 0;
}
