/* Built-in command dispatch and application/interpreter execution. */
#include "console_internal64.h"
#include <fd64.h>
#include <gui64.h>
#include <kstring64.h>
#include <memory64.h>
#include <mpport64.h>
#include <mtask64.h>
#include <process64.h>
#include <timer64.h>

static int str_eq64(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

static int str_starts_with64(const char *s, const char *prefix)
{
	while (*prefix != '\0') {
		if (*s != *prefix) {
			return 0;
		}
		s++;
		prefix++;
	}
	return 1;
}

static void print_file_name64(struct CONSOLE64 *con, const char *name)
{
	uint16_t n;

	for (n = 0; name[n] != '\0'; n++) {
	}
	console64_write_con(con, name, n);
}

static void print_uint64(struct CONSOLE64 *con, uint64_t value)
{
	char buf[20];
	uint16_t i;

	if (value == 0) {
		console_put_char64(con, "0", 1);
		return;
	}
	i = 0;
	while (value != 0) {
		buf[i++] = (char) ('0' + (value % 10));
		value /= 10;
	}
	while (i > 0) {
		console_put_char64(con, &buf[--i], 1);
	}
}

static void print_hex64(struct CONSOLE64 *con, uint64_t value)
{
	uint16_t shift;
	uint8_t digit;
	int started;
	char c;

	console_puts_con64(con, "0x");
	started = 0;
	for (shift = 60; shift > 0; shift -= 4) {
		digit = (uint8_t) ((value >> shift) & 0x0f);
		if (digit != 0 || started != 0) {
			c = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
			console_put_char64(con, &c, 1);
			started = 1;
		}
	}
	digit = (uint8_t) (value & 0x0f);
	c = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
	console_put_char64(con, &c, 1);
}

static int run_program64(struct CONSOLE64 *con, const char *cmdline)
{
	char elf_cmdline[CONSOLE64_LINE_MAX + 1];
	char program[FD64_NAME_MAX];
	size_t command_len;
	size_t line_len;
	size_t used;
	int has_extension;
	int nano_alias;
	int status;

	/* Keep the English command's argv intact while resolving the shipped name. */
	nano_alias = str_eq64(cmdline, "nano") || str_starts_with64(cmdline, "nano ");
	status = process64_exec_file(nano_alias != 0 ? "나노" : cmdline, cmdline, con);
	/* x86_64 이미지는 앱을 확장자 없는 이름으로 넣지만, Pi의 FAT 파티션에는
	   빌드 산출물을 HELLO.ELF처럼 복사하기도 한다. 둘 다 `hello`로 실행되게
	   첫 토큰에 점이 없을 때만 .elf를 붙여 한 번 더 찾는다. */
	if (status == -2 && nano_alias != 0) {
		status = process64_exec_file("나노.elf", cmdline, con);
	} else if (status == -2) {
		command_len = 0;
		has_extension = 0;
		while (cmdline[command_len] != '\0' &&
				cmdline[command_len] != ' ') {
			if (cmdline[command_len] == '.') {
				has_extension = 1;
			}
			command_len++;
		}
		line_len = command_len;
		while (cmdline[line_len] != '\0') {
			line_len++;
		}
		if (has_extension == 0 && command_len != 0 && command_len < sizeof(program) &&
				line_len < sizeof(elf_cmdline) - 4) {
			/* A validated first token fits before constructing the fallback name. */
			memcpy(program, cmdline, command_len);
			program[command_len] = '\0';
			used = 0;
			elf_cmdline[0] = '\0';
			if (str_append64(elf_cmdline, sizeof(elf_cmdline), &used, program) != 0 ||
				str_append64(elf_cmdline, sizeof(elf_cmdline), &used, ".elf") != 0 ||
				str_append64(elf_cmdline, sizeof(elf_cmdline), &used,
					cmdline + command_len) != 0) {
				return 0;
			}
			status = process64_exec_file(elf_cmdline, elf_cmdline, con);
		}
	}
	if (status == -2) {
		return 0;
	}
	if (status == -8) {
		console_puts_con64(con, "another program is running\n");
		return 1;
	}
	if (status == PROCESS64_ERR_ARGS) {
		console_puts_con64(con, "program name/command line too long or too many arguments\n");
		return 1;
	}
	if (status == PROCESS64_ERR_INVALID || status == PROCESS64_ERR_STACK) {
		console_puts_con64(con, "invalid program name, command line, or user stack\n");
		return 1;
	}
	console_puts_con64(con, "exit ");
	print_uint64(con, (uint64_t) status);
	console_puts_con64(con, "\n");
	return 1;
}

void console_dispatch_command64(struct CONSOLE64 *con, const char *line)
{
	if (str_eq64(line, "help")) {
#ifdef __aarch64__
		console_puts_con64(con, "commands: help clear mem tasks ls 목록 type readme.txt APP [ARGS] py py FILE.PY 머꼬 [FILE] xwindow 창 new 새창\n");
#else
		console_puts_con64(con, "commands: help clear ticks mem tasks ls 목록 type readme.txt run HELLO py py FILE.PY xwindow 창 new 새창\n");
#endif
	} else if (str_eq64(line, "xwindow") || str_eq64(line, "window") ||
			str_eq64(line, "창")) {
		/* 전체 화면 토글은 콘솔 0 전용이다 -- 화면 크기 버퍼를 가진 건
		   콘솔 0뿐이다. */
		if (con != console64_active()) {
			console_puts_con64(con, "fullscreen is console 0 only\n");
		} else {
			gui64_toggle_window();
		}
	} else if (str_eq64(line, "clear") || str_eq64(line, "지우기")) {
		console_clear_screen64(con);
#ifndef __aarch64__
	} else if (str_eq64(line, "ticks")) {
		console_puts_con64(con, "ticks ");
		print_uint64(con, timerctl64.count);
		console_puts_con64(con, "\n");
#endif
	} else if (str_eq64(line, "mem") || str_eq64(line, "메모리")) {
		uintptr_t addr;

		console_puts_con64(con, "free ");
		print_uint64(con, memman64_total(&memman64) / 1024);
		console_puts_con64(con, " KiB\n");
		addr = memman64_alloc_4k(&memman64, 4096);
		console_puts_con64(con, "alloc4k ");
		print_hex64(con, addr);
		console_puts_con64(con, "\n");
		if (addr != 0) {
			memman64_free_4k(&memman64, addr, 4096);
		}
	} else if (str_eq64(line, "new") || str_eq64(line, "새창")) {
		if (console64_create() == NULL) {
			console_puts_con64(con, "no free console slot\n");
		}
	} else if (str_eq64(line, "tasks") || str_eq64(line, "태스크")) {
		uint32_t i;

		console_puts_con64(con, "switches ");
		print_uint64(con, taskctl64.switches);
		console_puts_con64(con, " current-level ");
		print_uint64(con, taskctl64.now_lv);
		console_puts_con64(con, "\n");
		/* 숨긴 콘솔이 살아 있는지 보려면 태스크별 전환 수가 필요하다. */
		for (i = 0; i < MAX_TASKS64; i++) {
			if (taskctl64.tasks0[i].flags == TASK64_FLAGS_UNUSED) {
				continue;
			}
			console_puts_con64(con, "  task ");
			print_uint64(con, i);
			console_puts_con64(con, " lv ");
			print_uint64(con, taskctl64.tasks0[i].level);
			console_puts_con64(con, " switches ");
			print_uint64(con, taskctl64.tasks0[i].switches);
			console_puts_con64(con, "\n");
		}
	} else if (str_eq64(line, "ls") || str_eq64(line, "목록")) {
		uint32_t i;
		uint32_t count;
		struct FDINFO64 finfo;
		char name[FD64_NAME_MAX];

		count = fd64_file_count();
		for (i = 0; i < count; i++) {
			if (fd64_file_at(i, &finfo, name, sizeof(name)) != 0) {
				print_file_name64(con, name);
				console_puts_con64(con, "  ");
				print_uint64(con, finfo.size);
				console_puts_con64(con, "\n");
			}
		}
		if (count == 0) {
			console_puts_con64(con, "no files\n");
		}
	} else if (str_eq64(line, "type readme.txt") ||
		str_eq64(line, "읽기 readme.txt")) {
		struct FDHANDLE64 fh;
		char buf[65];
		size_t n;

		if (fd64_open(&fh, "readme.txt") == 0) {
			console_puts_con64(con, "file not found\n");
		} else {
			for (;;) {
				n = fd64_read(&fh, buf, sizeof(buf) - 1);
				if (n == 0) {
					break;
				}
				console64_write_con(con, buf, n);
			}
			console_puts_con64(con, "\n");
		}
	} else if (str_starts_with64(line, "run ") || str_starts_with64(line, "실행 ")) {
		const char *args;

		/* "실행 "은 UTF-8로 7바이트, "run "은 4바이트다. 한 값으로 고정해
		   건너뛰면 한글 쪽이 글자 중간에서 잘린다. */
		args = line + (line[0] == 'r' ? 4 : 7);
		if (run_program64(con, args) == 0) {
			console_puts_con64(con, "파일 없음\n");
		}
	} else if (str_eq64(line, "py") || str_eq64(line, "파이썬")) {
		mpport_repl();
	} else if (str_starts_with64(line, "py ")) {
		mpport_run_file(line + 3);
	} else if (str_eq64(line, "머꼬")) {
		mpport_run_mowkow(NULL);
	} else if (str_starts_with64(line, "머꼬 ")) {
		/* "머꼬 "는 UTF-8로 7바이트다 */
		mpport_run_mowkow(line + 7);
	} else if (run_program64(con, line) == 0) {
		/* 내장 명령도 아니고 그런 실행 파일도 없다 */
		console_puts_con64(con, "알 수 없는 명령어\n");
	}
}
