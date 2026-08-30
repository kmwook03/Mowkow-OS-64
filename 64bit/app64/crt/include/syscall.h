#ifndef MOWKOW_APP64_SYSCALL_H
#define MOWKOW_APP64_SYSCALL_H

#define SYS_EXIT   1
#define SYS_WRITE  2
#define SYS_READ   3
#define SYS_OPEN   4
#define SYS_CLOSE  5
#define SYS_SEEK   6
#define SYS_ALLOC  7
#define SYS_FREE   8
#define SYS_TICKS  9
#define SYS_TTY    10

#define TTY_MODE    0
#define TTY_READKEY 1
#define TTY_SIZE    2
#define TTY_MOVE    3
#define TTY_CLEAR   4
#define TTY_ATTR    5
#define TTY_FLUSH   6

long syscall0(long nr);
long syscall1(long nr, long a0);
long syscall2(long nr, long a0, long a1);
long syscall3(long nr, long a0, long a1, long a2);
long syscall4(long nr, long a0, long a1, long a2, long a3);
long syscall5(long nr, long a0, long a1, long a2, long a3, long a4);
long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

#endif
