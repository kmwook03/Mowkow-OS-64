/* Runtime context probe: run distinct IDs in several console windows. */
#include <mowos.h>

int context_probe64(unsigned long id, int wait_for_input);

#ifdef __aarch64__
__asm__(
	".text\n"
	".global context_probe64\n"
	".type context_probe64,%function\n"
	"context_probe64:\n"
	"sub sp,sp,#128\n"
	"stp x19,x20,[sp,#0]\n"
	"stp x21,x22,[sp,#16]\n"
	"stp x29,x30,[sp,#32]\n"
	"stp x0,x1,[sp,#48]\n"
	"mrs x9,fpcr\n"
	"mrs x10,fpsr\n"
	"stp x9,x10,[sp,#64]\n"
	"mrs x9,tpidr_el0\n"
	"str x9,[sp,#80]\n"
	"mov x19,x0\n"
	"mov x20,sp\n"
	"mov x21,x0\n"
	"mov x22,x0\n"
	"and x9,x0,#3\n"
	"lsl x9,x9,#22\n"
	"msr fpcr,x9\n"
	"str x9,[sp,#88]\n"
	"msr fpsr,xzr\n"
	"msr tpidr_el0,x0\n"
	"dup v0.2d,x0\n"
	"dup v31.2d,x0\n"
	"mov x9,#0x9680\n"
	"movk x9,#0x98,lsl #16\n"
	"1:\n"
	"subs x9,x9,#1\n"
	"b.ne 1b\n"
	"mov x12,#0x1234\n"
	"mov x8,#9\n"
	"svc #0\n"
	"mov x9,#0x1234\n"
	"cmp x12,x9\n"
	"b.ne 8f\n"
	"ldr x9,[sp,#56]\n"
	"cbz x9,3f\n"
	"2:\n"
	"mov x8,#10\n"
	"mov x0,#1\n"
	"svc #0\n"
	"ubfx x9,x0,#32,#8\n"
	"cmp x9,#1\n"
	"b.ne 2b\n"
	"3:\n"
	"ldr x9,[sp,#48]\n"
	"cmp x19,x9\n"
	"b.ne 8f\n"
	"cmp x21,x9\n"
	"b.ne 8f\n"
	"cmp x22,x9\n"
	"b.ne 8f\n"
	"mov x10,sp\n"
	"cmp x20,x10\n"
	"b.ne 8f\n"
	"umov x10,v0.d[0]\n"
	"cmp x10,x9\n"
	"b.ne 8f\n"
	"umov x10,v31.d[1]\n"
	"cmp x10,x9\n"
	"b.ne 8f\n"
	"mrs x10,tpidr_el0\n"
	"cmp x10,x9\n"
	"b.ne 8f\n"
	"mrs x10,fpcr\n"
	"ldr x9,[sp,#88]\n"
	"cmp x10,x9\n"
	"b.ne 8f\n"
	"mrs x10,fpsr\n"
	"cbnz x10,8f\n"
	"mov x0,#0\n"
	"b 9f\n"
	"8:\n"
	"mov x0,#1\n"
	"9:\n"
	"ldp x9,x10,[sp,#64]\n"
	"msr fpcr,x9\n"
	"msr fpsr,x10\n"
	"ldr x9,[sp,#80]\n"
	"msr tpidr_el0,x9\n"
	"ldp x19,x20,[sp,#0]\n"
	"ldp x21,x22,[sp,#16]\n"
	"ldp x29,x30,[sp,#32]\n"
	"add sp,sp,#128\n"
	"ret\n"
	".size context_probe64,.-context_probe64\n"
);
#else
__asm__(
	".text\n"
	".global context_probe64\n"
	".type context_probe64,@function\n"
	"context_probe64:\n"
	"push %rbx\n"
	"push %rbp\n"
	"push %r12\n"
	"push %r13\n"
	"push %r14\n"
	"push %r15\n"
	"sub $64,%rsp\n"
	"mov %rdi,0(%rsp)\n"
	"mov %rsi,8(%rsp)\n"
	"fnstcw 16(%rsp)\n"
	"stmxcsr 20(%rsp)\n"
	"mov %edi,%eax\n"
	"and $3,%eax\n"
	"shl $10,%eax\n"
	"or $0x37f,%eax\n"
	"mov %eax,24(%rsp)\n"
	"fldcw 24(%rsp)\n"
	"mov %edi,%eax\n"
	"and $3,%eax\n"
	"shl $13,%eax\n"
	"or $0x1f80,%eax\n"
	"mov %eax,28(%rsp)\n"
	"ldmxcsr 28(%rsp)\n"
	"fildq 0(%rsp)\n"
	"movq %rdi,%xmm0\n"
	"movq %rdi,%xmm15\n"
	"mov %rdi,%rbx\n"
	"mov %rdi,%rbp\n"
	"mov %rdi,%r12\n"
	"mov %rdi,%r13\n"
	"mov %rdi,%r14\n"
	"mov %rdi,%r15\n"
	"mov $10000000,%ecx\n"
	"1:\n"
	"dec %ecx\n"
	"jnz 1b\n"
	"mov $0x1234,%ecx\n"
	"mov $0x5678,%r8d\n"
	"mov $9,%eax\n"
	"std\n"
	"int $0x80\n"
	"pushfq\n"
	"pop %rdx\n"
	"cld\n"
	"test $0x400,%edx\n"
	"jz 8f\n"
	"cmp $0x1234,%rcx\n"
	"jne 8f\n"
	"cmp $0x5678,%r8\n"
	"jne 8f\n"
	"cmpq $0,8(%rsp)\n"
	"je 3f\n"
	"2:\n"
	"mov $10,%eax\n"
	"mov $1,%edi\n"
	"int $0x80\n"
	"shr $32,%rax\n"
	"and $255,%eax\n"
	"cmp $1,%eax\n"
	"jne 2b\n"
	"3:\n"
	"mov 0(%rsp),%rdx\n"
	"cmp %rdx,%rbx\n"
	"jne 8f\n"
	"cmp %rdx,%rbp\n"
	"jne 8f\n"
	"cmp %rdx,%r12\n"
	"jne 8f\n"
	"cmp %rdx,%r13\n"
	"jne 8f\n"
	"cmp %rdx,%r14\n"
	"jne 8f\n"
	"cmp %rdx,%r15\n"
	"jne 8f\n"
	"movq %xmm0,%rax\n"
	"cmp %rdx,%rax\n"
	"jne 8f\n"
	"movq %xmm15,%rax\n"
	"cmp %rdx,%rax\n"
	"jne 8f\n"
	"fistpq 40(%rsp)\n"
	"cmp %rdx,40(%rsp)\n"
	"jne 8f\n"
	"fnstcw 32(%rsp)\n"
	"movzwl 32(%rsp),%eax\n"
	"cmp %eax,24(%rsp)\n"
	"jne 8f\n"
	"stmxcsr 36(%rsp)\n"
	"mov 36(%rsp),%eax\n"
	"cmp %eax,28(%rsp)\n"
	"jne 8f\n"
	"xor %eax,%eax\n"
	"jmp 9f\n"
	"8:\n"
	"mov $1,%eax\n"
	"9:\n"
	"fninit\n"
	"fldcw 16(%rsp)\n"
	"ldmxcsr 20(%rsp)\n"
	"add $64,%rsp\n"
	"pop %r15\n"
	"pop %r14\n"
	"pop %r13\n"
	"pop %r12\n"
	"pop %rbp\n"
	"pop %rbx\n"
	"ret\n"
	".size context_probe64,.-context_probe64\n"
);
#endif

static volatile unsigned long image_cookie64;

static void report64(unsigned long id, const char *result)
{
	char line[64] = "isotest ";
	size_t used = 9;

	line[8] = (char) ('0' + id);
	line[9] = '\0';
	if (str_append64(line, sizeof(line), &used, ": ") != 0 ||
		str_append64(line, sizeof(line), &used, result) != 0 ||
		str_append64(line, sizeof(line), &used, "\n") != 0) { exit(2); }
	if (write(1, line, used) != (long) used) { exit(2); }
}

/* Disposable-media close probe. Rewriting the same bytes makes every completed
 * write independently verifiable after termination, including an open handle.
 */
static int close_io64(unsigned long id, const unsigned char *bytes)
{
	char name[] = "CLOSE1.TXT";
	unsigned long heartbeat = 0;
	int first = 1;
	int fd;

	name[5] = (char) ('0' + id);
	fd = open(name, O_CREAT | O_TRUNC);
	if (fd < 0) { report64(id, "FAIL open"); return 1; }
	for (;;) {
		if (seek(fd, 0, 0) < 0 || write(fd, bytes, 16384) != 16384 ||
			file_call64(FILE_SYNC64, (unsigned long) fd, 0, 0) != 0) {
			report64(id, "FAIL io");
			return 1;
		}
		if (first || ticks() - heartbeat >= 200) {
			report64(id, "IO");
			heartbeat = ticks();
			first = 0;
		}
	}
}

int main(int argc, char **argv)
{
	volatile unsigned long stack_cookie[16];
	unsigned char *heap;
	unsigned long id, start, duration;
	int failed = 0;
	int live = argc == 3 && strcmp(argv[2], "live") == 0;
	unsigned long heartbeat;

	if (argc < 2 || argc > 3 || argv[1][0] < '1' || argv[1][0] > '4' || argv[1][1]) {
		puts("isotest: use ID 1..4 and optional short/fault/live/spin/io");
		return 1;
	}
	id = (unsigned long) (argv[1][0] - '0');
	heap = malloc(16384);
	if (heap == NULL) { report64(id, "FAIL alloc"); return 1; }
	image_cookie64 = 0x12345678UL + id;
	for (size_t i = 0; i < 16; i++) { stack_cookie[i] = id + i; }
	for (size_t i = 0; i < 16384; i++) { heap[i] = (unsigned char) (id + i); }
	tty_raw(1);
	report64(id, "READY");
	if (argc == 3 && strcmp(argv[2], "spin") == 0) {
		/* No syscall or voluntary yield: only a user IRQ return can stop us. */
		for (;;) { __asm__ volatile ("" ::: "memory"); }
	}
	if (argc == 3 && strcmp(argv[2], "io") == 0) { return close_io64(id, heap); }
	if (argc == 3 && strcmp(argv[2], "fault") == 0) {
#ifdef __aarch64__
		__asm__ volatile ("brk #0");
#else
		__asm__ volatile ("ud2");
#endif
	}
	duration = argc == 3 && strcmp(argv[2], "short") == 0 ? 100 : 3000;
	start = ticks();
	heartbeat = start;
	do {
		failed |= context_probe64(id, 0);
		if (image_cookie64 != 0x12345678UL + id) { failed = 1; }
		for (size_t i = 0; i < 16; i++) {
			if (stack_cookie[i] != id + i) { failed = 1; }
		}
		for (size_t i = 0; i < 16384; i++) {
			if (heap[i] != (unsigned char) (id + i)) { failed = 1; }
		}
		if (live && ticks() - heartbeat >= 200) {
			report64(id, failed ? "FAIL live" : "LIVE");
			heartbeat = ticks();
		}
	} while (!failed && (live || ticks() - start < duration));
	if (!failed) {
		report64(id, "WAIT");
		failed |= context_probe64(id, 1);
	}
	if (image_cookie64 != 0x12345678UL + id) { failed = 1; }
	for (size_t i = 0; i < 16; i++) {
		if (stack_cookie[i] != id + i) { failed = 1; }
	}
	for (size_t i = 0; i < 16384; i++) {
		if (heap[i] != (unsigned char) (id + i)) { failed = 1; }
	}
	free(heap);
	report64(id, failed ? "FAIL context" : "PASS");
	return failed ? 1 : 0;
}
