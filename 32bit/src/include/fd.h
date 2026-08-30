// Handling Floppy Disk image.

#ifndef _FD_H_
#define _FD_H_

typedef struct {
  unsigned char name[8], ext[3], type, reserve[10];
  unsigned short time, date, clustno;
  unsigned int size;
} FDINFO;

typedef struct FDHANDLE {
  FDINFO* finfo;
  int pos;
  short cluster;
  char modified;
} FDHANDLE;

void init_fdc();
int fd_open(FDHANDLE* fh, const char* name);
int fd_writeopen(FDHANDLE* fh, const char* filename);
int fd_read(FDHANDLE* fh, void* dst, int requestSize);
int fd_write(FDHANDLE* fh, const void* src, int requestSize);
void fd_seek(FDHANDLE* fh, int offset, int origin);
void fd_close(FDHANDLE* fh);
int fd_delete(const char* filename);
void inthandler26(int *esp);

short get_next_cluster(short cluster);

// tek.c
int tek_getsize(unsigned char *p);
int tek_decomp(unsigned char *p, char *q, int size);

#endif // _FD_H_
