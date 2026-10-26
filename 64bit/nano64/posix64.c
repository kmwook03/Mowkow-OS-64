#include <mowos.h>
static int crt_close64(int fd) { return close(fd); }
static long crt_write64(int fd, const void *data, size_t count) { return write(fd, data, count); }
#include "include/nano_posix64.h"

static int unsupported64(void) { errno = ENOSYS; return -1; }
static long result64(long result) { if (result < 0) { errno = EIO; } return result; }

static long checked_result64(long result)
{
	if (result < 0) { errno = (int) -result; return -1; }
	return result;
}
int nano_open64(const char *path, int flags, ...)
{
	if ((flags & ~(O_CREAT | O_TRUNC | O_WRONLY)) != 0) { return unsupported64(); }
	const char *name = nano_file_path64(path);

	if (name == NULL) { return -1; }
	if (strcmp(name, "/") == 0) { errno = EISDIR; return -1; }
	return (int) checked_result64(file_call64(FILE_OPEN64, (unsigned long) name,
		(unsigned long) flags, 0));
}
long nano_read64(int fd, void *data, size_t count)
{
	return checked_result64(file_call64(FILE_READ64, (unsigned long) fd,
		(unsigned long) data, count));
}
long nano_write64(int fd, const void *data, size_t count)
{
	return result64(crt_write64(fd, data, count));
}
int nano_close64(int fd) { return (int) result64(crt_close64(fd)); }
int nano_dup264(int from, int to) { (void) from; (void) to; return unsupported64(); }

/* FAT timestamp conversion; unavailable/invalid dates remain zero. There is
 * no access timestamp or uid/gid in this ABI: zero time and UINT_MAX owner
 * explicitly denote unavailable fields, not host-derived attributes.
 */
static int leap64(unsigned year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static time_t fat_time64(unsigned date, unsigned stamp)
{
	static const unsigned char days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	unsigned year = 1980 + (date >> 9), month = (date >> 5) & 15, day = date & 31;
	unsigned hour = stamp >> 11, minute = (stamp >> 5) & 63, second = (stamp & 31) * 2;
	time_t total = 0;

	if (!month || month > 12 || !day ||
		day > days[month - 1] + (unsigned) (month == 2 && leap64(year)) ||
		hour > 23 || minute > 59 || second > 59) { return 0; }
	for (unsigned y = 1970; y < year; y++) { total += 365 + leap64(y); }
	for (unsigned m = 1; m < month; m++) { total += days[m - 1] + (m == 2 && leap64(year)); }
	return ((total + day - 1) * 24 + hour) * 3600 + minute * 60 + second;
}
int nano_stat64(const char *path, struct nano_stat64 *info)
{
	struct FILE_INFO64 raw;
	const char *name = nano_file_path64(path);

	if (info == NULL) { errno = EINVAL; return -1; }
	if (name == NULL) { return -1; }
	if (checked_result64(file_call64(FILE_STAT64, (unsigned long) name,
		(unsigned long) &raw, 0)) < 0) { return -1; }
	struct nano_stat64 local = { 0 };

	/* POSIX permission bits are a view of FAT read-only and volume state. */
	local.st_mode = (raw.flags64 & FILE_DIRECTORY64 ? S_IFDIR | 0111 : S_IFREG) | 0444;
	if (!(raw.flags64 & (FILE_READONLY64 | FILE_VOLUME_READONLY64))) { local.st_mode |= 0222; }
	local.st_size = raw.size64;
	local.st_ino = raw.identity64;
	local.st_dev = 1;
	local.st_uid = (uid_t) -1;
	local.st_gid = (gid_t) -1;
	local.st_mtim.tv_sec = fat_time64(raw.date64, raw.time64);
	*info = local;
	return 0;
}
int nano_access64(const char *path, int mode)
{
	struct nano_stat64 info;

	if (mode & ~(R_OK | W_OK | X_OK)) { errno = EINVAL; return -1; }
	if (nano_stat64(path, &info) < 0) { return -1; }
	if (((mode & W_OK) && !(info.st_mode & 0222)) ||
		((mode & X_OK) && !S_ISDIR(info.st_mode))) { errno = EACCES; return -1; }
	return 0;
}
int nano_fsync64(int fd)
{
	return (int) checked_result64(file_call64(FILE_SYNC64, (unsigned long) fd, 0, 0));
}
int nano_unlink64(const char *path) { (void) path; return unsupported64(); }
int nano_mkstemp64(char *path) { (void) path; return unsupported64(); }
int nano_mkstemps64(char *path, int suffix)
{
	(void) path; (void) suffix; return unsupported64();
}
int nano_fchmod64(int fd, mode_t mode) { (void) fd; (void) mode; return unsupported64(); }
int nano_fchown64(int fd, uid_t uid, gid_t gid)
{
	(void) fd; (void) uid; (void) gid; return unsupported64();
}
int nano_futimens64(int fd, const struct timespec times[2])
{
	(void) fd; (void) times; return unsupported64();
}
char *nano_realpath64(const char *path, char *resolved)
{
	struct nano_stat64 info;
	const char *name = nano_file_path64(path);

	if (name == NULL || nano_stat64(path, &info) < 0) { return NULL; }
	size_t length = strlen(name);
	char *out = resolved != NULL ? resolved : malloc(length + 2);

	if (out == NULL) { errno = ENOMEM; return NULL; }
	out[0] = '/';
	if (strcmp(name, "/") == 0) { out[1] = 0; }
	else { memcpy(out + 1, name, length + 1); }
	return out;
}
uid_t nano_geteuid64(void) { unsupported64(); return (uid_t) -1; }
pid_t nano_getpid64(void) { return unsupported64(); }
int nano_gethostname64(char *name, size_t capacity)
{
	(void) name; (void) capacity; return unsupported64();
}

char *nano_basename64(char *path)
{
	if (path == NULL || *path == '\0') { return "."; }
	size_t length = strlen(path);

	while (length > 1 && path[length - 1] == '/') { path[--length] = '\0'; }
	char *slash = nano_strrchr64(path, '/');

	return slash == NULL ? path : (slash[1] == '\0' ? slash : slash + 1);
}
char *nano_dirname64(char *path)
{
	if (path == NULL || *path == '\0') { return "."; }
	size_t length = strlen(path);

	while (length > 1 && path[length - 1] == '/') { path[--length] = '\0'; }
	char *slash = nano_strrchr64(path, '/');

	if (slash == NULL) { return "."; }
	while (slash > path && slash[-1] == '/') { slash--; }
	if (slash == path) { path[1] = '\0'; }
	else { *slash = '\0'; }
	return path;
}

int nano_isatty64(int fd)
{
	return fd >= 0 && fd <= 2 && TTY_SIZE_COLS(tty_size()) != 0;
}
/* Applications receive argv and a console, with no inherited environment. */
char *nano_getenv64(const char *name) { (void) name; return NULL; }
int nano_putenv64(char *setting) { (void) setting; return unsupported64(); }
char *nano_setlocale64(int category, const char *locale)
{
	if ((category != LC_ALL && category != LC_CTYPE) ||
		(locale != NULL && *locale != '\0' && strcmp(locale, "C.UTF-8") != 0)) {
		errno = EINVAL; return NULL;
	}
	return "C.UTF-8";
}
char *nano_nl_langinfo64(int item)
{
	if (item == CODESET) { return "UTF-8"; }
	errno = EINVAL; return "";
}
time_t nano_time64(time_t *result)
{
	/* Monotonic seconds since boot, not a wall clock. Both targets use 100 Hz. */
	time_t value = (time_t) (ticks() / 100UL);

	if (result != NULL) { *result = value; }
	return value;
}
char *nano_strerror64(int error)
{
	switch (error) {
	case 0: return "Success";
	case EINTR: return "Interrupted";
	case ENOENT: return "No such file";
	case EACCES: return "Read-only file or volume";
	case ENOTDIR: return "Only the root directory is supported";
	case EISDIR: return "Is a directory";
	case ENAMETOOLONG: return "Filename too long";
	case EIO: return "Input/output error";
	case EBADF: return "Bad file descriptor";
	case ENOMEM: return "Out of memory";
	case EINVAL: return "Invalid argument";
	case EMFILE: return "Too many open streams";
	case EOVERFLOW: return "Value too large";
	case EILSEQ: return "Invalid UTF-8";
	case ENOSYS: return "Unsupported operation";
	default: return "Unknown error";
	}
}

int nano_regcomp64(regex_t *regex, const char *pattern, int flags)
{
	(void) regex; (void) pattern; (void) flags; unsupported64(); return REG_BADPAT;
}
int nano_regexec64(const regex_t *regex, const char *text, size_t count,
	regmatch_t *matches, int flags)
{
	(void) regex; (void) text; (void) count; (void) matches; (void) flags;
	unsupported64(); return REG_BADPAT;
}
size_t nano_regerror64(int error, const regex_t *regex, char *text, size_t capacity)
{
	const char message[] = "Regular expressions are unavailable";

	(void) error; (void) regex;
	if (capacity != 0 && text != NULL) { str_copy64(text, capacity, message); }
	return sizeof(message);
}
void nano_regfree64(regex_t *regex)
{
	/* No compiled object is ever published by regcomp. */
	(void) regex;
}
int nano_sigemptyset64(sigset_t *set)
{
	if (set == NULL) { errno = EINVAL; return -1; } *set = 0; return 0;
}
int nano_sigfillset64(sigset_t *set)
{
	if (set == NULL) { errno = EINVAL; return -1; } *set = ~0UL; return 0;
}
int nano_sigaddset64(sigset_t *set, int signal)
{
	if (set == NULL || signal <= 0 || signal > 64) { errno = EINVAL; return -1; }
	*set |= 1UL << (signal - 1); return 0;
}
int nano_sigprocmask64(int operation, const sigset_t *set, sigset_t *old)
{
	(void) operation; (void) set; (void) old; return unsupported64();
}
int nano_sigaction64(int signal, const struct nano_sigaction64 *action,
	struct nano_sigaction64 *old)
{
	(void) signal; (void) action; (void) old; return unsupported64();
}

char *nano_optarg64;
int nano_optind64 = 1;
static size_t short_at64;

int nano_getopt_long64(int argc, char *const argv[], const char *shorts,
	const struct option *longs, int *index)
{
	nano_optarg64 = NULL;
	if (nano_optind64 < 1) { nano_optind64 = 1; short_at64 = 0; }
	if (nano_optind64 >= argc) { return -1; }
	char *arg = argv[nano_optind64];

	if (short_at64 == 0) {
		if (arg[0] != '-' || arg[1] == '\0') { return -1; }
		if (arg[1] == '-' && arg[2] == '\0') { nano_optind64++; return -1; }
		if (arg[1] == '-') {
			char *equal = nano_strchr64(arg + 2, '=');
			size_t length = equal != NULL ? (size_t) (equal - arg - 2) : strlen(arg + 2);

			nano_optind64++;
			for (int i = 0; longs[i].name != NULL; i++) {
				if (strlen(longs[i].name) != length ||
					strncmp(longs[i].name, arg + 2, length) != 0) { continue; }
				if (index != NULL) { *index = i; }
				if (longs[i].has_arg == 0 && equal != NULL) { return '?'; }
				if (equal != NULL) { nano_optarg64 = equal + 1; }
				else if (longs[i].has_arg == 1) {
					if (nano_optind64 >= argc) { return '?'; }
					nano_optarg64 = argv[nano_optind64++];
				}
				if (longs[i].flag != NULL) { *longs[i].flag = longs[i].val; return 0; }
				return longs[i].val;
			}
			return '?';
		}
		short_at64 = 1;
	}
	int option = (unsigned char) arg[short_at64++];
	const char *entry = nano_strchr64(shorts, option);

	if (arg[short_at64] == '\0') { nano_optind64++; short_at64 = 0; }
	if (entry == NULL || option == ':') { return '?'; }
	if (entry[1] == ':') {
		if (short_at64 != 0) {
			nano_optarg64 = arg + short_at64; nano_optind64++; short_at64 = 0;
		} else if (entry[2] != ':') {
			if (nano_optind64 >= argc) { return '?'; }
			nano_optarg64 = argv[nano_optind64++];
		}
	}
	return option;
}
