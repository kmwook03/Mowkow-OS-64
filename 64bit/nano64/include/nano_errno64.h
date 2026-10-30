#ifndef MOWKOW64_NANO_ERRNO64_H
#define MOWKOW64_NANO_ERRNO64_H

/* Private, positive libc-style errors, separate from the CRT's legacy
 * negative syscall results. The nano application has one execution context.
 */
#define NANO_EIO64 5
#define NANO_EBADF64 9
#define NANO_ENOTDIR64 20
#define NANO_ENAMETOOLONG64 36
#define NANO_ENOMEM64 12
#define NANO_EINVAL64 22
#define NANO_EMFILE64 24
#define NANO_ENOSYS64 38
#define NANO_EOVERFLOW64 75
#define NANO_EILSEQ64 84

extern int nano_errno64;

#endif
