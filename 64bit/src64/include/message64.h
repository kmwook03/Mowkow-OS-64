#ifndef MOWKOW64_MESSAGE64_H
#define MOWKOW64_MESSAGE64_H

enum MESSAGE_ID64 {
#define MESSAGE64(id, policy, korean, english) MESSAGE_##id,
#include "message64.def"
#undef MESSAGE64
	MESSAGE_COUNT64
};

/* OS errors and notices always use Korean. App/port ERROR entries retain
 * mode selection, with English on lookup failure. Strings have static lifetime.
 * Header-only storage is shared by kernel and freestanding CRT consumers.
 */
static inline const char *message_text64(enum MESSAGE_ID64 id, int korean)
{
#define MESSAGE_NOTICE64(kr, en) (kr)
#define MESSAGE_OS_ERROR64(kr, en) (kr)
#define MESSAGE_ERROR64(kr, en) (korean ? (kr) : (en))
	switch (id) {
#define MESSAGE64(id, policy, kr, en) case MESSAGE_##id: return MESSAGE_##policy##64(kr, en);
#include "message64.def"
#undef MESSAGE64
	default:
		return "";
	}
#undef MESSAGE_ERROR64
#undef MESSAGE_OS_ERROR64
#undef MESSAGE_NOTICE64
}

#endif
