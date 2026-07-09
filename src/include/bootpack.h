#include "asmfunc.h"
#include "hangul.h"
#include "dsctbl.h"
#include "int.h"
#include "input.h"
#include "memory.h"
#include "sheet.h"
#include "timer.h"
#include "graphic.h"
#include "window.h"
#include "fd.h"

// font data
extern char hankaku[4096];
extern unsigned char *system_font;

// boot info struct (asmhead.nas)
struct BOOTINFO {
    char cyls, leds, vmode, reserve; // boot info
    short scrnx, scrny; // screen resolution
    char *vram; // video RAM address
};

#define ADR_BOOTINFO    0x00000ff0
#define ADR_DISKIMG     0x00100000

// fifo.c
struct FIFO32 {
    int *buf;
    int p, q, size, free, flags;
    struct TASK *task;
};

void fifo32_init(struct FIFO32 *fifo, int size, int *buf, struct TASK *task);
int fifo32_put(struct FIFO32 *fifo, int data);
int fifo32_get(struct FIFO32 *fifo);
int fifo32_status(struct FIFO32 *fifo);

#define ADR_IDT         0x0026f800
#define LIMIT_IDT       0x000007ff
#define ADR_GDT         0x00270000
#define LIMIT_GDT       0x0000ffff
#define ADR_BOTPAK      0x00280000
#define LIMIT_BOTPAK    0x0007ffff
#define AR_DATA32_RW    0x4092
#define AR_CODE32_ER    0x409a
#define AR_LDT          0x0082
#define AR_TSS32        0x0089
#define AR_INTGATE32    0x008e


// mtask.c
#define MAX_TASKS   1000    // max number of tasks
#define TASK_GDT0   3       // first TSS GDT number
#define MAX_TASKS_LV  100   // max number of tasks per level
#define MAX_TASKLEVELS  10  // max number of task levels

struct TSS32 {              // Task State Segment
	int backlink, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
	int eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
	int es, cs, ss, ds, fs, gs;
	int ldtr, iomap;
};

struct TASK {
    int sel, flags;                     // GDT 번호 셀렉터, 태스크 상태 플래그
    int level, priority;                // 레벨, 우선순위
    struct FIFO32 fifo;                 // 태스크용 FIFO
    struct TSS32 tss;                   // 태스크 상태 세그먼트
    struct SEGMENT_DESCRIPTOR ldt[2];   // LDT
    struct CONSOLE *cons;               // 태스크와 관련된 콘솔
    int ds_base, cons_stack;            // 데이터 세그먼트 베이스 주소
    // struct FILEHANDLE *fhandle;         // 파일 핸들
    
    struct FDHANDLE *fhandle;
    int fhandle_count;

    // int *fat;                           // 파일 할당 테이블
    char *cmdline;                      // 명령어 버퍼
    char langmode;                      // 언어 모드 (0: 영어, 1: 한국어)
    struct HANGUL hangul;               // 한글 오토마타 상태
};

struct TASKLEVEL {
    int running;                        // number of running tasks
    int now;                            // current running task index
    struct TASK *tasks[MAX_TASKS_LV];   // addresses of tasks
};

struct TASKCTL {
    int now_lv;                                 // current level
    char lv_change;                             // flag for level change
    struct TASKLEVEL level[MAX_TASKLEVELS];     // task levels
    struct TASK tasks0[MAX_TASKS];              // actual tasks
};
extern struct TASKCTL *taskctl;
extern struct TIMER *task_timer;
struct TASK *task_now(void);
struct TASK *task_init(struct MEMMAN *memman);
struct TASK *task_alloc(void);
void task_run(struct TASK *task, int level, int priority);
void task_switch(void);
void task_sleep(struct TASK *task);


// console.c
struct CONSOLE {
    struct SHEET *sht;
    int cur_x, cur_y, cur_c, cur_width;
    int cmd_pos;
    struct TIMER *timer;
};

void console_task(struct SHEET *sht, int memtotal, int langmode);
void cons_putchar(struct CONSOLE *cons, int chr, char move);
void cons_put_utf8(struct CONSOLE *cons, char *s, int len, char move);
void cons_putstr(struct CONSOLE *cons, char *s);
void cons_newline(struct CONSOLE *cons);
void cons_runcmd(char *cmdline, struct CONSOLE *cons, int memtotal);
void cmd_mem(struct CONSOLE *cons, int memtotal);
void cmd_cls(struct CONSOLE *cons);
void cmd_dir(struct CONSOLE *cons);
void cmd_exit(struct CONSOLE *cons);
void cmd_start(struct CONSOLE *cons, char *cmdline, int memtotal, int langmode);
void cmd_ncst(struct CONSOLE *cons, char *cmdline, int memtotal, int langmode);
void cmd_langmode(struct CONSOLE *cons, char *cmdline);
void hrb_api_linewin(struct SHEET *sht, int x0, int y0, int x1, int y1, int col);
int cmd_app(struct CONSOLE *cons, char *cmdline);
int *hrb_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax);
int *inthandler0c(int *esp);
int *inthandler0d(int *esp);

// 콘솔 윈도우의 높이는 16의 배수여야 함. (스크롤 함수 때문) (512 * 308, 496 * 272)
#define CONSOLE_WIDTH 512
#define CONSOLE_HEIGHT 320
#define CONSOLE_TBOX_WIDTH 496
#define CONSOLE_TBOX_HEIGHT 288

// bootpack.c
struct TASK *open_constask(struct SHEET *sht, unsigned int memtotal, int langmode);
struct SHEET *open_console(struct SHTCTL *shtctl, unsigned int memtotal, int langmode);
