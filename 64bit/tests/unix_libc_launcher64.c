#include <mowkow/syscall64.h>
#include <stdint.h>

void exec_probe_entry64(uintptr_t argc, char **argv);
void exec_probe_entry64(uintptr_t argc, char **argv)
{
	char *args[] = {"libcprobe", argc > 1 ? argv[1] : "1", "2", NULL};
	char *env[] = {"MOWKOW_LIBC=static", NULL};
	static const char error[] = "libc: FAIL launcher\n";

	(void) unix64_syscall6(UNIX64_SYS_EXECVE, (uintptr_t) "libcprobe",
		(uintptr_t) args, (uintptr_t) env, 0, 0, 0);
	(void) unix64_syscall6(2, 2, (uintptr_t) error, sizeof(error) - 1, 0, 0, 0);
	(void) unix64_syscall6(1, 1, 0, 0, 0, 0, 0);
	__builtin_trap();
}
