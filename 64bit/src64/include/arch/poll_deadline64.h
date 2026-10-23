#ifndef MOWKOW64_ARCH_POLL_DEADLINE64_H
#define MOWKOW64_ARCH_POLL_DEADLINE64_H

#include <stddef.h>
#include <stdint.h>

struct POLL_DEADLINE64 {
	uint64_t start, ticks;
};

/* Round up fractional ticks, without multiplying the full frequency. Limit
 * intervals to half the counter range; unsigned elapsed time survives wrap.
 * A rejected deadline is already expired, so ignoring an error cannot hang.
 */
static inline int poll_deadline_init64(struct POLL_DEADLINE64 *deadline,
	uint64_t now, uint64_t frequency, uint32_t duration, uint32_t units)
{
	uint64_t whole, remainder, fraction;

	if (deadline == NULL) { return -22; }
	deadline->start = now;
	deadline->ticks = 0;
	if (frequency == 0 || units == 0) { return -22; }
	whole = frequency / units;
	remainder = (frequency % units) * duration;
	fraction = remainder / units + (remainder % units != 0);
	if (duration != 0 && whole > (uint64_t) INT64_MAX / duration) { return -22; }
	whole *= duration;
	if (fraction > (uint64_t) INT64_MAX - whole) { return -22; }
	deadline->ticks = whole + fraction;
	return 0;
}

static inline int poll_deadline_expired64(const struct POLL_DEADLINE64 *deadline,
	uint64_t now)
{
	return now - deadline->start >= deadline->ticks;
}

#endif
