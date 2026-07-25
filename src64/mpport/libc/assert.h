#ifndef MOWKOW64_MPPORT_ASSERT_FAIL_H
#define MOWKOW64_MPPORT_ASSERT_FAIL_H
static inline void __mowkow64_assert_fail(void)
{
	for (;;) {
	}
}
#endif

/* No include guard past this point: assert.h is meant to be re-includable
 * so redefining NDEBUG and re-including it changes assert()'s behavior. */
#undef assert

#ifdef NDEBUG
#define assert(expr) ((void) 0)
#else
#define assert(expr) ((expr) ? (void) 0 : __mowkow64_assert_fail())
#endif
