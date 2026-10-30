#ifndef MOWKOW64_NANO_POSIX64_H
#define MOWKOW64_NANO_POSIX64_H

#include <mowos.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "nano_file64.h"
#include "nano_utf64.h"

typedef long ssize_t;
typedef long off_t;
typedef long time_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef uint32_t wint_t;
typedef int sig_atomic_t;
typedef unsigned long sigset_t;

#define FILE struct NANO_FILE64
#define DIR struct NANO_DIR64
#define regex_t struct NANO_REGEX64
#define regmatch_t struct NANO_REGMATCH64
#define regoff_t long
#define stat nano_stat64
#define sigaction nano_sigaction64
struct timespec { time_t tv_sec; long tv_nsec; };
struct nano_stat64 {
	mode_t st_mode;
	off_t st_size;
	uid_t st_uid;
	gid_t st_gid;
	unsigned long st_dev, st_ino;
	struct timespec st_atim, st_mtim;
};
#define st_mtime st_mtim.tv_sec
struct nano_sigaction64 {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	int sa_flags;
};
struct NANO_REGEX64 { size_t re_nsub; };
struct NANO_REGMATCH64 { long rm_so, rm_eo; };
struct option { const char *name; int has_arg; int *flag; int val; };

#define errno nano_errno64
#define EOF NANO_EOF64
#define BUFSIZ 512
#define MB_CUR_MAX 4
#define MB_LEN_MAX 4
#define P_tmpdir "/tmp/"
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define O_RDONLY 0
#define O_WRONLY 4
#define O_RDWR 8
#define O_APPEND 16
#define O_EXCL 32
#define O_NONBLOCK 64
#define O_NOFOLLOW 128
#define O_BINARY 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFIFO 0010000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m) 0
#define S_ISBLK(m) 0
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IRWXU 0700
#define LC_ALL 0
#define LC_CTYPE 1
#define CODESET 1
#define REG_ICASE 2
#define REG_STARTEND 4
#define REG_NOTBOL 8
#define REG_NOMATCH 1
#define REG_BADPAT 2
#define SIGINT 2
#define SIGTERM 15
#define SIGSEGV 11
#define SIGABRT 6
#define SIG_IGN ((void (*)(int)) 1)
#define SA_RESETHAND 1
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define TCSANOW 0
#define EINTR 4
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define ENOSPC 28
#define ERANGE 34
#define ENOSYS 38
#define ENAMETOOLONG 36
#define EOVERFLOW 75
#define EILSEQ 84
#define ENOENT 2

extern FILE *nano_stdout64;
extern FILE *nano_stderr64;
#define stdout nano_stdout64
#define stderr nano_stderr64

/* Compatibility signatures are restricted to vendor calls; ordinary port
 * code uses bounded builders. No hosted ABI structs are imported.
 */
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
int sprintf(char *dst, const char *format, ...);
#ifdef malloc
#undef malloc
#endif
#define malloc nano_malloc64
void *nano_malloc64(size_t size);
#ifdef realloc
#undef realloc
#endif
#define realloc nano_realloc64
void *nano_realloc64(void *ptr, size_t size);
#ifdef free
#undef free
#endif
#define free nano_free64
void nano_free64(void *ptr);
#define memmove nano_memmove64
void *nano_memmove64(void *dst, const void *src, size_t size);
#define strncpy nano_strncpy64
char *nano_strncpy64(char *dst, const char *src, size_t size);
#define strchr nano_strchr64
char *nano_strchr64(const char *text, int byte);
#define strrchr nano_strrchr64
char *nano_strrchr64(const char *text, int byte);
#define strstr nano_strstr64
char *nano_strstr64(const char *text, const char *needle);
#define strpbrk nano_strpbrk64
char *nano_strpbrk64(const char *text, const char *accept);
#define strcasecmp nano_strcasecmp64
int nano_strcasecmp64(const char *left, const char *right);
#define strncasecmp nano_strncasecmp64
int nano_strncasecmp64(const char *left, const char *right, size_t count);
#define strcasestr nano_strcasestr64
char *nano_strcasestr64(const char *text, const char *needle);
#define strerror nano_strerror64
char *nano_strerror64(int error);
#define strtol nano_strtol64
long nano_strtol64(const char *text, char **end, int base);
#define snprintf nano_snprintf64
int nano_snprintf64(char *dst, size_t capacity, const char *format, ...)
	__attribute__((format(__printf__, 3, 4)));
#define vsnprintf nano_vsnprintf64
int nano_vsnprintf64(char *dst, size_t capacity, const char *format, va_list args)
	__attribute__((format(__printf__, 3, 0)));
#define printf nano_printf64
int nano_printf64(const char *format, ...)
	__attribute__((format(__printf__, 1, 2)));
#define fprintf nano_fprintf64
int nano_fprintf64(FILE *stream, const char *format, ...)
	__attribute__((format(__printf__, 2, 3)));
#define vfprintf nano_vfprintf64
int nano_vfprintf64(FILE *stream, const char *format, va_list args)
	__attribute__((format(__printf__, 2, 0)));
#define fputc nano_fputc64
int nano_fputc64(int byte, FILE *stream);
#define putchar nano_putchar64
int nano_putchar64(int byte);
#ifdef open
#undef open
#endif
#define open nano_open64
int nano_open64(const char *path, int flags, ...);
#ifdef read
#undef read
#endif
#define read nano_read64
long nano_read64(int fd, void *data, size_t count);
#ifdef write
#undef write
#endif
#define write nano_write64
long nano_write64(int fd, const void *data, size_t count);
#ifdef close
#undef close
#endif
#define close nano_close64
int nano_close64(int fd);
#define fsync nano_fsync64
int nano_fsync64(int fd);
#define access nano_access64
int nano_access64(const char *path, int mode);
#define realpath nano_realpath64
char *nano_realpath64(const char *path, char *resolved);
#define basename nano_basename64
char *nano_basename64(char *path);
#define dirname nano_dirname64
char *nano_dirname64(char *path);
#define unlink nano_unlink64
int nano_unlink64(const char *path);
#define mkstemp nano_mkstemp64
int nano_mkstemp64(char *path);
#define mkstemps nano_mkstemps64
int nano_mkstemps64(char *path, int suffix);
#define fchmod nano_fchmod64
int nano_fchmod64(int fd, mode_t mode);
#define fchown nano_fchown64
int nano_fchown64(int fd, uid_t uid, gid_t gid);
#define futimens nano_futimens64
int nano_futimens64(int fd, const struct timespec times[2]);
#define isatty nano_isatty64
int nano_isatty64(int fd);
#define getenv nano_getenv64
char *nano_getenv64(const char *name);
#define putenv nano_putenv64
int nano_putenv64(char *setting);
#define geteuid nano_geteuid64
uid_t nano_geteuid64(void);
#define getpid nano_getpid64
pid_t nano_getpid64(void);
#define gethostname nano_gethostname64
int nano_gethostname64(char *name, size_t capacity);
#define setlocale nano_setlocale64
char *nano_setlocale64(int category, const char *locale);
#define nl_langinfo nano_nl_langinfo64
char *nano_nl_langinfo64(int item);
#define time nano_time64
time_t nano_time64(time_t *result);
#define regcomp nano_regcomp64
int nano_regcomp64(regex_t *regex, const char *pattern, int flags);
#define regexec nano_regexec64
int nano_regexec64(const regex_t *regex, const char *text, size_t count,
	regmatch_t *matches, int flags);
#define regerror nano_regerror64
size_t nano_regerror64(int error, const regex_t *regex, char *text, size_t capacity);
#define regfree nano_regfree64
void nano_regfree64(regex_t *regex);
#define sigemptyset nano_sigemptyset64
int nano_sigemptyset64(sigset_t *set);
#define sigfillset nano_sigfillset64
int nano_sigfillset64(sigset_t *set);
#define sigaddset nano_sigaddset64
int nano_sigaddset64(sigset_t *set, int signal);
#define sigprocmask nano_sigprocmask64
int nano_sigprocmask64(int operation, const sigset_t *set, sigset_t *old);
int nano_sigaction64(int signal, const struct nano_sigaction64 *action,
	struct nano_sigaction64 *old);
#define getopt_long nano_getopt_long64
int nano_getopt_long64(int argc, char *const argv[], const char *shorts,
	const struct option *longs, int *index);
#define isalpha nano_isalpha64
int nano_isalpha64(int byte);
#define isalnum nano_isalnum64
int nano_isalnum64(int byte);
#define isblank nano_isblank64
int nano_isblank64(int byte);
#define isdigit nano_isdigit64
int nano_isdigit64(int byte);
#define isspace nano_isspace64
int nano_isspace64(int byte);
#define ispunct nano_ispunct64
int nano_ispunct64(int byte);
#define iscntrl nano_iscntrl64
int nano_iscntrl64(int byte);
#define isxdigit nano_isxdigit64
int nano_isxdigit64(int byte);
#define tolower nano_tolower64
int nano_tolower64(int byte);
#define toupper nano_toupper64
int nano_toupper64(int byte);
#define iswalnum nano_iswalnum64
int nano_iswalnum64(wint_t scalar);
#define iswblank nano_iswblank64
int nano_iswblank64(wint_t scalar);
#define iswpunct nano_iswpunct64
int nano_iswpunct64(wint_t scalar);
#define towlower nano_towlower64
wint_t nano_towlower64(wint_t scalar);
#define wcwidth nano_libc_wcwidth64
int nano_libc_wcwidth64(wchar_t scalar);
#define wctomb nano_libc_wctomb64
int nano_libc_wctomb64(char *text, wchar_t scalar);
#define fopen nano_fopen64
#define fdopen nano_fdopen64
#define fread nano_fread64
#define fwrite nano_fwrite64
#define fclose nano_fclose64
#define fflush nano_fflush64
#define ferror nano_ferror64
#define feof nano_feof64
#define clearerr nano_clearerr64
#define fileno nano_fileno64
#define getc nano_getc64
#define putc nano_putc64

#define optarg nano_optarg64
#define optind nano_optind64
extern char *nano_optarg64;
extern int nano_optind64;
#define atoi nano_atoi64
int nano_atoi64(const char *text);
#define dup2 nano_dup264
int nano_dup264(int from, int to);
int nano_stat64(const char *path, struct nano_stat64 *info);

#endif
