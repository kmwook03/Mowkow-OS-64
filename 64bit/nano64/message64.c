#include "include/nano_posix64.h"
#include "include/nano_message64.h"

struct NANO_MESSAGE64 { const char *source64; const char *korean64; };
static const struct NANO_MESSAGE64 catalog64[] = {
#define NANO_MESSAGE64(source, korean) { source, korean },
#include "messages64.def"
#undef NANO_MESSAGE64
};

static int language64 = 1;
static unsigned int depth64;

void nano_message_begin64(int error)
{
	/* Nested formatting inherits the outer message's snapshot. TTY lookup
	 * can fail before a console exists; preserve errno and use English then.
	 */
	if (depth64++ == 0) {
		int saved = errno;
		long mode = error ? tty_input_mode() : 1;

		language64 = mode >= 0 && TTY_INPUT_HANGUL(mode);
		errno = saved;
	}
}

void nano_message_end64(void)
{
	if (depth64 != 0 && --depth64 == 0) { language64 = 1; }
}

const char *nano_message64(const char *source)
{
	if (source == NULL) { return source; }
	for (size_t i = 0; i < sizeof(catalog64) / sizeof(catalog64[0]); i++) {
		const struct NANO_MESSAGE64 *entry = &catalog64[i];

		/* Deferred upstream messages may already hold a translated format.
		 * Only registered message formats enter here, never file/user data.
		 */
		if (strcmp(source, entry->source64) == 0 || strcmp(source, entry->korean64) == 0) {
			return language64 ? entry->korean64 : entry->source64;
		}
	}
	return source;
}
