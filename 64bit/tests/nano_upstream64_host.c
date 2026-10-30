#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include "prototypes.h"
#include <syscall.h>
#include <utf864.h>

void *host_malloc64(size_t size) __asm__("malloc");
void host_free64(void *ptr) __asm__("free");
void *native_malloc64(size_t size);
void native_free64(void *ptr);
int nano_entry64(int argc, char **argv);

static jmp_buf exited64;
static int exit_status64, raw64, file_opens64, file_writes64;
static int allocations_before_failure64 = -1;
static unsigned rows64 = 25, cols64 = 80, generation64 = 1;
static int row64, col64, fg64, bg64, flushes64, checkpoints64;
static uint32_t cells64[32][100];
static size_t event_at64;
static uint64_t preedit_size64;
static void *preedit_undo64;

struct NANO_EVENT64 { unsigned int kind64, payload64, mods64; };
static const struct NANO_EVENT64 events64[] = {
	{ 2, 0x3131, 0 }, { 2, 0xd55c, 0 }, { 2, 0, 0 },
	{ 1, 0xd55c, 0 }, { 2, 0xae00, 0 }, { 1, 0xae00, 0 }, { 5, 1, 0 },
	{ 1, 'u', 4 }, { 5, 2, 0 }, { 1, 'e', 4 }, { 5, 3, 0 },
	{ 1, 23, 0 }, { 1, 0xd55c, 0 }, { 1, 10, 0 }, { 5, 4, 0 },
	{ 1, 28, 0 }, { 1, 0xd55c, 0 }, { 1, 10, 0 },
	{ 1, 0xac00, 0 }, { 1, 10, 0 }, { 1, 'y', 0 }, { 5, 5, 0 },
	{ 4, 0, 0 }, { 5, 6, 0 },
	{ 1, 15, 0 }, { 1, 'P', 0 }, { 1, '0', 0 }, { 1, '.', 0 },
	{ 1, 'T', 0 }, { 1, 'X', 0 }, { 1, 'T', 0 }, { 1, 10, 0 }, { 5, 7, 0 },
	{ 1, 24, 0 },
};

void *native_malloc64(size_t size)
{
	if (allocations_before_failure64 == 0) { allocations_before_failure64 = -1; return NULL; }
	if (allocations_before_failure64 > 0) { allocations_before_failure64--; }
	return host_malloc64(size);
}
void native_free64(void *ptr) { host_free64(ptr); }

static unsigned char source64[65537], saved64[65537];
static size_t source_size64, saved_size64, file_at64;
static int metadata_error64, read_error64, sync_error64, close_error64, write_error64;
static int source_exists64, saved_exists64, sync_calls64;
static int fstat_changes64, fstat_calls64, extra_read64;
static FILE *failed_close64;
int __real_nano_fclose64(FILE *stream);
int __wrap_nano_fclose64(FILE *stream);
int __wrap_nano_fclose64(FILE *stream)
{
	int result = __real_nano_fclose64(stream);

	if (close_error64 && result != 0) { failed_close64 = stream; }
	return result;
}
static void storage_boundaries64(void);

static long file_call_fixture64(long operation, long arg0, long arg1, long arg2)
{
	if (operation == FILE_STAT64 || operation == FILE_FSTAT64) {
		struct FILE_INFO64 info = { 0 };
		const char *path = (const char *) arg0;

		if (metadata_error64) { return -5; }
		if (operation == FILE_FSTAT64) {
			info.size64 = (uint32_t) source_size64;
			if (fstat_changes64 && ++fstat_calls64 == 2) { info.size64++; }
		}
		else if (strcmp(path, "/") == 0) { info.flags64 = FILE_DIRECTORY64; }
		else if (strcmp(path, "IN.TXT") == 0 && source_exists64) {
			info.size64 = (uint32_t) source_size64;
			info.date64 = (46 << 9) | (10 << 5) | 26;
			info.time64 = (12 << 11) | (34 << 5) | 28;
			info.identity64 = 0x200000020UL;
		} else if (strcmp(path, "P0.TXT") == 0 && saved_exists64) {
			info.size64 = (uint32_t) saved_size64;
		} else { return -2; }
		memcpy((void *) arg1, &info, sizeof(info)); return 0;
	}
	if (operation == FILE_OPEN64) {
		file_opens64++;
		if (metadata_error64) { return -5; }
		if (arg1 & FILE_WRITING64) {
			assert(strcmp((const char *) arg0, "P0.TXT") == 0);
			assert(arg1 == (FILE_CREATE64 | FILE_TRUNCATE64 | FILE_WRITING64));
			saved_size64 = 0; saved_exists64 = 1;
		} else {
			assert(strcmp((const char *) arg0, "IN.TXT") == 0);
			if (!source_exists64) { return -2; }
		}
		file_at64 = 0; return 3;
	}
	if (operation == FILE_READ64) {
		assert(arg0 == 3);
		if (read_error64 && file_at64 >= 3) { return read_error64 < 0 ? -5 : 0; }
		if (extra_read64 && file_at64 == source_size64) {
			*(char *) arg1 = 'x'; return 1;
		}
		size_t count = source_size64 - file_at64;

		if (count > (size_t) arg2) { count = (size_t) arg2; }
		if (count > 3) { count = 3; }
		memcpy((void *) arg1, source64 + file_at64, count);
		file_at64 += count; return (long) count;
	}
	if (operation == FILE_SYNC64) {
		assert(arg0 == 3 && arg1 == 0 && arg2 == 0);
		sync_calls64++; return sync_error64 ? -5 : 0;
	}
	assert(0); return -1;
}

static void checkpoint64(unsigned int point)
{
	static const char *const expected[] = { "", "한글", "", "한글", "한글", "가글", "가글", "가글" };

	assert(point >= 1 && point <= 7);
	assert(strcmp(openfile->filetop->data, expected[point]) == 0);
	if (point < 7) { assert(file_opens64 == 0 && file_writes64 == 0); }
	if (point == 1) {
		assert(cells64[1][0] == 0xd55c && cells64[1][1] == 0);
		assert(cells64[1][2] == 0xae00 && cells64[1][3] == 0);
	}
	if (point == 4) { assert(openfile->current_x == 0); }
	if (point == 6) { assert(LINES == 20 && COLS == 60); }
	if (point == 7) {
		assert(!openfile->modified && openfile->last_saved != NULL);
		assert(saved_size64 == 7 && memcmp(saved64, "가글\n", 7) == 0);
		assert(sync_calls64 == 1 && file_opens64 == 1);
	}
	checkpoints64++;
}

static unsigned long event64(void)
{
	if (event_at64 == 0) { storage_boundaries64(); }
	for (;;) {
		assert(event_at64 < sizeof(events64) / sizeof(events64[0]));
		struct NANO_EVENT64 event = events64[event_at64++];

		if (event.kind64 == 5) { checkpoint64(event.payload64); continue; }
		if (event.kind64 == 2) {
			if (event_at64 == 1 || events64[event_at64 - 2].kind64 != 2) {
				preedit_size64 = openfile->totsize;
				preedit_undo64 = openfile->undotop;
			} else {
				assert(openfile->totsize == preedit_size64);
				assert(openfile->undotop == preedit_undo64);
			}
		}
		if (event.kind64 == 4) {
			rows64 = 20; cols64 = 60; generation64++;
			memset(cells64, 0, sizeof(cells64));
		}
		return ((unsigned long) event.kind64 << 32) |
			((unsigned long) event.mods64 << 40) | event.payload64;
	}
}

static long tty64(long operation, long a1, long a2)
{
	switch (operation) {
	case TTY_MODE: raw64 = (int) a1; return 0;
	case TTY_SIZE: return (long) (((unsigned long) generation64 << 32) | (rows64 << 16) | cols64);
	case TTY_MOVE:
		assert(a1 >= 0 && a1 < (long) rows64 && a2 >= 0 && a2 < (long) cols64);
		row64 = (int) a1; col64 = (int) a2; return 0;
	case TTY_ATTR: fg64 = (int) a1; bg64 = (int) a2; return 0;
	case TTY_FLUSH: flushes64++; return 0;
	case TTY_READKEY: return (long) event64();
	default: assert(0); return -1;
	}
}
long syscall0(long nr) { assert(nr == SYS_TICKS); return 1234; }
long syscall1(long nr, long a0)
{
	if (nr == SYS_EXIT) { exit_status64 = (int) a0; longjmp(exited64, 1); }
	if (nr == SYS_TTY) { return tty64(a0, 0, 0); }
	if (nr == SYS_CLOSE) { assert(a0 >= 3); return close_error64 ? -1 : 0; }
	assert(0); return -1;
}
long syscall2(long nr, long a0, long a1)
{
	if (nr == SYS_TTY) { return tty64(a0, a1, 0); }
	if (nr == SYS_OPEN) { file_opens64++; return -2; }
	assert(0); return -1;
}
long syscall3(long nr, long a0, long a1, long a2)
{
	if (nr == SYS_TTY) { return tty64(a0, a1, a2); }
	if (nr == SYS_WRITE) {
		if (a0 >= 3) {
			file_writes64++;
			if (write_error64 == 1 || (write_error64 == 2 && saved_size64 >= 3)) { return 0; }
			size_t count = (size_t) a2 < 3 ? (size_t) a2 : 3;

			assert(count <= sizeof(saved64) - saved_size64);
			memcpy(saved64 + saved_size64, (void *) a1, count);
			saved_size64 += count; return (long) count;
		}
		assert(a0 == 1 || a0 == 2);
		if (!raw64) { return a2; }
		assert(fg64 >= 0 && bg64 >= 0);
		const char *text = (const char *) a1;
		size_t at = 0;

		while (at < (size_t) a2) {
			uint32_t scalar;
			size_t consumed;

			assert(utf8_decode64(text + at, (size_t) a2 - at, &scalar, &consumed) == 0);
			assert(row64 >= 0 && row64 < (int) rows64 && col64 >= 0 && col64 < (int) cols64);
			cells64[row64][col64++] = scalar;
			if ((scalar >= 0xac00 && scalar <= 0xd7a3) ||
				(scalar >= 0x3131 && scalar <= 0x3163)) {
				assert(col64 < (int) cols64); cells64[row64][col64++] = 0;
			}
			at += consumed;
		}
		return a2;
	}
	assert(0); return -1;
}
long syscall4(long nr, long a0, long a1, long a2, long a3)
{
	if (nr == SYS_FILE) { return file_call_fixture64(a0, a1, a2, a3); }
	return syscall3(nr, a0, a1, a2);
}
long syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
	(void) a4; return syscall4(nr, a0, a1, a2, a3);
}
long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
	(void) a5; return syscall5(nr, a0, a1, a2, a3, a4);
}

static void libc_boundaries64(void)
{
	char text[80], *end;

	assert(nano_snprintf64(text, sizeof(text), "%zi:%zu %04X %-5.3s", -23L, (size_t) 7, 0xac00, "abcdef") == 16);
	assert(strcmp(text, "-23:7 AC00 abc  ") == 0);
	assert(nano_snprintf64(text, 4, "%s", "한글") == 6);
	assert(memcmp(text, "한\0", 4) == 0);
	assert(nano_snprintf64(NULL, 0, "%lld", -9223372036854775807LL - 1) == 20);
	assert(nano_snprintf64(text, sizeof(text), "%hhd %hhu", 255, 257) == 4);
	assert(strcmp(text, "-1 1") == 0);
	/* Non-literal formats test explicit rejection without compiler diagnostics. */
	int (*volatile format)(char *, size_t, const char *, ...) = nano_snprintf64;
	assert(format(text, sizeof(text), "%99999999999999999s", "x") == -1 && errno == EOVERFLOW);
	assert(format(text, sizeof(text), "%n", &event_at64) == -1 && errno == EINVAL);
	assert(nano_strtol64("[", &end, 10) == 0 && *end == '[');
	assert(nano_strtol64("-9223372036854775808", &end, 10) == -__LONG_MAX__ - 1 && *end == 0);
	errno = 0;
	assert(nano_strtol64("9223372036854775808", &end, 10) == __LONG_MAX__ && errno == ERANGE);
	void *memory = nano_malloc64(7);

	assert(memory != NULL && ((uintptr_t) memory & 15) == 0);
	memcpy(memory, "abcdef", 7);
	allocations_before_failure64 = 0;
	assert(nano_realloc64(memory, 100) == NULL && memcmp(memory, "abcdef", 7) == 0);
	allocations_before_failure64 = -1;
	void *larger = nano_realloc64(memory, 30);

	assert(larger != NULL && memcmp(larger, "abcdef", 7) == 0);
	nano_free64(larger);
	nano_free64(larger);
	struct nano_stat64 info;

	memset(&info, 0xa5, sizeof(info));
	struct nano_stat64 previous = info;

	assert(nano_stat64("file", &info) == -1 && errno == ENOENT);
	assert(memcmp(&info, &previous, sizeof(info)) == 0);
	assert(nano_open64("file", O_CREAT | O_TRUNC | O_APPEND) == -1 && errno == ENOSYS);
	sync_error64 = 1;
	assert(nano_fsync64(3) == -1 && errno == EIO && file_opens64 == 0);
	sync_error64 = sync_calls64 = 0;
	assert(nano_regcomp64(NULL, "x", 0) == REG_BADPAT && errno == ENOSYS);
	assert(nano_iswalnum64(0xd55c) && !nano_iswalnum64('!'));
	assert(nano_towlower64(0xd55c) == 0xd55c && nano_towlower64('A') == 'a');
	char *args[] = { "nano", "-vx", "-T", "4", "--view", "--tabsize=8", "file" };
	const struct option options[] = {
		{ "view", 0, NULL, 'v' }, { "tabsize", 1, NULL, 'T' }, { NULL, 0, NULL, 0 },
	};
	nano_optind64 = 0;
	assert(nano_getopt_long64(7, args, "vxT:", options, NULL) == 'v');
	assert(nano_getopt_long64(7, args, "vxT:", options, NULL) == 'x');
	assert(nano_getopt_long64(7, args, "vxT:", options, NULL) == 'T');
	assert(strcmp(nano_optarg64, "4") == 0);
	assert(nano_getopt_long64(7, args, "vxT:", options, NULL) == 'v');
	assert(nano_getopt_long64(7, args, "vxT:", options, NULL) == 'T');
	assert(strcmp(nano_optarg64, "8") == 0);
	assert(nano_getopt_long64(7, args, "vxT:", options, NULL) == -1 && nano_optind64 == 6);
	nano_optind64 = 0;
}

static void storage_boundaries64(void)
{
	FILE *sentinel = (FILE *) (uintptr_t) 1, *stream;
	char *original = openfile->filetop->data;
	struct nano_stat64 info;

	assert(open_file("missing", TRUE, &sentinel) == 0 && sentinel == (FILE *) (uintptr_t) 1);
	metadata_error64 = 1;
	assert(open_file("missing", TRUE, &sentinel) == -1);
	assert(sentinel == (FILE *) (uintptr_t) 1 && file_opens64 == 0);
	metadata_error64 = 0;
	source_exists64 = 1; source_size64 = 7;
	memcpy(source64, "한글\n", 7);
	assert(stat("./IN.TXT", &info) == 0 && info.st_size == 7 && S_ISREG(info.st_mode));
	assert(info.st_ino == 0x200000020UL && info.st_mtime == 1793018096L);
	assert(realpath("../IN.TXT", NULL) == NULL && errno == ENOTDIR);
	for (int fault = -1; fault <= 1; fault += 2) {
		read_error64 = fault;
		assert(open_file("IN.TXT", TRUE, &sentinel) == -1);
		assert(sentinel == (FILE *) (uintptr_t) 1);
		assert(openfile->filetop->data == original && !openfile->modified);
	}
	read_error64 = 0;
	fstat_changes64 = 1;
	assert(open_file("IN.TXT", TRUE, &sentinel) == -1 && fstat_calls64 == 2);
	fstat_changes64 = 0;
	extra_read64 = 1;
	assert(open_file("IN.TXT", TRUE, &sentinel) == -1);
	extra_read64 = 0;
	allocations_before_failure64 = 0;
	assert(open_file("IN.TXT", TRUE, &sentinel) == -1);
	allocations_before_failure64 = -1;
	assert(open_file("/IN.TXT", TRUE, &stream) == 3);
	memset(source64, 'x', source_size64); /* Snapshot must own the original bytes. */
	read_file(stream, 3, "IN.TXT", FALSE);
	assert(strcmp(openfile->filetop->data, "한글") == 0);
	assert(openfile->filetop->next != NULL && openfile->filetop->next->data[0] == 0);
	/* Use this real loaded buffer for serialization/failure tests, then restore
	 * a fresh buffer before the interactive undo/search/replace scenario. */
	linestruct *loaded = openfile->filetop;
	char *loaded_data = loaded->data;
	linestruct *loaded_next = loaded->next;
	char *large = malloc(NANO_FILE_LIMIT64 + 2);

	assert(large != NULL);
	memset(large, 'a', NANO_FILE_LIMIT64 + 1); large[NANO_FILE_LIMIT64 + 1] = 0;
	loaded->data = large; loaded->next = NULL;
	int before = file_opens64;

	assert(!write_file("P0.TXT", NULL, OVERWRITE, TRUE) && file_opens64 == before);
	large[NANO_FILE_LIMIT64] = 0;
	assert(write_file("P0.TXT", NULL, OVERWRITE, TRUE) && saved_size64 == 65536);
	assert(saved64[0] == 'a' && saved64[65535] == 'a');
	loaded->next = loaded_next; loaded->data[65535] = 0; openfile->fmt = DOS_FILE;
	before = file_opens64;
	assert(!write_file("P0.TXT", NULL, OVERWRITE, TRUE) && file_opens64 == before);
	openfile->fmt = NIX_FILE;
	assert(write_file("P0.TXT", NULL, OVERWRITE, TRUE) && saved_size64 == 65536);
	assert(saved64[65534] == 'a' && saved64[65535] == '\n');
	loaded->data = loaded_data;
	for (int failure = 0; failure < 4; failure++) {
		set_modified();
		sync_error64 = failure == 0;
		write_error64 = failure == 1 ? 1 : (failure == 3 ? 2 : 0);
		close_error64 = failure == 2;
		void *last_saved = openfile->last_saved;
		assert(!write_file("P0.TXT", NULL, OVERWRITE, TRUE) && openfile->modified);
		assert(strcmp(loaded->data, "한글") == 0 && openfile->last_saved == last_saved);
		if (close_error64) {
			assert(failed_close64 != NULL && fileno(failed_close64) == 3);
			assert(fdopen(3, "wb") == NULL);
			close_error64 = 0;
			assert(fclose(failed_close64) == EOF && fileno(failed_close64) == -1);
		}
	}
	close_error64 = write_error64 = sync_error64 = 0;
	/* Reserve every output stream: preflight must fail before truncation. */
	FILE *held[NANO_STREAMS64];
	for (int i = 0; i < NANO_STREAMS64; i++) { held[i] = fdopen(3 + i, "wb"); assert(held[i]); }
	before = file_opens64;
	assert(!write_file("P0.TXT", NULL, OVERWRITE, TRUE) && file_opens64 == before);
	for (int i = 0; i < NANO_STREAMS64; i++) { assert(fclose(held[i]) == 0); }
	loaded->data = loaded_data; loaded->next = loaded_next;
	free(large);
	source_size64 = 65537;
	before = file_opens64;
	assert(open_file("IN.TXT", TRUE, &sentinel) == -1 && file_opens64 == before);
	source_size64 = 65536;
	assert(open_file("IN.TXT", TRUE, &stream) == 3 && fclose(stream) == 0);
	/* Free loaded nodes and reset the actual editor buffer to its initial state. */
	free_lines(openfile->filetop);
	openfile->filetop = make_new_node(NULL); openfile->filetop->data = copy_of("");
	openfile->filebot = openfile->current = openfile->edittop = openfile->filetop;
	openfile->totsize = 0; openfile->current_x = 0; openfile->modified = FALSE;
	free(openfile->filename); openfile->filename = copy_of("");
	free(openfile->statinfo); openfile->statinfo = NULL;
	openfile->last_saved = NULL;
	file_opens64 = file_writes64 = sync_calls64 = 0;
	saved_exists64 = 0; saved_size64 = 0;
}

static void curses_boundaries64(void)
{
	rows64 = 5; cols64 = 8;
	assert(nano_initscr64() != NULL && nano_raw64() == OK);
	WINDOW *window = nano_newwin64(2, 8, 1, 0);

	assert(window != NULL && nano_newwin64(-1, 8, 0, 0) == NULL);
	assert(nano_waddnstr64(window, "한A", -1) == OK);
	assert(nano_wrefresh64(window) == OK && cells64[1][0] == 0xd55c);
	assert(cells64[1][1] == 0 && cells64[1][2] == 'A');
	assert(nano_wmove64(window, 0, 1) == OK && nano_waddch64(window, 'X') == OK);
	assert(nano_wrefresh64(window) == OK);
	assert(cells64[1][0] == ' ' && cells64[1][1] == 'X');
	assert(nano_wmove64(window, 0, 7) == OK && nano_waddnstr64(window, "한", -1) == ERR);
	assert(nano_waddnstr64(window, "AB", -1) == ERR);
	assert(nano_wrefresh64(window) == OK && cells64[1][7] == 'A');
	assert(nano_wmove64(window, 1, 0) == OK);
	assert(nano_waddnstr64(window, "\xf0\x9f", 2) == OK);
	assert(nano_wrefresh64(window) == OK && cells64[2][0] == 0xfffd);
	assert(cells64[2][1] == 0xfffd);
	assert(nano_wscrl64(window, 1) == ERR);
	assert(nano_scrollok64(window, TRUE) == OK && nano_wscrl64(window, 1) == OK);
	assert(nano_wrefresh64(window) == OK && cells64[1][0] == 0xfffd);
	assert(cells64[2][0] == ' ');
	assert(nano_wscrl64(window, -1) == OK && nano_wrefresh64(window) == OK);
	assert(cells64[1][0] == ' ' && cells64[2][0] == 0xfffd);
	assert(nano_nodelay64(window, TRUE) == OK && nano_wgetch64(window) == ERR);
	assert(event_at64 == 0);
	assert(nano_ungetch64('q') == OK && nano_ungetch64('r') == ERR);
	assert(nano_wgetch64(window) == 'q' && event_at64 == 0);
	assert(nano_curs_set64(1) == ERR && errno == ENOSYS);
	assert(nano_halfdelay64(1) == ERR && nano_napms64(10) == ERR);
	assert(nano_endwin64() == OK && raw64 == 0);
	rows64 = 6; cols64 = 9; generation64++;
	allocations_before_failure64 = 0;
	assert(nano_wrefresh64(stdscr) == ERR && LINES == 5 && COLS == 8);
	allocations_before_failure64 = -1;
	assert(nano_wrefresh64(stdscr) == OK && LINES == 6 && COLS == 9);
	assert(nano_delwin64(window) == OK && nano_delwin64(window) == ERR);
	assert(nano_endwin64() == OK);
}

int main(int argc, char **arguments)
{
	(void) arguments;
	if (argc > 1) { curses_boundaries64(); return 0; }
	libc_boundaries64();
	char *argv[] = { "nano", NULL };

	if (setjmp(exited64) == 0) { nano_entry64(1, argv); assert(0); }
	assert(exit_status64 == 0 && raw64 == 0);
	assert(checkpoints64 == 7 && flushes64 > 10);
	assert(event_at64 == sizeof(events64) / sizeof(events64[0]));
	assert(file_opens64 == 1 && file_writes64 > 0);
	return 0;
}
