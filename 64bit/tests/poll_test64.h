#ifndef MOWKOW64_TESTS_POLL_TEST64_H
#define MOWKOW64_TESTS_POLL_TEST64_H

#include <arch/aarch64_poll64.h>

/* Device models capture failure calls; poll64_host.c tests the actual logger. */
static struct AARCH64_POLL_RECORD64 poll_last64;
static unsigned int poll_failures64;

void aarch64_poll_fail64(const char *device, const char *stage,
	const char *register_name, uintptr_t address, uint64_t value, int error)
{
	poll_last64 = (struct AARCH64_POLL_RECORD64) {
		device, stage, register_name, address, value, error
	};
	poll_failures64++;
}

#endif
