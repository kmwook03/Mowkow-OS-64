#ifndef MOWKOW64_ARCH_EXCEPTION_FRAME64_H
#define MOWKOW64_ARCH_EXCEPTION_FRAME64_H

#define ARCH64_FRAME_ELR_OFFSET       248
#define ARCH64_FRAME_SPSR_OFFSET      256
#define ARCH64_FRAME_RESERVED_OFFSET  264
#define ARCH64_FRAME_Q_OFFSET         272
#define ARCH64_FRAME_FPCR_OFFSET      784
#define ARCH64_FRAME_FPSR_OFFSET      792
#define ARCH64_EXCEPTION_FRAME_SIZE   800

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

struct ARCH64_EXCEPTION_FRAME {
	uint64_t x[31];
	uint64_t elr;
	uint64_t spsr;
	uint64_t reserved;
	uint64_t q[64];
	uint64_t fpcr;
	uint64_t fpsr;
};

_Static_assert(offsetof(struct ARCH64_EXCEPTION_FRAME, elr) ==
	ARCH64_FRAME_ELR_OFFSET, "AArch64 ELR frame offset mismatch");
_Static_assert(offsetof(struct ARCH64_EXCEPTION_FRAME, spsr) ==
	ARCH64_FRAME_SPSR_OFFSET, "AArch64 SPSR frame offset mismatch");
_Static_assert(offsetof(struct ARCH64_EXCEPTION_FRAME, q) ==
	ARCH64_FRAME_Q_OFFSET, "AArch64 Q-register frame offset mismatch");
_Static_assert(offsetof(struct ARCH64_EXCEPTION_FRAME, fpcr) ==
	ARCH64_FRAME_FPCR_OFFSET, "AArch64 FPCR frame offset mismatch");
_Static_assert(offsetof(struct ARCH64_EXCEPTION_FRAME, fpsr) ==
	ARCH64_FRAME_FPSR_OFFSET, "AArch64 FPSR frame offset mismatch");
_Static_assert(sizeof(struct ARCH64_EXCEPTION_FRAME) ==
	ARCH64_EXCEPTION_FRAME_SIZE, "AArch64 exception frame size mismatch");
#endif

#endif
