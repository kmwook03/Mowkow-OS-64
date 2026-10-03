#ifndef MOWKOW64_INTERRUPT64_H
#define MOWKOW64_INTERRUPT64_H

#include <stddef.h>
#include <stdint.h>

#define INTERRUPT64_FRAME_VECTOR_OFFSET 120
#define INTERRUPT64_FRAME_ERROR_OFFSET  128
#define INTERRUPT64_FRAME_RIP_OFFSET    136
#define INTERRUPT64_FRAME_RSP_OFFSET    160
#define INTERRUPT64_FRAME_SIZE          176

struct INTERRUPT_FRAME64 {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
	uint64_t vector;
	uint64_t error;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
};

_Static_assert(offsetof(struct INTERRUPT_FRAME64, vector) ==
	INTERRUPT64_FRAME_VECTOR_OFFSET, "x86_64 vector frame offset mismatch");
_Static_assert(offsetof(struct INTERRUPT_FRAME64, error) ==
	INTERRUPT64_FRAME_ERROR_OFFSET, "x86_64 error frame offset mismatch");
_Static_assert(offsetof(struct INTERRUPT_FRAME64, rip) ==
	INTERRUPT64_FRAME_RIP_OFFSET, "x86_64 RIP frame offset mismatch");
_Static_assert(offsetof(struct INTERRUPT_FRAME64, rsp) ==
	INTERRUPT64_FRAME_RSP_OFFSET, "x86_64 RSP frame offset mismatch");
_Static_assert(sizeof(struct INTERRUPT_FRAME64) == INTERRUPT64_FRAME_SIZE,
	"x86_64 interrupt frame size mismatch");

void exception_handler64(const struct INTERRUPT_FRAME64 *frame);
void irq_handler64(const struct INTERRUPT_FRAME64 *frame);

#endif
