/*
 * 나노 - GNU nano와 같은 키 배치를 쓰는 편집기 (roadmap64.md Phase 4 결정 8).
 * 업스트림 nano 소스가 아니라 여기서 쓴 코드다.
 *
 * 이 단계(step 5)는 편집 엔진과 화면이다: 파일 읽기, 줄 편집, 커서 이동,
 * 스크롤, 제목/상태/도움말 줄. 저장(^O)과 수정 확인 후 종료는 step 6이다.
 * 지금 ^X는 묻지 않고 그냥 나간다.
 *
 * 한글은 커널이 조합해서 준다(결정 11). 앱은 완성된 글자를 CHAR로 받고,
 * 조합 중인 음절은 PREEDIT로 받아 커서 자리에 직접 그린다.
 */
#include <mowos.h>

#define MAX_FILE   (64 * 1024)      /* step 7에서 실측 후 조정 */
#define LINE_MIN   32
#define STATUS_MAX 96

struct LINE {
	char *text;
	int len;
	int cap;
};

static struct LINE *lines;
static int nlines;
static int lines_cap;

static int cur_line;
static int cur_byte;                /* 현재 줄 안의 바이트 위치 */
static int top_line;                /* 화면 맨 위에 보이는 줄 */
static int left_col;                /* 가로 스크롤, 칸 단위 */
static int goal_col = -1;           /* 위/아래로 움직일 때 유지할 칸 */

static int rows;
static int cols;
static unsigned int preedit;
static int modified;
static const char *filename = "새 파일";
static int has_name;
static char status[STATUS_MAX];

/* ---------------------------------------------------------------- UTF-8 */

static int utf8_seq_len(unsigned char c)
{
	if (c < 0x80) {
		return 1;
	}
	if ((c & 0xe0) == 0xc0) {
		return 2;
	}
	if ((c & 0xf0) == 0xe0) {
		return 3;
	}
	return 1;
}

static unsigned int utf8_decode(const char *s, int *len)
{
	unsigned char a = (unsigned char) s[0];
	unsigned char b;
	unsigned char c;

	*len = utf8_seq_len(a);
	if (*len == 1) {
		return a;
	}
	b = (unsigned char) s[1];
	if (*len == 2) {
		return (unsigned int) ((a & 0x1f) << 6) | (b & 0x3f);
	}
	c = (unsigned char) s[2];
	return (unsigned int) ((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f);
}

static int utf8_encode(char *dst, unsigned int cp)
{
	if (cp < 0x80) {
		dst[0] = (char) cp;
		return 1;
	}
	if (cp < 0x800) {
		dst[0] = (char) (0xc0 | (cp >> 6));
		dst[1] = (char) (0x80 | (cp & 0x3f));
		return 2;
	}
	dst[0] = (char) (0xe0 | (cp >> 12));
	dst[1] = (char) (0x80 | ((cp >> 6) & 0x3f));
	dst[2] = (char) (0x80 | (cp & 0x3f));
	return 3;
}

/* 콘솔이 쓰는 폭과 반드시 같아야 한다 (console64.c의 put_utf8_char:
   한글 음절과 호환 자모만 16픽셀 = 두 칸, 나머지는 한 칸). */
static int cp_width(unsigned int cp)
{
	if ((cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0x3131 && cp <= 0x3163)) {
		return 2;
	}
	return 1;
}

/* ------------------------------------------------------------ 줄 저장소 */

static int line_reserve(struct LINE *ln, int need)
{
	char *bigger;
	int cap;
	int i;

	if (need <= ln->cap) {
		return 1;
	}
	cap = ln->cap != 0 ? ln->cap : LINE_MIN;
	while (cap < need) {
		cap *= 2;
	}
	bigger = malloc((size_t) cap);
	if (bigger == 0) {
		return 0;
	}
	for (i = 0; i < ln->len; i++) {
		bigger[i] = ln->text[i];
	}
	free(ln->text);
	ln->text = bigger;
	ln->cap = cap;
	return 1;
}

static int lines_reserve(int need)
{
	struct LINE *bigger;
	int cap;
	int i;

	if (need <= lines_cap) {
		return 1;
	}
	cap = lines_cap != 0 ? lines_cap : 64;
	while (cap < need) {
		cap *= 2;
	}
	bigger = malloc((size_t) cap * sizeof(struct LINE));
	if (bigger == 0) {
		return 0;
	}
	for (i = 0; i < nlines; i++) {
		bigger[i] = lines[i];
	}
	free(lines);
	lines = bigger;
	lines_cap = cap;
	return 1;
}

/* idx 자리에 빈 줄을 끼워 넣는다. */
static int line_insert(int idx)
{
	int i;

	if (lines_reserve(nlines + 1) == 0) {
		return 0;
	}
	for (i = nlines; i > idx; i--) {
		lines[i] = lines[i - 1];
	}
	lines[idx].text = 0;
	lines[idx].len = 0;
	lines[idx].cap = 0;
	nlines++;
	return 1;
}

static void line_remove(int idx)
{
	int i;

	free(lines[idx].text);
	for (i = idx; i < nlines - 1; i++) {
		lines[i] = lines[i + 1];
	}
	nlines--;
}

static int line_append(struct LINE *ln, const char *src, int n)
{
	int i;

	if (line_reserve(ln, ln->len + n) == 0) {
		return 0;
	}
	for (i = 0; i < n; i++) {
		ln->text[ln->len + i] = src[i];
	}
	ln->len += n;
	return 1;
}

/* 줄 안의 at 위치에 n바이트를 끼워 넣는다. */
static int line_insert_bytes(struct LINE *ln, int at, const char *src, int n)
{
	int i;

	if (line_reserve(ln, ln->len + n) == 0) {
		return 0;
	}
	for (i = ln->len - 1; i >= at; i--) {
		ln->text[i + n] = ln->text[i];
	}
	for (i = 0; i < n; i++) {
		ln->text[at + i] = src[i];
	}
	ln->len += n;
	return 1;
}

static void line_delete_bytes(struct LINE *ln, int at, int n)
{
	int i;

	for (i = at; i + n < ln->len; i++) {
		ln->text[i] = ln->text[i + n];
	}
	ln->len -= n;
}

/* -------------------------------------------------------------- 파일 읽기 */

static char load_buf[MAX_FILE];

static int load_file(const char *path)
{
	int fd;
	long n;
	int total;
	int start;
	int i;

	fd = open(path, 0);
	if (fd < 0) {
		return line_insert(0);          /* 없는 파일이면 새로 만든다 */
	}
	total = 0;
	for (;;) {
		n = read(fd, load_buf + total, (size_t) (MAX_FILE - total));
		if (n <= 0) {
			break;
		}
		total += (int) n;
		if (total >= MAX_FILE) {
			close(fd);
			return -1;                  /* 너무 크다 */
		}
	}
	close(fd);

	start = 0;
	for (i = 0; i <= total; i++) {
		if (i == total || load_buf[i] == '\n') {
			int len = i - start;

			if (len > 0 && load_buf[start + len - 1] == '\r') {
				len--;                  /* CRLF */
			}
			if (i == total && len == 0 && nlines > 0) {
				break;                  /* 끝의 개행 하나는 줄이 아니다 */
			}
			if (line_insert(nlines) == 0) {
				return 0;
			}
			if (len > 0 && line_append(&lines[nlines - 1],
					load_buf + start, len) == 0) {
				return 0;
			}
			start = i + 1;
		}
	}
	if (nlines == 0) {
		return line_insert(0);
	}
	return 1;
}

/* ------------------------------------------------------------- 칸 계산 */

/* 줄 머리부터 byte까지의 화면 칸 수. */
static int col_of_byte(const struct LINE *ln, int byte)
{
	int i;
	int col;
	int len;

	col = 0;
	i = 0;
	while (i < byte && i < ln->len) {
		col += cp_width(utf8_decode(ln->text + i, &len));
		i += len;
	}
	return col;
}

/* 목표 칸에 가장 가까운 바이트 위치. */
static int byte_of_col(const struct LINE *ln, int want)
{
	int i;
	int col;
	int len;

	col = 0;
	i = 0;
	while (i < ln->len) {
		int w = cp_width(utf8_decode(ln->text + i, &len));

		if (col + w > want) {
			break;
		}
		col += w;
		i += len;
	}
	return i;
}

static int prev_char_start(const struct LINE *ln, int byte)
{
	int i = byte - 1;

	while (i > 0 && ((unsigned char) ln->text[i] & 0xc0) == 0x80) {
		i--;
	}
	return i < 0 ? 0 : i;
}

/* --------------------------------------------------------------- 화면 */

static int text_rows(void)
{
	return rows - 4;                    /* 제목 1 + 상태 1 + 도움말 2 */
}

static void put(const char *s)
{
	write(1, s, strlen(s));
}

static void put_dec(int v)
{
	char buf[12];
	int i;

	if (v < 0) {
		put("-");
		v = -v;
	}
	if (v == 0) {
		put("0");
		return;
	}
	i = 0;
	while (v != 0 && i < 12) {
		buf[i++] = (char) ('0' + v % 10);
		v /= 10;
	}
	while (i > 0) {
		write(1, &buf[--i], 1);
	}
}

/* 칸 수로 잘라서 쓴다. 상태 줄이 화면보다 길면 줄이 넘어가면서 화면 전체가
   밀려 올라가므로, 넘칠 바에는 자른다. 쓴 칸 수를 돌려준다. */
static int put_clipped(const char *s, int max_cols)
{
	int i;
	int used;
	int len;

	i = 0;
	used = 0;
	while (s[i] != '\0') {
		int w = cp_width(utf8_decode(s + i, &len));

		if (used + w > max_cols) {
			break;
		}
		write(1, s + i, (size_t) len);
		used += w;
		i += len;
	}
	return used;
}

static void pad_to(int used, int width)
{
	while (used < width) {
		put(" ");
		used++;
	}
}

/* 한 줄을 left_col부터 화면 폭만큼 그린다. 경계에 걸친 두 칸짜리 글자는
   반쪽만 그릴 수 없으므로 공백으로 대신한다. */
static void draw_line(int screen_row, int idx)
{
	const struct LINE *ln;
	int i;
	int col;
	int drawn;
	int len;

	tty_move(screen_row, 0);
	if (idx >= nlines) {
		return;
	}
	ln = &lines[idx];
	col = 0;
	drawn = 0;
	i = 0;
	while (i < ln->len && drawn < cols) {
		unsigned int cp = utf8_decode(ln->text + i, &len);
		int w = cp_width(cp);

		if (col + w <= left_col) {
			col += w;
			i += len;
			continue;
		}
		if (col < left_col || drawn + w > cols) {
			put(" ");                   /* 잘린 두 칸짜리 글자 자리 */
			drawn++;
			col += w;
			i += len;
			continue;
		}
		write(1, ln->text + i, (size_t) len);
		drawn += w;
		col += w;
		i += len;
	}
}

static void draw_title(void)
{
	int used;

	tty_move(0, 0);
	tty_attr(0, 7);
	put(" 나노  ");
	used = 7;
	put(filename);
	used += (int) strlen(filename);
	if (modified != 0) {
		put("  *");
		used += 3;
	}
	pad_to(used, cols - 1);
	tty_attr(7, 0);
}

static void draw_status(void)
{
	int used;

	tty_move(rows - 3, 0);
	tty_clear(rows - 3, 0, 1, cols);
	if (status[0] != '\0') {
		tty_attr(0, 7);
		put(" ");
		used = 1 + put_clipped(status, cols - 2);
		pad_to(used, cols - 1);
		tty_attr(7, 0);
		return;
	}
	put("  줄 ");
	put_dec(cur_line + 1);
	put("/");
	put_dec(nlines);
	put("  칸 ");
	put_dec(col_of_byte(&lines[cur_line], cur_byte) + 1);
}

static void draw_help(void)
{
	tty_move(rows - 2, 0);
	tty_attr(0, 7);
	put(" ^O ");
	tty_attr(7, 0);
	put("저장   ");
	tty_attr(0, 7);
	put(" ^X ");
	tty_attr(7, 0);
	put("나가기   ");
	tty_attr(0, 7);
	put(" ^A ");
	tty_attr(7, 0);
	put("줄 처음   ");
	tty_attr(0, 7);
	put(" ^E ");
	tty_attr(7, 0);
	put("줄 끝");

	tty_move(rows - 1, 0);
	put(" 화살표 이동   PgUp/PgDn 쪽 넘기기   Shift+Space 한/영");
}

/* 커서가 화면 밖으로 나가지 않게 보이는 범위를 옮긴다. */
static void scroll_into_view(void)
{
	int col;

	if (cur_line < top_line) {
		top_line = cur_line;
	}
	if (cur_line >= top_line + text_rows()) {
		top_line = cur_line - text_rows() + 1;
	}
	if (top_line < 0) {
		top_line = 0;
	}
	col = col_of_byte(&lines[cur_line], cur_byte);
	if (col < left_col) {
		left_col = col;
	}
	if (col >= left_col + cols - 1) {
		left_col = col - cols + 2;
	}
	if (left_col < 0) {
		left_col = 0;
	}
}

static void draw_all(void)
{
	int r;

	scroll_into_view();
	/* ponytail: 키마다 화면 전체를 다시 그린다. 100x37이면 한 번에 3700칸이라
	   느려지면 바뀐 줄만 그리도록 좁히면 된다. 지금은 단순함이 먼저다. */
	tty_clear(0, 0, rows, cols);
	draw_title();
	for (r = 0; r < text_rows(); r++) {
		draw_line(r + 1, top_line + r);
	}
	draw_status();
	draw_help();

	/* 커서 자리: 조합 중인 음절이 있으면 거기에 그려 준다. */
	tty_move(cur_line - top_line + 1,
		col_of_byte(&lines[cur_line], cur_byte) - left_col);
	if (preedit != 0) {
		char buf[4];
		int n = utf8_encode(buf, preedit);

		tty_attr(2, 0);
		write(1, buf, (size_t) n);
		tty_attr(7, 0);
	}
	tty_flush();
}

/* --------------------------------------------------------------- 편집 */

static void set_status(const char *s)
{
	int i;

	for (i = 0; i < STATUS_MAX - 1 && s[i] != '\0'; i++) {
		status[i] = s[i];
	}
	status[i] = '\0';
}

static void insert_cp(unsigned int cp)
{
	char buf[4];
	int n;

	n = utf8_encode(buf, cp);
	if (line_insert_bytes(&lines[cur_line], cur_byte, buf, n) == 0) {
		set_status("메모리가 부족합니다");
		return;
	}
	cur_byte += n;
	modified = 1;
	goal_col = -1;
}

static void insert_newline(void)
{
	struct LINE *ln;
	int tail;

	if (line_insert(cur_line + 1) == 0) {
		set_status("메모리가 부족합니다");
		return;
	}
	ln = &lines[cur_line];
	tail = ln->len - cur_byte;
	if (tail > 0) {
		if (line_append(&lines[cur_line + 1], ln->text + cur_byte, tail) == 0) {
			set_status("메모리가 부족합니다");
			return;
		}
		ln->len = cur_byte;
	}
	cur_line++;
	cur_byte = 0;
	modified = 1;
	goal_col = -1;
}

static void do_backspace(void)
{
	int start;
	int prev_len;

	if (cur_byte > 0) {
		start = prev_char_start(&lines[cur_line], cur_byte);
		line_delete_bytes(&lines[cur_line], start, cur_byte - start);
		cur_byte = start;
		modified = 1;
	} else if (cur_line > 0) {
		prev_len = lines[cur_line - 1].len;
		if (lines[cur_line].len > 0 &&
				line_append(&lines[cur_line - 1], lines[cur_line].text,
					lines[cur_line].len) == 0) {
			set_status("메모리가 부족합니다");
			return;
		}
		line_remove(cur_line);
		cur_line--;
		cur_byte = prev_len;
		modified = 1;
	}
	goal_col = -1;
}

static void do_delete(void)
{
	struct LINE *ln;
	int len;

	ln = &lines[cur_line];
	if (cur_byte < ln->len) {
		utf8_decode(ln->text + cur_byte, &len);
		line_delete_bytes(ln, cur_byte, len);
		modified = 1;
	} else if (cur_line + 1 < nlines) {
		if (lines[cur_line + 1].len > 0 &&
				line_append(ln, lines[cur_line + 1].text,
					lines[cur_line + 1].len) == 0) {
			set_status("메모리가 부족합니다");
			return;
		}
		line_remove(cur_line + 1);
		modified = 1;
	}
	goal_col = -1;
}

static void move_vertical(int delta)
{
	int want;

	if (goal_col < 0) {
		goal_col = col_of_byte(&lines[cur_line], cur_byte);
	}
	want = goal_col;
	cur_line += delta;
	if (cur_line < 0) {
		cur_line = 0;
	}
	if (cur_line >= nlines) {
		cur_line = nlines - 1;
	}
	cur_byte = byte_of_col(&lines[cur_line], want);
}

static void move_left(void)
{
	if (cur_byte > 0) {
		cur_byte = prev_char_start(&lines[cur_line], cur_byte);
	} else if (cur_line > 0) {
		cur_line--;
		cur_byte = lines[cur_line].len;
	}
	goal_col = -1;
}

static void move_right(void)
{
	int len;

	if (cur_byte < lines[cur_line].len) {
		utf8_decode(lines[cur_line].text + cur_byte, &len);
		cur_byte += len;
	} else if (cur_line + 1 < nlines) {
		cur_line++;
		cur_byte = 0;
	}
	goal_col = -1;
}

/* --------------------------------------------------------------- 저장 */

static void refresh_size(void);

/*
 * 제자리 저장이다: O_TRUNC로 잘라 내고 그 위에 쓴다 (roadmap64.md 결정 12).
 * fd64에는 rename도 unlink도 없어서 임시 파일 방식을 쓸 수 없다. 쓰는 도중에
 * 죽으면 원본이 사라진다는 뜻이고, 이 단계에서는 그 값을 치르기로 했다.
 *
 * 줄마다 write하지 않고 통째로 모아 한 번에 쓴다. 커널의 fd64_write는 호출
 * 끝에서 매번 fd64_sync를 돌리므로(fd64.c), 줄 단위로 쓰면 줄 수만큼 디스크
 * 동기화가 일어난다.
 */
static int save_file(void)
{
	int fd;
	int total;
	int i;
	int j;
	long n;

	if (has_name == 0) {
		set_status("이름이 없습니다 - 나노 <파일이름>으로 여세요");
		return 0;
	}
	total = 0;
	for (i = 0; i < nlines; i++) {
		if (total + lines[i].len + 1 > MAX_FILE) {
			set_status("너무 커서 저장할 수 없습니다");
			return 0;
		}
		for (j = 0; j < lines[i].len; j++) {
			load_buf[total++] = lines[i].text[j];
		}
		load_buf[total++] = '\n';
	}

	fd = open(filename, O_CREAT | O_TRUNC);
	if (fd < 0) {
		set_status("파일을 열 수 없습니다");
		return 0;
	}
	n = write(fd, load_buf, (size_t) total);
	close(fd);
	if (n != (long) total) {
		set_status("쓰기에 실패했습니다");
		return 0;
	}
	modified = 0;
	set_status("저장했습니다");
	return 1;
}

/*
 * 예/아니오 물음. 한글 모드에서도 동작해야 하므로 Enter/Esc/^C를 쓴다 -
 * 이 셋은 커널의 raw_process_key에서 오토마타를 거치지 않는다. 영어 모드에서는
 * nano처럼 y/n도 받는다.
 * 1 = 예, 0 = 아니오, -1 = 취소.
 */
static int prompt_yesno(const char *msg)
{
	unsigned long ev;
	unsigned int payload;

	set_status(msg);
	draw_all();
	for (;;) {
		ev = tty_readkey();
		if (TTY_KEY_KIND(ev) == TTY_KIND_RESIZE) {
			refresh_size();
			draw_all();
			continue;
		}
		if (TTY_KEY_KIND(ev) != TTY_KIND_CHAR) {
			continue;               /* 조합 중인 한글 등은 무시 */
		}
		payload = TTY_KEY_PAYLOAD(ev);
		if (payload == '\n' || payload == 'y' || payload == 'Y') {
			return 1;
		}
		if (payload == 0x1b || payload == 'n' || payload == 'N') {
			return 0;
		}
		if (payload == 3 || payload == 24) {    /* ^C, ^X */
			return -1;
		}
	}
}

static void refresh_size(void)
{
	unsigned long s = tty_size();

	cols = (int) TTY_SIZE_COLS(s);
	rows = (int) TTY_SIZE_ROWS(s);
}

int main(int argc, char **argv)
{
	unsigned long ev;
	unsigned int kind;
	unsigned int payload;
	int loaded;

	if (argc >= 2) {
		filename = argv[1];
		has_name = 1;
	}
	loaded = load_file(argc >= 2 ? argv[1] : "");
	if (loaded < 0) {
		puts("나노: 파일이 너무 큽니다 (최대 64 KiB)");
		return 1;
	}
	if (loaded == 0 || nlines == 0) {
		puts("나노: 메모리가 부족합니다");
		return 1;
	}

	refresh_size();
	if (rows < 6 || cols < 20) {
		puts("나노: 화면이 너무 작습니다");
		return 1;
	}
	tty_raw(1);
	draw_all();

	for (;;) {
		ev = tty_readkey();
		kind = TTY_KEY_KIND(ev);
		payload = TTY_KEY_PAYLOAD(ev);

		if (kind == TTY_KIND_RESIZE) {
			refresh_size();
			preedit = 0;
			draw_all();
			continue;
		}
		if (kind == TTY_KIND_PREEDIT) {
			preedit = payload;
			draw_all();
			continue;
		}
		if (kind == TTY_KIND_KEY) {
			switch (payload) {
			case KEY_UP:    move_vertical(-1); break;
			case KEY_DOWN:  move_vertical(1); break;
			case KEY_LEFT:  move_left(); break;
			case KEY_RIGHT: move_right(); break;
			case KEY_HOME:  cur_byte = 0; goal_col = -1; break;
			case KEY_END:   cur_byte = lines[cur_line].len; goal_col = -1; break;
			case KEY_PGUP:  move_vertical(-text_rows()); break;
			case KEY_PGDN:  move_vertical(text_rows()); break;
			case KEY_DELETE: do_delete(); break;
			default: break;
			}
			status[0] = '\0';
			draw_all();
			continue;
		}
		if (kind != TTY_KIND_CHAR) {
			continue;
		}

		preedit = 0;
		status[0] = '\0';
		if (payload == 24) {            /* ^X */
			int answer;

			if (modified == 0) {
				break;
			}
			answer = prompt_yesno("바뀐 내용을 저장할까요?  Enter/y 예   Esc/n 아니오   ^C 취소");
			if (answer < 0) {
				status[0] = '\0';
				draw_all();
				continue;
			}
			if (answer == 0) {
				break;                  /* 버리고 나간다 */
			}
			if (save_file() == 0) {
				draw_all();             /* 저장 실패 - 나가지 않는다 */
				continue;
			}
			break;
		}
		if (payload == 15) {            /* ^O */
			save_file();
		} else if (payload == 1) {      /* ^A */
			cur_byte = 0;
			goal_col = -1;
		} else if (payload == 5) {      /* ^E */
			cur_byte = lines[cur_line].len;
			goal_col = -1;
		} else if (payload == '\n') {
			insert_newline();
		} else if (payload == '\b') {
			do_backspace();
		} else if (payload == '\t') {
			insert_cp(' ');
			insert_cp(' ');
		} else if (payload >= 0x20) {
			insert_cp(payload);
		}
		draw_all();
	}

	/* raw 모드 해제와 화면 정리는 커널이 한다 (console64_set_raw). */
	return 0;
}
