#ifndef MOWKOW64_PROCESS_FILE64_H
#define MOWKOW64_PROCESS_FILE64_H

#include <process64.h>
#include <mutex64.h>

/* Temporary read-only OFD: task context, lock before the FAT/cache mutex.
 * An installed descriptor retains a reference through sleeping I/O. One
 * execution flow per process; close/exec cannot race that process's syscall.
 */
struct UNIX_FILE64 {
	unsigned int references;
	struct MUTEX64 mutex;
	struct FDHANDLE64 handle;
};

int process_file_inherit64(struct PROCESS64 *parent, struct PROCESS64 *child);
void process_file_close64(struct PROCESS64_FILE *file);
void process_files_close64(struct PROCESS64 *process, int cloexec_only);
int process_file_lock64(struct PROCESS64_FILE *file, struct FDHANDLE64 **handle);
void process_file_unlock64(struct PROCESS64_FILE *file);

#endif
