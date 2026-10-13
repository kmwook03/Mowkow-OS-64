#include <arch/arch64.h>
#include <console64.h>
#include <fd64.h>
#include <interrupt64.h>
#include <process64.h>
#include <stdio.h>
#include <string.h>
#include <syscall64.h>
#include <timer64.h>

#define ERROR64 ((uint64_t) -1)
#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "syscall check failed at line %d\n", __LINE__); \
		return 1; \
	} \
} while (0)

struct TIMERCTL64 timerctl64;
static struct PROCESS64 process;
static struct PROCESS64 *current;
static int console_token;
_Alignas(16) static uint8_t user_buffer[512];
static uint64_t last_action;
static int abi_error;
static int open_result;
static int create_result;
static int truncate_result;
static int seek_result;
static uint32_t open_calls;
static uint32_t create_calls;
static uint32_t truncate_calls;
static uint32_t read_calls;
static uint32_t write_calls;
static uint32_t seek_calls;
static int64_t last_offset;
static int last_whence;
static size_t io_result;
static int64_t console_read_result;
static uint32_t console_reads;
static uint32_t console_writes;
static int raw_mode;
static uint32_t mode_calls;
static uint64_t screen_size;
static uint64_t key_event;
static uint32_t move_calls;
static uint32_t clear_calls;
static uint32_t attr_calls;
static uint32_t flush_calls;
static uint32_t last_row;
static uint32_t last_col;
static uint32_t last_rows;
static uint32_t last_cols;
static uint8_t last_fg;
static uint8_t last_bg;
static size_t user_limit;
static int mutate_user_name;
static int name_error;
static char opened_name[FD64_NAME_MAX];
static char created_name[FD64_NAME_MAX];
static uintptr_t opened_name_pointer;
static uintptr_t created_name_pointer;

struct PROCESS64 *process64_current(void)
{
	return current;
}

void process64_exit_current(int status)
{
	process.exited = 1;
	process.exit_status = status;
}

int process64_user_range_valid(const void *ptr, size_t size)
{
	uintptr_t p = (uintptr_t) ptr;
	uintptr_t base = (uintptr_t) user_buffer;

	return ptr != NULL && p >= base && p - base <= user_limit &&
		size <= user_limit - (p - base);
}

static void capture_name64(char *dst, const char *name)
{
	size_t i;

	if (process64_user_range_valid(name, 1) != 0) {
		name_error = 1;
	}
	for (i = 0; i < FD64_NAME_MAX; i++) {
		dst[i] = name[i];
		if (dst[i] == '\0') {
			return;
		}
	}
	name_error = 1;
	dst[FD64_NAME_MAX - 1] = '\0';
}

int fd64_open(struct FDHANDLE64 *fh, const char *name)
{
	capture_name64(opened_name, name);
	opened_name_pointer = (uintptr_t) name;
	open_calls++;
	fh->pos = 0;
	/* Simulate a task changing its path while fd64_open waits on the mutex. */
	if (mutate_user_name != 0) {
		memset(user_buffer, 'x', FD64_NAME_MAX);
	}
	return open_result;
}

int fd64_create(struct FDHANDLE64 *fh, const char *name)
{
	(void) fh;
	capture_name64(created_name, name);
	created_name_pointer = (uintptr_t) name;
	create_calls++;
	return create_result;
}

int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size)
{
	(void) fh;
	if (size != 0) {
		abi_error = 1;
	}
	truncate_calls++;
	return truncate_result;
}

size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t size)
{
	(void) fh;
	(void) dst;
	read_calls++;
	return io_result < size ? io_result : size;
}

size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size)
{
	(void) fh;
	(void) src;
	write_calls++;
	return io_result < size ? io_result : size;
}

int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence)
{
	(void) fh;
	seek_calls++;
	last_offset = offset;
	last_whence = whence;
	return seek_result;
}

void console64_write_con(struct CONSOLE64 *con, const char *src, uint64_t len)
{
	(void) src;
	(void) len;
	if (con != process.console) {
		abi_error = 1;
	}
	console_writes++;
}

uint64_t console64_read_con(struct CONSOLE64 *con, char *dst, uint64_t len)
{
	(void) dst;
	(void) len;
	if (con != process.console) {
		abi_error = 1;
	}
	console_reads++;
	return (uint64_t) console_read_result;
}

void console64_set_raw(int on)
{
	mode_calls++;
	raw_mode = on;
}

int console64_is_raw(void)
{
	return raw_mode;
}

uint64_t console64_read_key(void)
{
	return key_event;
}

uint64_t console64_size(void)
{
	return screen_size;
}

void console64_move(uint32_t row, uint32_t col)
{
	move_calls++;
	last_row = row;
	last_col = col;
}

void console64_clear_cells(uint32_t row, uint32_t col, uint32_t rows, uint32_t cols)
{
	clear_calls++;
	last_row = row;
	last_col = col;
	last_rows = rows;
	last_cols = cols;
}

void console64_set_attr(uint8_t fg, uint8_t bg)
{
	attr_calls++;
	last_fg = fg;
	last_bg = bg;
}

void console64_flush(void)
{
	flush_calls++;
}

uint64_t arch64_timer_ticks(void)
{
	return timerctl64.count;
}

static uint64_t call64(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
	uint64_t a3, uint64_t a4)
{
	struct INTERRUPT_FRAME64 frame = {
		.rax = nr, .rdi = a0, .rsi = a1, .rdx = a2,
		.r10 = a3, .r8 = a4, .r9 = 0x12345678,
	};
	struct INTERRUPT_FRAME64 original = frame;

	last_action = syscall_handler64(&frame);
	original.rax = frame.rax;
	if (memcmp(&frame, &original, sizeof frame) != 0) {
		abi_error = 1;
	}
	return frame.rax;
}

static void reset64(void)
{
	memset(&process, 0, sizeof process);
	memset(user_buffer, 0, sizeof user_buffer);
	memcpy(user_buffer, "app.txt", 8);
	current = &process;
	user_limit = sizeof user_buffer;
	mutate_user_name = 0;
	name_error = 0;
	opened_name[0] = created_name[0] = '\0';
	opened_name_pointer = created_name_pointer = 0;
	process.console = (struct CONSOLE64 *) (void *) &console_token;
	process.heap.base = (uintptr_t) user_buffer;
	process.heap.size = sizeof user_buffer;
	process.heap_next = process.heap.base;
	open_result = 1;
	create_result = 1;
	truncate_result = 0;
	seek_result = 0;
	open_calls = create_calls = truncate_calls = 0;
	read_calls = write_calls = seek_calls = 0;
	console_reads = console_writes = 0;
	io_result = 3;
	console_read_result = 3;
	raw_mode = 0;
	mode_calls = move_calls = clear_calls = attr_calls = flush_calls = 0;
	screen_size = 80U | (25ULL << 16) | (0x87654321ULL << 32);
	key_event = 0x0101000041ULL;
}

static int test_open64(void)
{
	uint64_t path = (uintptr_t) user_buffer;
	uint64_t flags;
	size_t i;
	uint64_t invalid[] = {4, 1ULL << 32, (1ULL << 32) | O_CREAT, UINT64_MAX};

	for (flags = 0; flags <= (O_CREAT | O_TRUNC); flags++) {
		reset64();
		CHECK64(call64(SYS_OPEN, path, flags, 0, 0, 0) == 3);
		CHECK64(process.files[3].used == 1 && open_calls == 1);
		CHECK64(create_calls == 0);
		CHECK64(truncate_calls == ((flags & O_TRUNC) != 0));
		CHECK64(name_error == 0 && strcmp(opened_name, "app.txt") == 0);
	}
	for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
		reset64();
		CHECK64(call64(SYS_OPEN, path, invalid[i], 0, 0, 0) == ERROR64);
		CHECK64(open_calls == 0 && create_calls == 0 && truncate_calls == 0);
		CHECK64(process.files[3].used == 0);
	}
	reset64();
	open_result = 0;
	CHECK64(call64(SYS_OPEN, path, 0, 0, 0, 0) == (uint64_t) -2);
	CHECK64(call64(SYS_OPEN, path, O_CREAT, 0, 0, 0) == 3);
	CHECK64(create_calls == 1 && truncate_calls == 0);
	reset64();
	open_result = create_result = 0;
	CHECK64(call64(SYS_OPEN, path, O_CREAT, 0, 0, 0) == (uint64_t) -2);
	CHECK64(process.files[3].used == 0);
	reset64();
	truncate_result = -1;
	CHECK64(call64(SYS_OPEN, path, O_TRUNC, 0, 0, 0) == (uint64_t) -2);
	CHECK64(process.files[3].used == 0);
	reset64();
	for (i = 3; i < PROCESS64_MAX_FILES; i++) {
		process.files[i].used = 1;
	}
	CHECK64(call64(SYS_OPEN, path, 0, 0, 0, 0) == (uint64_t) -3);
	CHECK64(open_calls == 0);
	reset64();
	memset(user_buffer, 'x', FD64_NAME_MAX);
	CHECK64(call64(SYS_OPEN, path, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_OPEN, 0, 0, 0, 0, 0) == ERROR64);
	CHECK64(open_calls == 0);
	return 0;
}

static int test_name_snapshot64(void)
{
	uint64_t path = (uintptr_t) user_buffer;
	size_t i;
	static const char *const invalid_utf8[] = {
		"\x80", "\xc0\x80", "\xc2", "\xe2\x82", "\xf0\x9f\x92",
		"\xe2(\xa1", "\xe0\x80\x80", "\xed\xa0\x80",
		"\xf0\x80\x80\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
	};
	static const char *const valid_utf8[] = {
		"한글.txt", "\xc2\x80", "\xe0\xa0\x80", "\xed\x9f\xbf",
		"\xee\x80\x80", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf",
	};

	reset64();
	open_result = 0;
	mutate_user_name = 1;
	CHECK64(call64(SYS_OPEN, path, O_CREAT, 0, 0, 0) == 3);
	CHECK64(open_calls == 1 && create_calls == 1 && name_error == 0);
	CHECK64(strcmp(opened_name, "app.txt") == 0);
	CHECK64(strcmp(created_name, "app.txt") == 0);
	CHECK64(opened_name_pointer == created_name_pointer);
	CHECK64(opened_name_pointer != (uintptr_t) user_buffer);
	CHECK64(user_buffer[0] == 'x' && user_buffer[7] == 'x');
	reset64();
	mutate_user_name = 1;
	CHECK64(call64(SYS_OPEN, path, O_TRUNC, 0, 0, 0) == 3);
	CHECK64(truncate_calls == 1 && name_error == 0);
	CHECK64(strcmp(opened_name, "app.txt") == 0);
	reset64();
	memset(user_buffer, 'x', FD64_NAME_MAX - 1);
	user_buffer[FD64_NAME_MAX - 1] = 0;
	user_limit = FD64_NAME_MAX;
	CHECK64(call64(SYS_OPEN, path, 0, 0, 0, 0) == 3);
	CHECK64(strlen(opened_name) == FD64_NAME_MAX - 1 && name_error == 0);
	reset64();
	user_limit = 8;
	CHECK64(call64(SYS_OPEN, path, 0, 0, 0, 0) == 3);
	CHECK64(strcmp(opened_name, "app.txt") == 0 && name_error == 0);
	reset64();
	user_limit = 7;
	CHECK64(call64(SYS_OPEN, path, O_CREAT, 0, 0, 0) == ERROR64);
	CHECK64(open_calls == 0 && create_calls == 0);
	reset64();
	CHECK64(call64(SYS_OPEN, UINTPTR_MAX, O_CREAT, 0, 0, 0) == ERROR64);
	CHECK64(open_calls == 0 && create_calls == 0);
	/* A lead byte followed by NUL at the range end must not reach UTF-16. */
	reset64();
	user_buffer[sizeof user_buffer - 2] = 0xe2;
	user_buffer[sizeof user_buffer - 1] = 0;
	CHECK64(call64(SYS_OPEN, path + sizeof user_buffer - 2,
		O_CREAT, 0, 0, 0) == ERROR64);
	CHECK64(open_calls == 0 && create_calls == 0);
	for (i = 0; i < sizeof invalid_utf8 / sizeof invalid_utf8[0]; i++) {
		reset64();
		memcpy(user_buffer, invalid_utf8[i], strlen(invalid_utf8[i]) + 1);
		CHECK64(call64(SYS_OPEN, path, O_CREAT, 0, 0, 0) == ERROR64);
		CHECK64(open_calls == 0 && create_calls == 0 && process.files[3].used == 0);
	}
	for (i = 0; i < sizeof valid_utf8 / sizeof valid_utf8[0]; i++) {
		reset64();
		memcpy(user_buffer, valid_utf8[i], strlen(valid_utf8[i]) + 1);
		CHECK64(call64(SYS_OPEN, path, 0, 0, 0, 0) == 3);
		CHECK64(strcmp(opened_name, valid_utf8[i]) == 0 && name_error == 0);
	}
	return 0;
}

static int test_files64(void)
{
	uint64_t buffer = (uintptr_t) user_buffer;
	uint64_t invalid[] = {0, 1, 2, 4, PROCESS64_MAX_FILES, 1ULL << 32, UINT64_MAX};
	size_t i;

	reset64();
	/* Stdio slots are reserved even if their unused file fields are nonzero. */
	process.files[0].used = process.files[1].used = process.files[2].used = 1;
	process.files[3].used = 1;
	CHECK64(call64(SYS_READ, 0, buffer, 8, 0, 0) == 3);
	CHECK64(call64(SYS_WRITE, 1, buffer, 8, 0, 0) == 8);
	CHECK64(call64(SYS_WRITE, 2, buffer, 8, 0, 0) == 8);
	CHECK64(console_reads == 1 && console_writes == 2);
	console_read_result = -2;
	CHECK64(call64(SYS_READ, 0, buffer, 8, 0, 0) == (uint64_t) -2);
	CHECK64(call64(SYS_WRITE, 0, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 1, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 2, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == 3);
	CHECK64(call64(SYS_WRITE, 3, buffer, 8, 0, 0) == 3);
	CHECK64(call64(SYS_SEEK, 3, (uint64_t) -17, 2, 0, 0) == 0);
	CHECK64(last_offset == -17 && last_whence == 2 && seek_calls == 1);
	CHECK64(call64(SYS_SEEK, 3, 0, (1ULL << 32) | 1, 0, 0) == ERROR64);
	CHECK64(call64(SYS_SEEK, 3, 0, 3, 0, 0) == ERROR64);
	CHECK64(seek_calls == 1);
	for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
		CHECK64(call64(SYS_SEEK, invalid[i], 0, 0, 0, 0) == ERROR64);
		CHECK64(call64(SYS_CLOSE, invalid[i], 0, 0, 0, 0) == ERROR64);
		if (invalid[i] >= 4) {
			CHECK64(call64(SYS_READ, invalid[i], buffer, 8, 0, 0) == ERROR64);
			CHECK64(call64(SYS_WRITE, invalid[i], buffer, 8, 0, 0) == ERROR64);
		}
	}
	CHECK64(read_calls == 1 && write_calls == 1 && seek_calls == 1);
	CHECK64(call64(SYS_READ, 3, 0, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_WRITE, 3, buffer, UINT64_MAX, 0, 0) == ERROR64);
	CHECK64(read_calls == 1 && write_calls == 1);
	CHECK64(call64(SYS_CLOSE, 3, 0, 0, 0, 0) == 0);
	CHECK64(call64(SYS_CLOSE, 3, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_WRITE, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64(process.files[0].used == 1 && read_calls == 1 && write_calls == 1);
	return 0;
}

static int test_tty64(void)
{
	uint64_t bad[] = {25, 80, 1ULL << 32, UINT64_MAX};
	size_t i;

	reset64();
	CHECK64(call64(SYS_TTY, TTY_SIZE, 0, 0, 0, 0) == screen_size);
	CHECK64(call64(SYS_TTY, TTY_READKEY, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_MODE, 1, 0, 0, 0) == 0);
	CHECK64(call64(SYS_TTY, TTY_READKEY, 0, 0, 0, 0) == key_event);
	CHECK64(call64(SYS_TTY, TTY_MODE, (1ULL << 32) | 1, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_MODE, 2, 0, 0, 0) == ERROR64);
	CHECK64(mode_calls == 1 && raw_mode == 1);
	CHECK64(call64(SYS_TTY, TTY_MOVE, 24, 79, 0, 0) == 0);
	CHECK64(move_calls == 1 && last_row == 24 && last_col == 79);
	for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		CHECK64(call64(SYS_TTY, TTY_MOVE, bad[i], 0, 0, 0) == ERROR64);
	}
	CHECK64(call64(SYS_TTY, TTY_MOVE, 0, 80, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_MOVE, 0, 1ULL << 32, 0, 0) == ERROR64);
	CHECK64(move_calls == 1);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 0, 0, 25, 80) == 0);
	CHECK64(last_rows == 25 && last_cols == 80);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 24, 79, 1, 1) == 0);
	CHECK64(last_row == 24 && last_col == 79 && last_rows == 1 && last_cols == 1);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 1, 1, 0, 0) == 0);
	CHECK64(clear_calls == 2);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 25, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 24, 79, 2, 1) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 24, 79, 1, 2) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 0, 0, UINT64_MAX, 1) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 0, 0, 1, (1ULL << 32) | 1) == ERROR64);
	CHECK64(clear_calls == 2);
	CHECK64(call64(SYS_TTY, TTY_ATTR, 255, 0, 0, 0) == 0);
	CHECK64(last_fg == 255 && last_bg == 0);
	CHECK64(call64(SYS_TTY, TTY_ATTR, 256, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_ATTR, 0, (1ULL << 32) | 7, 0, 0) == ERROR64);
	CHECK64(attr_calls == 1);
	CHECK64(call64(SYS_TTY, TTY_FLUSH, 0, 0, 0, 0) == 0 && flush_calls == 1);
	CHECK64(call64(SYS_TTY, (1ULL << 32) | TTY_FLUSH, 0, 0, 0, 0) == ERROR64);
	CHECK64(flush_calls == 1);
	screen_size = 0;
	CHECK64(call64(SYS_TTY, TTY_MOVE, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 0, 0, 0, 0) == ERROR64);
	process.console = NULL;
	CHECK64(call64(SYS_TTY, TTY_MODE, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_FLUSH, 0, 0, 0, 0) == ERROR64);
	CHECK64(mode_calls == 1 && flush_calls == 1);
	return 0;
}

static int test_dispatch_and_heap64(void)
{
	uintptr_t base = (uintptr_t) user_buffer;

	reset64();
	CHECK64(syscall_handler64(NULL) == 0);
	CHECK64(call64(0, 0, 0, 0, 0, 0) == ERROR64 && last_action == 0);
	CHECK64(call64((1ULL << 32) | SYS_EXIT, 0, 0, 0, 0, 0) == ERROR64);
	CHECK64(process.exited == 0);
	timerctl64.count = 0xfedcba9876543210ULL;
	CHECK64(call64(SYS_TICKS, 0, 0, 0, 0, 0) == timerctl64.count);
	CHECK64(call64(SYS_EXIT, 37, 0, 0, 0, 0) == SYS_EXIT && last_action == 1);
	CHECK64(process.exited == 1 && process.exit_status == 37);
	process.heap.size = 32;
	CHECK64(call64(SYS_ALLOC, 1, 0, 0, 0, 0) == base);
	CHECK64(process.heap_next == base + 16);
	CHECK64(call64(SYS_ALLOC, 0, 0, 0, 0, 0) == 0);
	CHECK64(call64(SYS_ALLOC, UINT64_MAX, 0, 0, 0, 0) == 0);
	CHECK64(call64(SYS_ALLOC, 17, 0, 0, 0, 0) == 0);
	CHECK64(process.heap_next == base + 16);
	CHECK64(call64(SYS_ALLOC, 16, 0, 0, 0, 0) == base + 16);
	CHECK64(call64(SYS_FREE, base + 16, 16, 0, 0, 0) == 0);
	CHECK64(process.heap_next == base + 32);
	CHECK64(call64(SYS_FREE, base + 16, 17, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_FREE, base, UINT64_MAX, 0, 0, 0) == ERROR64);
	process.heap.base = UINTPTR_MAX - 15;
	process.heap_next = process.heap.base;
	CHECK64(call64(SYS_ALLOC, 1, 0, 0, 0, 0) == 0);
	current = NULL;
	CHECK64(call64(SYS_EXIT, 0, 0, 0, 0, 0) == ERROR64 && last_action == 0);
	CHECK64(abi_error == 0);
	return 0;
}

int main(void)
{
	CHECK64(test_open64() == 0);
	CHECK64(test_name_snapshot64() == 0);
	CHECK64(test_files64() == 0);
	CHECK64(test_tty64() == 0);
	CHECK64(test_dispatch_and_heap64() == 0);
	return 0;
}
