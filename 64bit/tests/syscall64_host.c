#include <arch/arch64.h>
#include <assert.h>
#include <console64.h>
#include <fd64.h>
#include <interrupt64.h>
#include <arch/platform64.h>
#include <mtask64.h>
#include <process64.h>
#include <process_file64.h>
#include <stdio.h>
#include <string.h>
#include <syscall64.h>
#include <timer64.h>
#include <uapi/unix64.h>
#include <unix_wait64.h>

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
static int seek_mutate64;
static uint32_t open_calls;
static uint32_t create_calls;
static uint32_t truncate_calls;
static uint32_t write_calls;
static uint32_t seek_calls;
static int64_t last_offset;
static int last_whence;
static int64_t io_result;
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
static int readonly64, io_failure64;
static unsigned char snapshot64[512];
static int mutate_user_name;
static int name_error;
static char opened_name[FD64_NAME_MAX];
static char created_name[FD64_NAME_MAX];
static uintptr_t opened_name_pointer;
static uintptr_t created_name_pointer;
static unsigned char space_token64;
static unsigned int pins64;
static int pin_failure64;
static struct TASK64 wait_task64;
static int irq_off64, wait_mode64, close_wait64, wake_calls64, wake_error64;
static uint64_t wait_start64;
static struct PROCESS64 *opening_process64;
static int opening_fd64;

uint64_t platform_irq_save64(void)
{
	uint64_t old = irq_off64;

	irq_off64 = 1;
	return old;
}
void platform_irq_restore64(uint64_t old) { irq_off64 = (int) old; }
struct TASK64 *task_now64(void) { return &wait_task64; }
int console64_close_requested(const struct CONSOLE64 *console)
{
	(void) console;
	return close_wait64;
}
int task_run64(struct TASK64 *task, int level, int priority)
{
	assert(irq_off64 && task == &wait_task64 && level == -1 && priority == 0);
	wake_calls64++;
	return wake_error64;
}
int task_sleep_prepare64(struct TASK64 *task)
{
	assert(irq_off64 && task == &wait_task64 && pins64 == 1);
	if (wait_mode64 == 3) { return -12; }
	if (wait_mode64 == 1) { assert(unix_futex_wake64((uintptr_t) user_buffer) == 1); }
	return 0;
}
int task_sleep_commit64(struct TASK64 *task)
{
	assert(!irq_off64 && task == &wait_task64 && pins64 == 1);
	if (wait_mode64 == 1) { assert(wake_calls64 == 1); return 0; }
	if (wait_mode64 == 2) {
		unix_wait_tick64(wait_start64 + 1);
		assert(wake_calls64 == 0);
		unix_wait_tick64(wait_start64 + 2);
		assert(wake_calls64 == 1);
		return 0;
	}
	if (wait_mode64 == 4) { close_wait64 = 1; unix_wait_tick64(wait_start64 + 1); return 0; }
	if (wait_mode64 == 5) { return -22; }
	if (wait_mode64 == 6) {
		wake_error64 = -12;
		assert(unix_futex_wake64((uintptr_t) user_buffer) == 1);
		assert(wake_calls64 == 1);
		wake_error64 = 0;
		unix_wait_tick64(wait_start64 + 1);
		assert(wake_calls64 == 2);
		return 0;
	}
	assert(!"unexpected futex sleep");
	return -22;
}

int process64_execve_user64(const char *path, const void *argv, const void *envp)
{
	(void) path; (void) argv; (void) envp;
	return -38;
}
static struct MUTEX64 *held_mutex64;
static int mutex_error64;
int mutex64_lock(struct MUTEX64 *mutex)
{
	assert(held_mutex64 == NULL);
	if (mutex_error64) { return mutex_error64; }
	held_mutex64 = mutex;
	return 0;
}
int mutex64_unlock(struct MUTEX64 *mutex)
{
	assert(held_mutex64 == mutex);
	held_mutex64 = NULL;
	return 0;
}
int64_t process64_fork_user64(const void *frame, const void *fp)
{
	(void) frame; (void) fp; return -38;
}
int64_t process64_wait_user64(int64_t pid, void *status, uint64_t options)
{
	(void) pid; (void) status; (void) options; return -38;
}
void process64_wake_tick64(void) { }
int process64_signal_pending64(const struct PROCESS64 *process) { (void) process; return 0; }
int process64_signal_send64(int64_t pid) { (void) pid; return -38; }
int process64_signal_set64(uintptr_t handler, uintptr_t restorer)
{
	(void) handler; (void) restorer; return -38;
}
int process64_signal_user64(uint64_t number, void *frame, void *fp)
{
	(void) number; (void) frame; (void) fp; return -38;
}
int process64_set_tls64(uintptr_t pointer) { (void) pointer; return -38; }

int address_space_pin64(struct ADDRESS_SPACE64 *space)
{
	assert(space == (struct ADDRESS_SPACE64 *) (void *) &space_token64);
	if (pin_failure64) { return -16; }
	pins64++;
	return 0;
}

int address_space_unpin64(struct ADDRESS_SPACE64 *space)
{
	assert(space == (struct ADDRESS_SPACE64 *) (void *) &space_token64 && pins64 > 0);
	pins64--;
	return 0;
}

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

int process64_user_write_valid(void *ptr, size_t size)
{
	return !readonly64 && process64_user_range_valid(ptr, size);
}

int address_space_query64(const struct ADDRESS_SPACE64 *space, uintptr_t address,
	struct USER_PAGE64 *page)
{
	assert(space == (struct ADDRESS_SPACE64 *) (void *) &space_token64 && pins64 > 0);
	if (!process64_user_range_valid((void *) address, 1)) { return -14; }
	page->kernel_address = address;
	return 0;
}

void *process64_io_buffer(size_t size)
{
	return io_failure64 || size > sizeof(snapshot64) ? NULL : snapshot64;
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
	assert(!irq_off64);
	if (opening_process64 != NULL) {
		assert(!opening_process64->files[opening_fd64].used && fh->pos == 0);
	}
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
	assert(!irq_off64);
	if (opening_process64 != NULL) {
		assert(!opening_process64->files[opening_fd64].used);
	}
	(void) fh;
	capture_name64(created_name, name);
	created_name_pointer = (uintptr_t) name;
	create_calls++;
	return create_result;
}

int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size)
{
	assert(!irq_off64);
	if (opening_process64 != NULL) {
		assert(!opening_process64->files[opening_fd64].used);
	}
	(void) fh;
	if (size != 0) {
		abi_error = 1;
	}
	truncate_calls++;
	return truncate_result;
}

int64_t fd64_write_checked(struct FDHANDLE64 *fh, const void *src, size_t size)
{
	assert(held_mutex64 != NULL);
	(void) fh;
	if ((const void *) src != snapshot64) { abi_error = 1; }
	write_calls++;
	return io_result < 0 || (uint64_t) io_result < size ? io_result : (int64_t) size;
}

int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence)
{
	if (seek_mutate64) {
		assert(held_mutex64 != NULL);
		int64_t base = whence == 0 ? 0 : (whence == 1 ? fh->pos : fh->info.size);

		fh->pos = (uint32_t) (base + offset);
		fh->cluster++;
	}
	seek_calls++;
	last_offset = offset;
	last_whence = whence;
	return seek_result;
}

static int checked_error64, checked_calls64, sync_calls64;
int fd64_stat_checked(struct FDHANDLE64 *fh, const char *name, struct FILE_INFO64 *info)
{
	assert(pins64 > 0);
	if (fh != NULL) { assert(held_mutex64 != NULL && name == NULL); }
	if (name != NULL) { capture_name64(opened_name, name); }
	checked_calls64++;
	if (checked_error64) { return checked_error64; }
	memset(info, 0, sizeof(*info)); info->size64 = 123; info->identity64 = 0x200000040UL;
	return 0;
}
static unsigned int opened_flags64;
int fd64_open_checked(struct FDHANDLE64 *fh, const char *name, unsigned int flags)
{
	opened_flags64 = flags;
	assert(!irq_off64);
	if (opening_process64 != NULL) {
		assert(!opening_process64->files[opening_fd64].used);
		assert(fh->pos == 0);
		fh->pos = 11; /* Even a failed backend may have changed its handle. */
	}
	capture_name64(opened_name, name); checked_calls64++;
	return checked_error64;
}
int64_t fd64_read_checked(struct FDHANDLE64 *fh, void *dst, size_t size)
{
	assert(pins64 > 0);
	assert(held_mutex64 != NULL);
	(void) fh;
	if ((void *) dst != snapshot64) { abi_error = 1; }
	memset(dst, 0x7c, size);
	checked_calls64++; return checked_error64 ? checked_error64 : 7;
}
int fd64_sync(void) { sync_calls64++; return checked_error64 ? -1 : 3; }

void console64_write_con(struct CONSOLE64 *con, const char *src, uint64_t len)
{
	if ((const void *) src != snapshot64) { abi_error = 1; }
	(void) len;
	if (con != process.console) {
		abi_error = 1;
	}
	console_writes++;
}

uint64_t console64_read_con(struct CONSOLE64 *con, char *dst, uint64_t len)
{
	assert(pins64 > 0);
	if ((void *) dst != snapshot64) { abi_error = 1; }
	if (console_read_result > 0 && (uint64_t) console_read_result <= len) {
		memset(dst, 0x5a, (size_t) console_read_result);
	}
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

int console64_cursor_con(struct CONSOLE64 *con, int visible, unsigned int cells)
{
	CHECK64(con == process.console);
	CHECK64(visible == 0 || visible == 1);
	CHECK64(cells == 1 || cells == 2);
	return -16; /* Verify signed error propagation from the owning console. */
}

int64_t console64_input_mode_con(struct CONSOLE64 *con)
{
	if (con != process.console) { abi_error = 1; }
	return ((uint64_t) UINT32_MAX << 1) | 1U;
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
	assert(pins64 == 0);
	original.rax = frame.rax;
	if (memcmp(&frame, &original, sizeof frame) != 0) {
		abi_error = 1;
	}
	return frame.rax;
}

static void reset64(void)
{
	process_files_close64(&process, 0);
	memset(&process, 0, sizeof process);
	process.memory = &process.images[0];
	assert(pins64 == 0);
	pin_failure64 = 0;
	process.memory->space = (struct ADDRESS_SPACE64 *) (void *) &space_token64;
	memset(user_buffer, 0, sizeof user_buffer);
	memcpy(user_buffer, "app.txt", 8);
	current = &process;
	user_limit = sizeof user_buffer;
	readonly64 = io_failure64 = 0;
	checked_error64 = checked_calls64 = sync_calls64 = 0;
	abi_error = 0;
	mutate_user_name = 0;
	name_error = 0;
	opened_name[0] = created_name[0] = '\0';
	opened_name_pointer = created_name_pointer = 0;
	process.console = (struct CONSOLE64 *) (void *) &console_token;
	assert(process_file_stdio64(&process, process.console) == 0);
	process.memory->heap.base = (uintptr_t) user_buffer;
	process.memory->heap.size = sizeof user_buffer;
	process.memory->heap_next = process.memory->heap.base;
	open_result = 1;
	create_result = 1;
	truncate_result = 0;
	seek_result = 0;
	seek_mutate64 = 0;
	open_calls = create_calls = truncate_calls = 0;
	write_calls = seek_calls = 0;
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
		CHECK64(process.files[3].shared && process.files[3].shared->references == 1);
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
	CHECK64(call64(SYS_OPEN, buffer, 0, 0, 0, 0) == 3);
	CHECK64(call64(SYS_READ, 0, buffer, 8, 0, 0) == 3);
	CHECK64(call64(SYS_WRITE, 1, buffer, 8, 0, 0) == 8);
	CHECK64(call64(SYS_WRITE, 2, buffer, 8, 0, 0) == 8);
	CHECK64(console_reads == 1 && console_writes == 2);
	/* Legacy reads report every non-EIO failure, including console EINTR, as -1. */
	console_read_result = -2;
	CHECK64(call64(SYS_READ, 0, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_WRITE, 0, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 1, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 2, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == 7);
	/* A failed FAT read keeps the legacy zero count; other errors are -1. */
	checked_error64 = -5;
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == 0);
	checked_error64 = -22;
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == ERROR64);
	checked_error64 = 0;
	CHECK64(call64(SYS_WRITE, 3, buffer, 8, 0, 0) == 3);
	/* Checked FAT failures keep the legacy zero-count result. */
	const int64_t backend_errors[] = {-5, -27, -28, -30};

	for (i = 0; i < sizeof backend_errors / sizeof backend_errors[0]; i++) {
		io_result = backend_errors[i];
		CHECK64(call64(SYS_WRITE, 3, buffer, 8, 0, 0) == 0);
	}
	io_result = 3;
	process.files[3].shared->handle.info.size = 100;
	CHECK64(call64(SYS_SEEK, 3, (uint64_t) -17, 2, 0, 0) == 0);
	CHECK64(last_offset == -17 && last_whence == 2 && seek_calls == 1);
	/* Legacy seek failures stay -1; out-of-range offsets never reach FAT. */
	CHECK64(call64(SYS_SEEK, 3, 101, 0, 0, 0) == ERROR64);
	seek_result = -1;
	CHECK64(call64(SYS_SEEK, 3, 1, 0, 0, 0) == ERROR64);
	seek_result = 0;
	CHECK64(call64(SYS_SEEK, 3, 0, (1ULL << 32) | 1, 0, 0) == ERROR64);
	CHECK64(call64(SYS_SEEK, 3, 0, 3, 0, 0) == ERROR64);
	CHECK64(seek_calls == 2 && held_mutex64 == NULL);
	for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
		CHECK64(call64(SYS_SEEK, invalid[i], 0, 0, 0, 0) == ERROR64);
		CHECK64(call64(SYS_CLOSE, invalid[i], 0, 0, 0, 0) == ERROR64);
		if (invalid[i] >= 4) {
			CHECK64(call64(SYS_READ, invalid[i], buffer, 8, 0, 0) == ERROR64);
			CHECK64(call64(SYS_WRITE, invalid[i], buffer, 8, 0, 0) == ERROR64);
		}
	}
	CHECK64(checked_calls64 == 3 && write_calls == 5 && seek_calls == 2);
	CHECK64(call64(SYS_READ, 3, 0, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_WRITE, 3, buffer, UINT64_MAX, 0, 0) == ERROR64);
	CHECK64(checked_calls64 == 3 && write_calls == 5);
	CHECK64(call64(SYS_CLOSE, 3, 0, 0, 0, 0) == 0);
	CHECK64(call64(SYS_CLOSE, 3, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_WRITE, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64(process.files[0].used == 1 && checked_calls64 == 3 && write_calls == 5);
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
	CHECK64(call64(SYS_TTY, TTY_INPUT_MODE, 0, 0, 0, 0) == 0x1ffffffffULL);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 1, 2, 0, 0) == (uint64_t) -16);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 0, 1, 0, 0) == (uint64_t) -16);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 2, 1, 0, 0) == (uint64_t) -22);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 1, 0, 0, 0) == (uint64_t) -22);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 1, 3, 0, 0) == (uint64_t) -22);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, UINT64_MAX, 1, 0, 0) == (uint64_t) -22);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 1, (1ULL << 32) | 1, 0, 0) == (uint64_t) -22);
	screen_size = 0;
	CHECK64(call64(SYS_TTY, TTY_MOVE, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_TTY, TTY_CLEAR, 0, 0, 0, 0) == ERROR64);
	process.console = NULL;
	CHECK64(call64(SYS_TTY, TTY_INPUT_MODE, 0, 0, 0, 0) == (uint64_t) -22);
	CHECK64(call64(SYS_TTY, TTY_CURSOR, 1, 1, 0, 0) == (uint64_t) -22);
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
	process.memory->heap.size = 32;
	CHECK64(call64(SYS_ALLOC, 1, 0, 0, 0, 0) == base);
	CHECK64(process.memory->heap_next == base + 16);
	CHECK64(call64(SYS_ALLOC, 0, 0, 0, 0, 0) == 0);
	CHECK64(call64(SYS_ALLOC, UINT64_MAX, 0, 0, 0, 0) == 0);
	CHECK64(call64(SYS_ALLOC, 17, 0, 0, 0, 0) == 0);
	CHECK64(process.memory->heap_next == base + 16);
	CHECK64(call64(SYS_ALLOC, 16, 0, 0, 0, 0) == base + 16);
	CHECK64(call64(SYS_FREE, base + 16, 16, 0, 0, 0) == 0);
	CHECK64(process.memory->heap_next == base + 32);
	CHECK64(call64(SYS_FREE, base + 16, 17, 0, 0, 0) == ERROR64);
	CHECK64(call64(SYS_FREE, base, UINT64_MAX, 0, 0, 0) == ERROR64);
	process.memory->heap.base = UINTPTR_MAX - 15;
	process.memory->heap_next = process.memory->heap.base;
	CHECK64(call64(SYS_ALLOC, 1, 0, 0, 0, 0) == 0);
	current = NULL;
	CHECK64(call64(SYS_EXIT, 0, 0, 0, 0, 0) == ERROR64 && last_action == 0);
	CHECK64(abi_error == 0);
	return 0;
}

static int test_checked_files64(void)
{
	reset64();
	memcpy(user_buffer, "name", 5);
	uintptr_t output = (uintptr_t) user_buffer + 161; /* Deliberately unaligned. */
	struct FILE_INFO64 info;

	CHECK64(call64(SYS_FILE, FILE_STAT64, (uintptr_t) user_buffer, output, 0, 0) == 0);
	memcpy(&info, (void *) output, sizeof(info));
	CHECK64(info.size64 == 123 && info.identity64 == 0x200000040UL);
	CHECK64(checked_calls64 == 1 && name_error == 0);
	checked_error64 = -5;
	memset((void *) output, 0xa5, sizeof(info));
	CHECK64((int64_t) call64(SYS_FILE, FILE_STAT64, (uintptr_t) user_buffer, output, 0, 0) == -5);
	CHECK64(user_buffer[161] == 0xa5);
	CHECK64((int64_t) call64(SYS_FILE, FILE_OPEN64, (uintptr_t) user_buffer, 7, 0, 0) == -5);
	CHECK64(process.files[3].used == 0);
	checked_error64 = 0;
	CHECK64((int64_t) call64(SYS_FILE, FILE_OPEN64, (uintptr_t) user_buffer, 1UL << 32, 0, 0) == -22);
	CHECK64((int64_t) call64(SYS_FILE, FILE_STAT64, (uintptr_t) user_buffer, 1, 0, 0) == -14);
	CHECK64(call64(SYS_FILE, FILE_OPEN64, (uintptr_t) user_buffer, 0, 0, 0) == 3);
	CHECK64(process.files[3].shared && process.files[3].shared->references == 1);
	int before = checked_calls64;

	CHECK64(process_file_read64(NULL, snapshot64, 8) == -9);
	CHECK64(process_file_read64(&process.files[4], snapshot64, 8) == -9);
	CHECK64(process_file_read64(&process.files[3], NULL, 8) == -22);
	CHECK64(process_file_read64(&process.files[3], snapshot64, SIZE_MAX) == -22);
	mutex_error64 = -4;
	memset((void *) output, 0xa5, 8);
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, output, 8, 0) == -4);
	CHECK64(checked_calls64 == before && user_buffer[161] == 0xa5);
	CHECK64(pins64 == 0 && held_mutex64 == NULL);
	mutex_error64 = 0;
	CHECK64(call64(SYS_FILE, FILE_FSTAT64, 3, output, 0, 0) == 0);
	memcpy(&info, (void *) output, sizeof(info));
	CHECK64(info.size64 == 123 && info.identity64 == 0x200000040UL);
	before = checked_calls64;
	mutex_error64 = -4;
	CHECK64((int64_t) call64(SYS_FILE, FILE_FSTAT64, 3, output, 0, 0) == -4);
	CHECK64(checked_calls64 == before && pins64 == 0 && held_mutex64 == NULL);
	mutex_error64 = 0;
	checked_error64 = -5;
	CHECK64((int64_t) call64(SYS_FILE, FILE_FSTAT64, 3, output, 0, 0) == -5);
	CHECK64(memcmp(&info, (void *) output, sizeof(info)) == 0);
	CHECK64(pins64 == 0 && held_mutex64 == NULL);
	checked_error64 = 0;
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, output, UINT64_MAX, 0) == -22);
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, 1, 1, 0) == -14);
	memset((void *) output, 0xa5, 8);
	checked_error64 = -5;
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, output, 8, 0) == -5);
	CHECK64(pins64 == 0 && held_mutex64 == NULL && user_buffer[161] == 0xa5);
	CHECK64((int64_t) call64(SYS_FILE, FILE_SYNC64, 3, 0, 0, 0) == -5);
	checked_error64 = 0;
	CHECK64(call64(SYS_FILE, FILE_READ64, 3, output, 8, 0) == 7);
	CHECK64(call64(SYS_FILE, FILE_SYNC64, 3, 0, 0, 0) == 0 && sync_calls64 == 2);
	CHECK64((int64_t) call64(SYS_FILE, FILE_SYNC64, 3, 1, 0, 0) == -22);
	CHECK64((int64_t) call64(SYS_FILE, FILE_SYNC64, 2, 0, 0, 0) == -9);
	CHECK64((int64_t) call64(SYS_FILE, 1UL << 32, 0, 0, 0, 0) == -38);
	int calls = checked_calls64;

	pin_failure64 = 1;
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, output, 8, 0) == -14);
	CHECK64((int64_t) call64(SYS_FILE, FILE_FSTAT64, 3, output, 0, 0) == -14);
	CHECK64(checked_calls64 == calls);
	CHECK64(call64(SYS_READ, 0, output, 8, 0, 0) == ERROR64 && console_reads == 0);
	return 0;
}

static int test_copy_boundary64(void)
{
	uintptr_t buffer = (uintptr_t) user_buffer;
	struct FILE_INFO64 info;

	reset64();
	CHECK64(call64(SYS_OPEN, buffer, 0, 0, 0, 0) == 3);
	memset(user_buffer, 0xa5, sizeof(user_buffer));
	readonly64 = 1;
	CHECK64(call64(SYS_READ, 0, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, buffer, 8, 0) == -14);
	CHECK64((int64_t) call64(SYS_FILE, FILE_FSTAT64, 3, buffer, 0, 0) == -14);
	CHECK64((int64_t) call64(SYS_FILE, FILE_STAT64, buffer, buffer, 0, 0) == -14);
	CHECK64(console_reads == 0 && checked_calls64 == 0);
	CHECK64(call64(SYS_WRITE, 1, buffer, 8, 0, 0) == 8);
	CHECK64(memcmp(snapshot64, user_buffer, 8) == 0);
	readonly64 = 0;
	io_failure64 = 1;
	CHECK64(call64(SYS_READ, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64(call64(SYS_WRITE, 3, buffer, 8, 0, 0) == ERROR64);
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, buffer, 8, 0) == -12);
	CHECK64(write_calls == 0 && checked_calls64 == 0);
	io_failure64 = 0;
	checked_error64 = -5;
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, buffer, 8, 0) == -5);
	CHECK64(user_buffer[0] == 0xa5 && user_buffer[7] == 0xa5);
	checked_error64 = 0;
	CHECK64(call64(SYS_FILE, FILE_READ64, 3, buffer, 8, 0) == 7);
	CHECK64(user_buffer[0] == 0x7c && user_buffer[6] == 0x7c && user_buffer[7] == 0xa5);
	CHECK64((int64_t) call64(SYS_FILE, FILE_READ64, 3, buffer, 1, 0) == -5);
	CHECK64((int64_t) call64(SYS_FILE, FILE_FSTAT64, 3,
		buffer + user_limit - sizeof(info) + 1, 0, 0) == -14);
	CHECK64(abi_error == 0);
	return 0;
}

static int test_unix_bridge64(void)
{
	static unsigned char arena[16384];
	uintptr_t buffer = (uintptr_t) user_buffer;
	uint64_t first, second;

	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	process.pid = 42; process.parent_pid = 7;
	CHECK64(call64(UNIX64_SYS_GETPID, 0, 0, 0, 0, 0) == 42);
	CHECK64(call64(UNIX64_SYS_GETPPID, 0, 0, 0, 0, 0) == 7);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, UINT64_MAX, buffer, 1, 0, 0) == -9);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 1, UINT64_MAX, 1, 0, 0) == -14);
	CHECK64(call64(UNIX64_SYS_WRITE, 1, 0, 0, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_WRITE, 1, buffer, 8, 0, 0) == 8);
	CHECK64(console_writes == 1);
	readonly64 = 1;
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 0, buffer, 8, 0, 0) == -14);
	readonly64 = 0;
	CHECK64(call64(UNIX64_SYS_READ, 0, buffer, 8, 0, 0) == 3);
	CHECK64(user_buffer[0] == 0x5a && pins64 == 0);
	console_read_result = 0;
	CHECK64(call64(UNIX64_SYS_READ, 0, buffer, 8, 0, 0) == 0);
	console_read_result = -1;
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 0, buffer, 8, 0, 0) == -4);
	CHECK64(pins64 == 0);
	/* A fork child never sleeps on the owner's console FIFO, via any alias. */
	uint32_t reads = console_reads;

	process.fork_task = (struct TASK64 *) (void *) &console_token;
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 0, buffer, 8, 0, 0) == -95);
	CHECK64(call64(UNIX64_SYS_DUP2, 0, 5, 0, 0, 0) == 5);
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 5, buffer, 8, 0, 0) == -95);
	CHECK64(call64(SYS_READ, 5, buffer, 8, 0, 0) == ERROR64);
	CHECK64(console_reads == reads && pins64 == 0);
	CHECK64(call64(UNIX64_SYS_CLOSE, 5, 0, 0, 0, 0) == 0);
	process.fork_task = NULL;
	io_failure64 = 1;
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 1, buffer, 8, 0, 0) == -12);
	io_failure64 = 0;
	CHECK64(call64(UNIX64_SYS_ISATTY, 1, 0, 0, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_CLOSE, 1, 0, 0, 0, 0) == 0);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 1, buffer, 1, 0, 0) == -9);
	CHECK64((int64_t) call64(UNIX64_SYS_CLOSE, 1, 0, 0, 0, 0) == -9);
	memcpy(user_buffer, "app.txt", 8);
	checked_error64 = -5;
	CHECK64((int64_t) call64(UNIX64_SYS_OPEN, buffer, 0, 0, 0, 0) == -5);
	CHECK64(!process.files[1].used && !process.files[3].used);
	checked_error64 = 0;
	/* UNIX open takes the lowest free FD, including a closed stdio slot. */
	CHECK64(call64(UNIX64_SYS_OPEN, buffer, 0, 0, 0, 0) == 1);
	CHECK64((int64_t) call64(UNIX64_SYS_ISATTY, 1, 0, 0, 0, 0) == -25);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 1, buffer, 1, 0, 0) == -9);
	CHECK64(call64(UNIX64_SYS_OPEN, buffer, UNIX64_OPEN_CLOEXEC, 0, 0, 0) == 3);
	CHECK64(process.files[3].shared && process.files[3].shared->references == 1);
	CHECK64(process.files[3].cloexec && strcmp(opened_name, "app.txt") == 0);
	CHECK64((int64_t) call64(UNIX64_SYS_OPEN, buffer, 1ULL << 32, 0, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 3, buffer, 8, 0, 0) == -9);
	CHECK64(call64(UNIX64_SYS_READ, 3, buffer, 8, 0, 0) == 7);
	memset(user_buffer, 0xa5, 8);
	int calls_before = checked_calls64;

	mutex_error64 = -4;
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 3, buffer, 8, 0, 0) == -4);
	CHECK64(checked_calls64 == calls_before && user_buffer[0] == 0xa5);
	CHECK64(pins64 == 0 && held_mutex64 == NULL);
	mutex_error64 = 0;
	checked_error64 = -5;
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 3, buffer, 8, 0, 0) == -5);
	CHECK64(user_buffer[0] == 0xa5 && pins64 == 0);
	CHECK64(held_mutex64 == NULL);
	CHECK64((int64_t) call64(UNIX64_SYS_ISATTY, 3, 0, 0, 0, 0) == -25);
	CHECK64(call64(UNIX64_SYS_CLOSE, 3, 0, 0, 0, 0) == 0);
	CHECK64(!process.files[3].used && !process.files[3].cloexec);
	CHECK64(abi_error == 0);
	process.memory->heap.base = 0x100000000UL;
	process.memory->heap.size = sizeof(arena);
	process.memory->heap_backing = (uintptr_t) arena;
	memset(arena, 0xa5, sizeof(arena));
	CHECK64((int64_t) call64(UNIX64_SYS_ANON_ALLOC, UINT64_MAX, 0, 0, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_ANON_ALLOC, 0, 0, 0, 0, 0) == -22);
	first = call64(UNIX64_SYS_ANON_ALLOC, 8192, 0, 0, 0, 0);
	second = call64(UNIX64_SYS_ANON_ALLOC, 8192, 0, 0, 0, 0);
	CHECK64(first == 0x100000000UL && second == first + 8192);
	for (size_t i = 0; i < sizeof(arena); i++) { CHECK64(arena[i] == 0); }
	CHECK64((int64_t) call64(UNIX64_SYS_ANON_ALLOC, 4096, 0, 0, 0, 0) == -12);
	CHECK64((int64_t) call64(UNIX64_SYS_ANON_FREE, first + 4096, 4096, 0, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_ANON_FREE, first, 4096, 0, 0, 0) == -22);
	CHECK64(call64(UNIX64_SYS_ANON_FREE, first, 8192, 0, 0, 0) == 0);
	CHECK64((int64_t) call64(UNIX64_SYS_ANON_FREE, first, 8192, 0, 0, 0) == -22);
	memset(arena, 0x7b, sizeof(arena));
	CHECK64(call64(UNIX64_SYS_ANON_ALLOC, 8192, 0, 0, 0, 0) == first);
	for (size_t i = 0; i < 8192; i++) { CHECK64(arena[i] == 0 && arena[i + 8192] == 0x7b); }
	CHECK64(call64(SYS_ALLOC, 16, 0, 0, 0, 0) == 0);
	return 0;
}

static int test_unix_wait64(void)
{
	uintptr_t word = (uintptr_t) user_buffer;
	uintptr_t timeout = word + 16;
	struct UNIX_TIMESPEC64 time = {0};
	int32_t value = 7;

	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	memcpy(user_buffer, &value, sizeof(value));
	memcpy(user_buffer + 16, &time, sizeof(time));
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word + 1, 7, 0, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, 0, 7, 0, 0, 0) == -14);
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 6, timeout, 0, 0) == -11);
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 7, timeout, 0, 0) == -110);
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 7, UINT64_MAX, 0, 0) == -14);
	time.nanoseconds = 1000000000;
	memcpy(user_buffer + 16, &time, sizeof(time));
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 7, timeout, 0, 0) == -22);
	time.seconds = INT64_MAX; time.nanoseconds = 0;
	memcpy(user_buffer + 16, &time, sizeof(time));
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 7, timeout, 0, 0) == -22);
	time.seconds = -1;
	memcpy(user_buffer + 16, &time, sizeof(time));
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 7, timeout, 0, 0) == -22);
	time.seconds = 0; time.nanoseconds = 1;
	memcpy(user_buffer + 16, &time, sizeof(time));
	for (int mode = 1; mode <= 6; mode++) {
		static const int expected[] = {0, 0, -110, -12, -4, -22, 0};

		wait_mode64 = mode; wake_calls64 = close_wait64 = wake_error64 = 0;
		wait_start64 = UINT64_MAX;
		unix_wait_tick64(wait_start64);
		CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAIT, word, 7, timeout, 0, 0) ==
			expected[mode]);
		CHECK64(pins64 == 0 && !irq_off64);
		CHECK64(call64(UNIX64_SYS_FUTEX_WAKE, word, 0, 0, 0, 0) == 0);
	}
	close_wait64 = 0;
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAKE, UINT64_MAX, 0, 0, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_FUTEX_WAKE, 0, 0, 0, 0, 0) == -14);
	unix_wait_tick64(1234);
	CHECK64(call64(UNIX64_SYS_CLOCK_GET, timeout, 0, 0, 0, 0) == 0);
	memcpy(&time, user_buffer + 16, sizeof(time));
	CHECK64(time.seconds == 12 && time.nanoseconds == 340000000);
	CHECK64((int64_t) call64(UNIX64_SYS_CLOCK_GET, UINT64_MAX, 0, 0, 0, 0) == -14);
	return 0;
}

static int test_unix_fcntl64(void)
{
	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	memcpy(user_buffer, "app.txt", 8);
	CHECK64(call64(UNIX64_SYS_OPEN, (uintptr_t) user_buffer, 0, 0, 0, 0) == 3);
	CHECK64(process_file_dup264(&process, 3, 4) == 0);
	struct UNIX_FILE64 *shared = process.files[3].shared;

	shared->handle.pos = 17;
	for (uint64_t fd = 0; fd <= 3; fd++) {
		CHECK64(call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_GETFD, UINT64_MAX, 0, 0) == 0);
		CHECK64(call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_SETFD, UNIX64_FD_CLOEXEC, 0, 0) == 0);
		CHECK64(call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_GETFD, 0, 0, 0) == UNIX64_FD_CLOEXEC);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_SETFD, 2, 0, 0) == -22);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_SETFD,
			(1ULL << 32) | UNIX64_FD_CLOEXEC, 0, 0) == -22);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, fd,
			(1ULL << 32) | UNIX64_F_SETFD, 0, 0, 0) == -22);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, fd, UINT64_MAX, 0, 0, 0) == -22);
		CHECK64(call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_GETFD, 0, 0, 0) == UNIX64_FD_CLOEXEC);
		CHECK64(call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_SETFD, 0, 0, 0) == 0);
		CHECK64(call64(UNIX64_SYS_FCNTL, fd, UNIX64_F_GETFD, 0, 0, 0) == 0);
	}
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_SETFD, UNIX64_FD_CLOEXEC, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_FCNTL, 4, UNIX64_F_GETFD, 0, 0, 0) == 0);
	CHECK64(shared->references == 2 && shared->handle.pos == 17);
	/* Status is per OFD: access mode comes from open, APPEND/NONBLOCK alias. */
	CHECK64(call64(UNIX64_SYS_FCNTL, 0, UNIX64_F_GETFL, UINT64_MAX, 0, 0) ==
		UNIX64_FL_RDONLY);
	CHECK64(call64(UNIX64_SYS_FCNTL, 2, UNIX64_F_GETFL, 0, 0, 0) == UNIX64_FL_WRONLY);
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_GETFL, 0, 0, 0) == UNIX64_FL_RDONLY);
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_SETFL,
		UNIX64_FL_RDWR | UNIX64_FL_APPEND | UNIX64_FL_NONBLOCK, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_FCNTL, 4, UNIX64_F_GETFL, 0, 0, 0) ==
		(UNIX64_FL_RDONLY | UNIX64_FL_APPEND | UNIX64_FL_NONBLOCK));
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 4, (uintptr_t) user_buffer, 1, 0, 0) == -9);
	CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_SETFL, 16, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_SETFL,
		(1ULL << 32) | UNIX64_FL_APPEND, 0, 0) == -22);
	CHECK64(call64(UNIX64_SYS_FCNTL, 4, UNIX64_F_SETFL, UNIX64_FL_APPEND, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_GETFL, 0, 0, 0) == UNIX64_FL_APPEND);
	/* Console streams accept APPEND, but cannot honor NONBLOCK before A3. */
	CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, 0, UNIX64_F_SETFL,
		UNIX64_FL_NONBLOCK, 0, 0) == -95);
	CHECK64(call64(UNIX64_SYS_FCNTL, 1, UNIX64_F_SETFL, UNIX64_FL_APPEND, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_FCNTL, 2, UNIX64_F_GETFL, 0, 0, 0) ==
		(UNIX64_FL_WRONLY | UNIX64_FL_APPEND));
	CHECK64(call64(UNIX64_SYS_FCNTL, 0, UNIX64_F_GETFL, 0, 0, 0) == UNIX64_FL_RDONLY);
	int fd;

	CHECK64(process_file_open_legacy64(&process, "app.txt", 0, &fd) == 0 && fd == 5);
	CHECK64(process_file_open64(&process, "app.txt", FILE_WRITING64,
		PROCESS_FILE_READ64 | PROCESS_FILE_WRITE64, 0, &fd) == 0);
	CHECK64(fd == 6 && call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_GETFL, 0, 0, 0) ==
		UNIX64_FL_RDWR);
	CHECK64(call64(UNIX64_SYS_FCNTL, 6, UNIX64_F_GETFL, 0, 0, 0) == UNIX64_FL_RDWR);
	/* An inherited legacy read/write FAT FD uses the same checked UNIX write. */
	CHECK64(call64(UNIX64_SYS_WRITE, 5, (uintptr_t) user_buffer, 1, 0, 0) == 1);
	CHECK64(call64(UNIX64_SYS_CLOSE, 5, 0, 0, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_CLOSE, 6, 0, 0, 0, 0) == 0);
	CHECK64((int64_t) call64(UNIX64_SYS_CLOSE, (1ULL << 32) | 4, 0, 0, 0, 0) == -9);
	CHECK64((int64_t) call64(UNIX64_SYS_CLOSE, UINT64_MAX, 0, 0, 0, 0) == -9);
	CHECK64(shared->references == 2 && process.files[4].used);
	CHECK64(call64(UNIX64_SYS_FCNTL, 1, UNIX64_F_SETFD, UNIX64_FD_CLOEXEC, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_CLOSE, 1, 0, 0, 0, 0) == 0);
	CHECK64(!process.files[1].used && !process.files[1].cloexec);
	CHECK64((int64_t) call64(UNIX64_SYS_CLOSE, 1, 0, 0, 0, 0) == -9);
	CHECK64(call64(UNIX64_SYS_CLOSE, 3, 0, 0, 0, 0) == 0);
	CHECK64(!process.files[3].cloexec && process.files[3].shared == NULL);
	CHECK64((int64_t) call64(UNIX64_SYS_CLOSE, 3, 0, 0, 0, 0) == -9);
	const uint64_t invalid[] = {1, 3, 7, PROCESS64_MAX_FILES, UINT64_MAX, (1ULL << 32) | 4};

	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, invalid[i], UNIX64_F_GETFD, 0, 0, 0) == -9);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, invalid[i], UNIX64_F_SETFD, 0, 0, 0) == -9);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, invalid[i], UNIX64_F_GETFL, 0, 0, 0) == -9);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, invalid[i], UNIX64_F_SETFL, 0, 0, 0) == -9);
	}
	CHECK64(shared->references == 1 && shared->handle.pos == 17);
	CHECK64(call64(UNIX64_SYS_FCNTL, 4, UNIX64_F_GETFD, 0, 0, 0) == 0);
	CHECK64(!irq_off64 && abi_error == 0);
	return 0;
}

static int test_unix_dupfd64(void)
{
	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	memcpy(user_buffer, "app.txt", 8);
	CHECK64(call64(UNIX64_SYS_OPEN, (uintptr_t) user_buffer,
		UNIX64_OPEN_CLOEXEC, 0, 0, 0) == 3);
	struct UNIX_FILE64 *shared = process.files[3].shared;

	shared->handle.pos = 17;
	const uint64_t commands[] = {UNIX64_F_DUPFD, UNIX64_F_DUPFD_CLOEXEC};

	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, 7, commands[i], 3, 0, 0) == -9);
		CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, (1ULL << 32) | 3,
			commands[i], 3, 0, 0) == -9);
		const uint64_t minimums[] = {PROCESS64_MAX_FILES, UINT64_MAX, (1ULL << 32) | 3};

		for (size_t j = 0; j < sizeof(minimums) / sizeof(minimums[0]); j++) {
			CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, 3, commands[i], minimums[j], 0, 0)
				== -22);
		}
	}
	CHECK64(shared->references == 1 && !process.files[4].used);
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_DUPFD, 5, 0, 0) == 5);
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_DUPFD_CLOEXEC, 3, 0, 0) == 4);
	CHECK64(process.files[5].shared == shared && process.files[4].shared == shared);
	CHECK64(call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_GETFD, 0, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_FCNTL, 4, UNIX64_F_GETFD, 0, 0, 0) == UNIX64_FD_CLOEXEC);
	CHECK64(call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_DUPFD, 3, 0, 0) == 6);
	CHECK64(call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_DUPFD_CLOEXEC, 7, 0, 0) == 7);
	CHECK64((int64_t) call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_DUPFD, 3, 0, 0) == -24);
	CHECK64(shared->references == 5 && shared->handle.pos == 17);
	process_files_close64(&process, 1);
	CHECK64(!process.files[3].used && !process.files[4].used && !process.files[7].used);
	CHECK64(process.files[5].used && process.files[6].used && shared->references == 2);
	CHECK64(call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_DUPFD, 3, 0, 0) == 3);
	CHECK64(process.files[3].shared == shared && shared->handle.pos == 17);
	/* Stdio is an ordinary slot: a closed stdin is the lowest free FD. */
	struct UNIX_FILE64 *output = process.files[1].shared;

	CHECK64(call64(UNIX64_SYS_CLOSE, 0, 0, 0, 0, 0) == 0);
	CHECK64(call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_DUPFD, 0, 0, 0) == 0);
	CHECK64(process.files[0].shared == shared && shared->references == 4);
	CHECK64(call64(UNIX64_SYS_FCNTL, 1, UNIX64_F_DUPFD_CLOEXEC, 0, 0, 0) == 4);
	CHECK64(process.files[4].shared == output && output->references == 3);
	CHECK64(process.files[4].cloexec && call64(UNIX64_SYS_ISATTY, 4, 0, 0, 0, 0) == 0);
	CHECK64((int64_t) call64(UNIX64_SYS_ISATTY, 0, 0, 0, 0, 0) == -25);
	CHECK64(!irq_off64 && abi_error == 0);
	return 0;
}

static int test_unix_dup264(void)
{
	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	memcpy(user_buffer, "app.txt", 8);
	for (int fd = 3; fd <= 4; fd++) {
		CHECK64(call64(UNIX64_SYS_OPEN, (uintptr_t) user_buffer,
			UNIX64_OPEN_CLOEXEC, 0, 0, 0) == (uint64_t) fd);
	}
	struct UNIX_FILE64 *source = process.files[3].shared;
	struct UNIX_FILE64 *target = process.files[4].shared;
	const uint64_t invalid[] = {PROCESS64_MAX_FILES, UINT64_MAX, (1ULL << 32) | 3};

	source->handle.pos = 17;
	target->handle.pos = 29;
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK64((int64_t) call64(UNIX64_SYS_DUP2, invalid[i], 4, 0, 0, 0) == -9);
		CHECK64((int64_t) call64(UNIX64_SYS_DUP2, 3, invalid[i], 0, 0, 0) == -9);
	}
	CHECK64((int64_t) call64(UNIX64_SYS_DUP2, 7, 4, 0, 0, 0) == -9);
	CHECK64(process.files[4].shared == target && target->references == 1);
	CHECK64(process.files[4].cloexec && target->handle.pos == 29);
	CHECK64(call64(UNIX64_SYS_DUP2, 3, 3, 0, 0, 0) == 3);
	CHECK64(process.files[3].cloexec && source->references == 1);
	CHECK64(call64(UNIX64_SYS_DUP2, 3, 4, 0, 0, 0) == 4);
	CHECK64(process.files[4].shared == source && !process.files[4].cloexec);
	CHECK64(target->references == 0 && source->references == 2);
	CHECK64(call64(UNIX64_SYS_DUP2, 3, 7, 0, 0, 0) == 7);
	CHECK64(call64(UNIX64_SYS_DUP2, 3, 4, 0, 0, 0) == 4);
	CHECK64(source->references == 3 && source->handle.pos == 17);
	CHECK64(call64(UNIX64_SYS_CLOSE, 3, 0, 0, 0, 0) == 0);
	CHECK64((int64_t) call64(UNIX64_SYS_DUP2, 3, 3, 0, 0, 0) == -9);
	CHECK64(source->references == 2 && process.files[7].shared == source);
	/* Save stdout, redirect it to a file, then restore the console alias. */
	struct UNIX_FILE64 *output = process.files[1].shared;
	uintptr_t buffer = (uintptr_t) user_buffer;
	uint32_t writes = console_writes;

	CHECK64(call64(UNIX64_SYS_FCNTL, 1, UNIX64_F_DUPFD, 0, 0, 0) == 3);
	CHECK64(call64(UNIX64_SYS_DUP2, 4, 1, 0, 0, 0) == 1);
	CHECK64(process.files[1].shared == source && output->references == 2);
	CHECK64(source->references == 3 && !process.files[1].cloexec);
	CHECK64((int64_t) call64(UNIX64_SYS_ISATTY, 1, 0, 0, 0, 0) == -25);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 1, buffer, 1, 0, 0) == -9);
	CHECK64(call64(UNIX64_SYS_DUP2, 3, 1, 0, 0, 0) == 1 && source->references == 2);
	CHECK64(call64(UNIX64_SYS_WRITE, 1, buffer, 1, 0, 0) == 1);
	CHECK64(call64(UNIX64_SYS_CLOSE, 3, 0, 0, 0, 0) == 0 && output->references == 2);
	CHECK64(console_writes == writes + 1);
	/* Console input stays read-only and output write-only through any alias. */
	CHECK64(call64(UNIX64_SYS_DUP2, 0, 5, 0, 0, 0) == 5);
	CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 5, buffer, 0, 0, 0) == -9);
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 2, buffer, 1, 0, 0) == -9);
	CHECK64(console_writes == writes + 1);
	CHECK64(!irq_off64 && abi_error == 0);
	return 0;
}

static int test_unix_seek64(void)
{
	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	CHECK64(call64(UNIX64_SYS_OPEN, (uintptr_t) user_buffer, 0, 0, 0, 0) == 3);
	CHECK64(call64(UNIX64_SYS_DUP2, 3, 4, 0, 0, 0) == 4);
	struct FDHANDLE64 *handle = &process.files[3].shared->handle;

	handle->info.size = 100;
	handle->cluster = 7;
	seek_mutate64 = 1;
	CHECK64(call64(UNIX64_SYS_SEEK, 3, 20, 0, 0, 0) == 20);
	CHECK64(call64(UNIX64_SYS_SEEK, 4, 5, 1, 0, 0) == 25);
	CHECK64(call64(UNIX64_SYS_SEEK, 3, (uint64_t) -10, 2, 0, 0) == 90);
	CHECK64(handle->pos == 90 && handle->cluster == 10);
	struct FDHANDLE64 saved = *handle;
	uint32_t calls = seek_calls;

	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 3, INT64_MIN, 1, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 3, INT64_MAX, 1, 0, 0) == -95);
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 3, 11, 1, 0, 0) == -95);
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 3, 0, (1ULL << 32) | 1, 0, 0) == -22);
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 0, 0, 0, 0, 0) == -29);
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 7, 0, 0, 0, 0) == -9);
	mutex_error64 = -4;
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 3, 0, 0, 0, 0) == -4);
	mutex_error64 = 0;
	CHECK64(seek_calls == calls && memcmp(handle, &saved, sizeof(saved)) == 0);
	seek_result = -1;
	CHECK64((int64_t) call64(UNIX64_SYS_SEEK, 4, 50, 0, 0, 0) == -5);
	CHECK64(seek_calls == calls + 1 && memcmp(handle, &saved, sizeof(saved)) == 0);
	CHECK64(held_mutex64 == NULL);
	seek_result = 0;
	CHECK64(call64(UNIX64_SYS_SEEK, 4, 0, 2, 0, 0) == 100);
	CHECK64(call64(UNIX64_SYS_SEEK, 3, (uint64_t) -100, 1, 0, 0) == 0);
	CHECK64(held_mutex64 == NULL && !irq_off64 && abi_error == 0);
	return 0;
}

static int64_t backend_read64(struct UNIX_FILE64 *file, void *buffer, size_t size)
{
	assert(held_mutex64 == &file->mutex && size == 8);
	memcpy(buffer, "ok", 2);
	return 2;
}

static int64_t stream_read64(struct UNIX_FILE64 *file, void *buffer, size_t size)
{
	(void) file; (void) buffer; (void) size;
	assert(held_mutex64 == NULL);
	return 1;
}

static int backend_writes64, backend_appends64;
static int64_t backend_append64;
static int64_t backend_write64(struct UNIX_FILE64 *file, const void *buffer,
	size_t size)
{
	assert(held_mutex64 == &file->mutex && size == 8);
	backend_writes64++;
	return memcmp(buffer, "ok", 2) == 0 ? 3 : -5;
}

static int64_t backend_seek64(struct UNIX_FILE64 *file, int64_t offset, int whence)
{
	assert(held_mutex64 == &file->mutex);
	if (offset == 0 && whence == 2) {
		backend_appends64++;
		return backend_append64;
	}
	assert(offset == 7 && whence == 1);
	return -29;
}

static int backend_stat_error64;
static int backend_stat64(struct UNIX_FILE64 *file, struct FILE_INFO64 *info)
{
	assert(held_mutex64 == &file->mutex);
	info->size64 = 42;
	info->identity64 = 1234;
	return backend_stat_error64;
}

static int test_file_ops64(void)
{
	static const struct PROCESS_FILE_OPS64 ops = {
		.read = backend_read64, .write = backend_write64, .seek = backend_seek64,
		.stat = backend_stat64,
	};
	static const struct PROCESS_FILE_OPS64 missing_ops = {0};
	static const struct PROCESS_FILE_OPS64 stream_ops = {.read = stream_read64};
	const unsigned int rw = PROCESS_FILE_READ64 | PROCESS_FILE_WRITE64;
	struct UNIX_FILE64 stream = {
		.references = 1, .flags = PROCESS_FILE_READ64, .ops = &stream_ops,
	};
	struct PROCESS64_FILE stream_file = {.used = 1, .shared = &stream};
	struct UNIX_FILE64 shared = {.references = 1, .flags = rw, .ops = &ops};
	struct UNIX_FILE64 unsupported = {.references = 1, .flags = rw, .ops = &missing_ops};
	struct PROCESS64_FILE file = {.used = 1, .shared = &shared};
	struct PROCESS64_FILE missing = {.used = 1, .shared = &unsupported};
	char buffer[8] = {0};
	int reads = checked_calls64;
	uint32_t seeks = seek_calls;

	CHECK64(process_file_read64(&file, buffer, sizeof(buffer)) == 2);
	CHECK64(buffer[0] == 'o' && buffer[1] == 'k' && buffer[2] == 0);
	CHECK64(process_file_write64(&file, buffer, sizeof(buffer)) == 3);
	CHECK64(process_file_seek64(&file, 7, 1) == -29);
	/* Missing directions are EBADF; a stream has no cursor or mutex. */
	CHECK64(process_file_read64(&missing, buffer, sizeof(buffer)) == -9);
	CHECK64(process_file_write64(&missing, buffer, sizeof(buffer)) == -9);
	CHECK64(process_file_write64(NULL, buffer, sizeof(buffer)) == -9);
	CHECK64(process_file_write64(&file, NULL, sizeof(buffer)) == -22);
	CHECK64(process_file_write64(&file, buffer, SIZE_MAX) == -22);
	CHECK64(process_file_seek64(&missing, 7, 1) == -29);
	CHECK64(process_file_read64(&stream_file, buffer, sizeof(buffer)) == 1);
	/* Access mode gates direction before any backend call. */
	int writes = backend_writes64;

	shared.flags = PROCESS_FILE_READ64;
	CHECK64(process_file_check64(&file, 1) == -9 && process_file_check64(&file, 0) == 0);
	CHECK64(process_file_write64(&file, buffer, sizeof(buffer)) == -9);
	shared.flags = PROCESS_FILE_WRITE64;
	CHECK64(process_file_check64(&file, 0) == -9);
	CHECK64(process_file_read64(&file, buffer, sizeof(buffer)) == -9);
	CHECK64(backend_writes64 == writes && backend_appends64 == 0);
	/* APPEND moves to EOF under the same mutex; a failed move writes nothing. */
	shared.flags = rw | PROCESS_FILE_APPEND64;
	CHECK64(process_file_write64(&file, buffer, sizeof(buffer)) == 3);
	CHECK64(backend_appends64 == 1 && backend_writes64 == writes + 1);
	backend_append64 = -5;
	CHECK64(process_file_write64(&file, buffer, sizeof(buffer)) == -5);
	CHECK64(backend_appends64 == 2 && backend_writes64 == writes + 1);
	backend_append64 = 0;
	shared.flags = rw;
	struct FILE_INFO64 info;

	memset(&info, 0xa5, sizeof(info));
	CHECK64(process_file_stat64(&file, &info) == 0);
	CHECK64(info.size64 == 42 && info.identity64 == 1234 && info.reserved64 == 0);
	struct FILE_INFO64 saved = info;

	backend_stat_error64 = -5;
	CHECK64(process_file_stat64(&file, &info) == -5);
	CHECK64(memcmp(&info, &saved, sizeof(info)) == 0);
	backend_stat_error64 = 0;
	CHECK64(process_file_stat64(&missing, &info) == -95);
	CHECK64(process_file_stat64(NULL, &info) == -9);
	CHECK64(process_file_stat64(&file, NULL) == -22);
	CHECK64(memcmp(&info, &saved, sizeof(info)) == 0);
	CHECK64(held_mutex64 == NULL && checked_calls64 == reads && seek_calls == seeks);
	return 0;
}

static int test_unix_open_write64(void)
{
	const uint64_t invalid[] = {
		UNIX64_OPEN_WRONLY | UNIX64_OPEN_RDWR, UNIX64_OPEN_CREAT, UNIX64_OPEN_TRUNC,
		UNIX64_OPEN_RDWR | 64, UNIX64_OPEN_WRONLY | (1ULL << 32),
	};
	uintptr_t path = (uintptr_t) user_buffer;
	uintptr_t data = (uintptr_t) user_buffer + 16;

	reset64();
	process.state = PROCESS_RUNNING64;
	process.memory->abi = UNIX64_ABI_VERSION;
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK64((int64_t) call64(UNIX64_SYS_OPEN, path, invalid[i], 0, 0, 0) == -22);
	}
	CHECK64(checked_calls64 == 0 && !process.files[3].used);
	/* Write access selects the checked backend's writing mode and OFD access. */
	CHECK64(call64(UNIX64_SYS_OPEN, path, UNIX64_OPEN_WRONLY | UNIX64_OPEN_CREAT |
		UNIX64_OPEN_TRUNC | UNIX64_OPEN_APPEND, 0, 0, 0) == 3);
	CHECK64(opened_flags64 == (FILE_WRITING64 | FILE_CREATE64 | FILE_TRUNCATE64));
	CHECK64(call64(UNIX64_SYS_FCNTL, 3, UNIX64_F_GETFL, 0, 0, 0) ==
		(UNIX64_FL_WRONLY | UNIX64_FL_APPEND));
	CHECK64((int64_t) call64(UNIX64_SYS_READ, 3, data, 8, 0, 0) == -9);
	/* APPEND seeks to EOF before the checked write under the OFD mutex. */
	CHECK64(call64(UNIX64_SYS_WRITE, 3, data, 8, 0, 0) == 3);
	CHECK64(seek_calls == 1 && last_offset == 0 && last_whence == 2 && write_calls == 1);
	CHECK64(call64(UNIX64_SYS_OPEN, path, UNIX64_OPEN_RDWR, 0, 0, 0) == 4);
	CHECK64(opened_flags64 == FILE_WRITING64);
	CHECK64(call64(UNIX64_SYS_FCNTL, 4, UNIX64_F_GETFL, 0, 0, 0) == UNIX64_FL_RDWR);
	CHECK64(call64(UNIX64_SYS_READ, 4, data, 8, 0, 0) == 7);
	CHECK64(call64(UNIX64_SYS_OPEN, path, UNIX64_OPEN_APPEND, 0, 0, 0) == 5);
	CHECK64(opened_flags64 == 0 && call64(UNIX64_SYS_FCNTL, 5, UNIX64_F_GETFL, 0, 0, 0) ==
		(UNIX64_FL_RDONLY | UNIX64_FL_APPEND));
	/* Checked FAT errors reach UNIX callers unchanged. */
	const int64_t errors[] = {-5, -27, -28, -30};

	for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		io_result = errors[i];
		CHECK64((int64_t) call64(UNIX64_SYS_WRITE, 4, data, 8, 0, 0) == errors[i]);
	}
	CHECK64(write_calls == 5 && seek_calls == 1 && held_mutex64 == NULL);
	CHECK64(!irq_off64 && abi_error == 0 && pins64 == 0);
	return 0;
}

static int test_unix_reservation64(void)
{
	reset64();
	for (uint64_t number = 0x100; number <= 0x1ff; number++) {
		CHECK64((int64_t) call64(number, 37, UINT64_MAX, UINT64_MAX, 0, 0) == -38);
	}
	CHECK64(process.exited == 0 && open_calls == 0 && checked_calls64 == 0);
	CHECK64(write_calls == 0 && console_writes == 0 && console_reads == 0);
	CHECK64(call64(0xff, 0, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(0x200, 0, 0, 0, 0, 0) == ERROR64);
	CHECK64(call64(0x100000100UL, 0, 0, 0, 0, 0) == ERROR64);
	return 0;
}

static int test_open_ownership64(void)
{
	static struct PROCESS64 owners[PROCESS64_MAX];
	const unsigned int capacity = PROCESS64_MAX * (PROCESS64_MAX_FILES - 3);
	int fd = 99;

	reset64();
	opening_process64 = &process; opening_fd64 = 3;
	open_result = create_result = 0;
	for (unsigned int i = 0; i <= capacity; i++) {
		CHECK64((int64_t) call64(SYS_OPEN, (uintptr_t) user_buffer, O_CREAT, 0, 0, 0) == -2);
		CHECK64(!process.files[3].used && !process.files[3].shared);
	}
	open_result = create_result = 1; truncate_result = -1;
	for (unsigned int i = 0; i <= capacity; i++) {
		CHECK64((int64_t) call64(SYS_OPEN, (uintptr_t) user_buffer, O_TRUNC, 0, 0, 0) == -2);
		CHECK64(!process.files[3].used && !process.files[3].shared);
	}
	truncate_result = 0;
	CHECK64(call64(SYS_OPEN, (uintptr_t) user_buffer, 0, 0, 0, 0) == 3);
	CHECK64(process.files[3].shared->references == 1 && !process.files[3].cloexec);
	CHECK64(call64(SYS_CLOSE, 3, 0, 0, 0, 0) == 0);
	/* More failures than pool slots must still leave room for a later open. */
	checked_error64 = -5;
	for (unsigned int i = 0; i <= capacity; i++) {
		CHECK64(process_file_open64(&process, "app.txt", 0, PROCESS_FILE_READ64, 1, &fd) == -5);
		CHECK64(fd == 99 && !process.files[3].used && !process.files[3].shared);
	}
	checked_error64 = 0;
	CHECK64(process_file_open64(&process, "app.txt", 0, PROCESS_FILE_READ64, 1, &fd) == 0 && fd == 3);
	struct UNIX_FILE64 *shared = process.files[3].shared;

	CHECK64(shared->references == 1 && shared->handle.pos == 11);
	CHECK64(process_file_inherit64(&process, &owners[0]) == 0);
	CHECK64(owners[0].files[3].shared == shared && shared->references == 2);
	process_files_close64(&process, 1);
	CHECK64(shared->references == 1 && owners[0].files[3].cloexec);
	process_files_close64(&owners[0], 0);
	CHECK64(shared->references == 0);

	/* The pool covers every slot, stdio included, of every process table. */
	process_files_close64(&process, 0);
	for (unsigned int i = 0; i < PROCESS64_MAX; i++) {
		opening_process64 = &owners[i];
		for (int slot = 0; slot < PROCESS64_MAX_FILES; slot++) {
			opening_fd64 = slot;
			CHECK64(process_file_open64(&owners[i], "app.txt", 0, PROCESS_FILE_READ64, 0, &fd) == 0);
			CHECK64(fd == slot && owners[i].files[slot].shared->references == 1);
		}
	}
	int calls = checked_calls64;

	opening_process64 = &process; opening_fd64 = 0; fd = 99;
	CHECK64(process_file_open64(&process, "app.txt", 0, PROCESS_FILE_READ64, 0, &fd) == -23);
	CHECK64(fd == 99 && !process.files[0].used && checked_calls64 == calls);
	CHECK64(process_file_stdio64(&process, process.console) == -23);
	CHECK64(!process.files[0].used && !process.files[1].used);
	CHECK64(process_file_open64(&owners[0], "app.txt", 0, PROCESS_FILE_READ64, 0, &fd) == -24);
	CHECK64(fd == 99 && checked_calls64 == calls);
	uint32_t legacy_calls = open_calls + create_calls + truncate_calls;

	CHECK64((int64_t) call64(SYS_OPEN, (uintptr_t) user_buffer, O_CREAT, 0, 0, 0) == -3);
	CHECK64(!process.files[3].used && !process.files[3].shared);
	CHECK64(open_calls + create_calls + truncate_calls == legacy_calls);
	process_file_close64(&owners[0].files[3]);
	CHECK64(process_file_open64(&process, "app.txt", 0, PROCESS_FILE_READ64, 0, &fd) == 0 && fd == 0);
	CHECK64(process.files[0].shared->references == 1);
	opening_process64 = NULL;
	process_files_close64(&process, 0);
	for (unsigned int i = 0; i < PROCESS64_MAX; i++) {
		process_files_close64(&owners[i], 0);
	}
	CHECK64(!irq_off64);
	return 0;
}

int main(void)
{
	CHECK64(test_unix_reservation64() == 0);
	CHECK64(test_unix_bridge64() == 0);
	CHECK64(test_unix_open_write64() == 0);
	CHECK64(test_unix_fcntl64() == 0);
	CHECK64(test_unix_dupfd64() == 0);
	CHECK64(test_unix_dup264() == 0);
	CHECK64(test_unix_seek64() == 0);
	CHECK64(test_file_ops64() == 0);
	CHECK64(test_unix_wait64() == 0);
	CHECK64(test_copy_boundary64() == 0);
	CHECK64(test_checked_files64() == 0);
	CHECK64(test_open64() == 0);
	CHECK64(test_name_snapshot64() == 0);
	CHECK64(test_files64() == 0);
	CHECK64(test_tty64() == 0);
	CHECK64(test_dispatch_and_heap64() == 0);
	CHECK64(test_open_ownership64() == 0);
	return 0;
}
