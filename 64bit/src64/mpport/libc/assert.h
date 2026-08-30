#ifndef MOWKOW64_MPPORT_ASSERT_FAIL_H
#define MOWKOW64_MPPORT_ASSERT_FAIL_H
static inline void __mowkow64_assert_fail(void)
{
	for (;;) {
	}
}
#endif

/* 여기서부터는 포함 보호(include guard)를 두지 않는다. assert.h는 여러 번
 * 포함할 수 있어야 하고, NDEBUG를 다시 정의한 뒤 다시 포함하면 assert()의
 * 동작이 바뀌어야 하기 때문이다. */
#undef assert

#ifdef NDEBUG
#define assert(expr) ((void) 0)
#else
#define assert(expr) ((expr) ? (void) 0 : __mowkow64_assert_fail())
#endif
