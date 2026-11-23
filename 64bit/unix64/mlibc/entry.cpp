#include <mlibc/elf/startup.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" void __dlapi_enter(uintptr_t *);
extern char **environ;

extern "C" void __mlibc_entry(uintptr_t *stack,
	int (*main_fn)(int, char **, char **))
{
	__dlapi_enter(stack);
	exit(main_fn(mlibc::entry_stack.argc, mlibc::entry_stack.argv, environ));
}
