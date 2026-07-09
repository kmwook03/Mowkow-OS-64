/**
 * @file mtask.c
 * 
 * @brief 태스크 관리 함수 구현
 * 
 */

#include "../include/bootpack.h"

struct TASKCTL *taskctl;
struct TIMER *task_timer;

struct TASK *task_now(void) 
{
    struct TASKLEVEL *tl = &taskctl->level[taskctl->now_lv];
    return tl->tasks[tl->now];
}

void task_add(struct TASK *task)
{
    struct TASKLEVEL *tl = &taskctl->level[task->level];
    tl->tasks[tl->running] = task;
    tl->running++;
    task->flags = 2; // 실행 중으로 설정
    return;
}

void task_remove(struct TASK *task)
{
    int i;
    struct TASKLEVEL *tl = &taskctl->level[task->level];

    // find the task in the running tasks list
    for (i=0; i<tl->running; i++) {
        if (tl->tasks[i] == task) {
            break;
        }
    }

    tl->running--; // decrease number of running tasks
    if (i < tl->now) {
        tl->now--; // adjust now index
    }
    if (tl->now >= tl->running) {
        tl->now = 0; // adjust now index
    }
    task->flags = 1; // 실행 중이 아님

    for (; i<tl->running; i++) {
        tl->tasks[i] = tl->tasks[i + 1];
    }

    return;
}

/**
 * @brief 태스크 스위칭 보조 함수
 * 
 * 실행 중인 태스크가 있는 가장 높은 레벨을 찾아 현재 레벨로 설정
 * 
 * @return: void
 */
void task_switchsub(void)
{
    int i;
    // 실행 중인 태스크가 있는 가장 높은 레벨 찾기
    for (i = 0; i < MAX_TASKLEVELS; i++) {
        if (taskctl->level[i].running > 0) {
            break;
        }
    }
    taskctl->now_lv = i;        // 현재 레벨 설정
    taskctl->lv_change = 0;     // 레벨 변경 플래그 클리어
    return;
}

void task_idle(void)
{
    for (;;) {
        io_hlt();
    }
}

struct TASK *task_init(struct MEMMAN *memman)
{
    int i;
    struct TASK *task, *idle;
    struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *) ADR_GDT;
    taskctl = (struct TASKCTL *) memman_alloc_4k(memman, sizeof (struct TASKCTL));

    for (i = 0; i < MAX_TASKS; i++) {
        taskctl->tasks0[i].flags = 0; // mark as unused
        taskctl->tasks0[i].sel = (TASK_GDT0 + i) * 8;
        taskctl->tasks0[i].tss.ldtr = (TASK_GDT0 + MAX_TASKS + i) * 8;
        set_segmdesc(gdt + TASK_GDT0 + i, 103, (int) &taskctl->tasks0[i].tss, AR_TSS32);
        set_segmdesc(gdt + TASK_GDT0 + MAX_TASKS + i, 15, (int) taskctl->tasks0[i].ldt, AR_LDT);
    }
    for (i=0; i<MAX_TASKLEVELS; i++) {
        taskctl->level[i].running = 0;
        taskctl->level[i].now = 0;
    }
    task = task_alloc();
    task->flags = 2; // mark as running
    task->priority = 2; // default priority (0.02s)
    task->level = 0; // default level
    task_add(task);
    task_switchsub();
    load_tr(task->sel);
    
    task_timer = timer_alloc();
    timer_settime(task_timer, task->priority);

    idle = task_alloc();
    idle->tss.esp = memman_alloc_4k(memman, 64 * 1024) + 64 * 1024;
    idle->tss.eip = (int) &task_idle;
    idle->tss.es = 1 * 8;
    idle->tss.cs = 2 * 8;
    idle->tss.ss = 1 * 8;
    idle->tss.ds = 1 * 8;
    idle->tss.fs = 1 * 8;
    idle->tss.gs = 1 * 8;
    task_run(idle, MAX_TASKLEVELS - 1, 1); // lowest    

    return task;
}

struct TASK *task_alloc(void)
{
    int i;
    struct TASK *task;
    for (i = 0; i < MAX_TASKS; i++) {
        if (taskctl->tasks0[i].flags == 0) {
            task = &taskctl->tasks0[i];
            task->flags = 1; // mark as allocated
            task->tss.eflags = 0x00000202; // IF = 1
            task->tss.eax = 0;
            task->tss.ecx = 0;
            task->tss.edx = 0;
            task->tss.ebx = 0;
            task->tss.ebp = 0;
            task->tss.esi = 0;
            task->tss.edi = 0;
            task->tss.es  = 0;
            task->tss.ds  = 0;
            task->tss.fs  = 0;
            task->tss.gs  = 0;
            task->tss.iomap= 0x40000000;
            task->tss.ss0 = 0;

            task->fhandle_count = 8;
            int j;
            for (j=0; j<task->fhandle_count; j++) {
                task->fhandle[j].finfo = 0;
                task->fhandle[j].pos = 0;
                task->fhandle[j].cluster = 0;
                task->fhandle[j].modified = 0;
            }
            return task;
        }
    }
    return 0; // all tasks are used
}

void task_run(struct TASK *task, int level, int priority)
{
    if (level < 0) {
        level = task->level; // 현재 레벨 유지
    }
    if (priority > 0) {
        task->priority = priority; // 우선순위 갱신
    }

    if (task->flags == 2 && task->level != level) {
        // 실행 중인 태스크의 레벨 변경
        task_remove(task);
    }
    if (task->flags != 2) {
        // 실행 중인 태스크가 아니면 실행 중인 태스크로 추가
        task->level = level;
        task_add(task);
    }

    taskctl->lv_change = 1; // 레벨 변경 필요
    return;
}

void task_sleep(struct TASK *task)
{
    struct TASK *now_task;
    if (task->flags == 2) {
        now_task = task_now();
        task_remove(task);
        if (task == now_task) {
            // 다음 태스크로 전환
            task_switchsub();
            now_task = task_now();
            farjmp(0, now_task->sel);
        }
    }
    
    return;
}

/**
 * @brief 태스크 스위칭 함수
 * 
 * MLQ + 라운드 로빈(RR) 방식으로 태스크를 전환
 * 
 * @return: void
 */
void task_switch(void)
{
    struct TASKLEVEL *tl = &taskctl->level[taskctl->now_lv];    // 현재 레벨
    struct TASK *new_task, *now_task = tl->tasks[tl->now];
    tl->now++;                      // 현재 레벨의 다음 태스크로 변경
    
    if (tl->now == tl->running) {   // 현재 레벨의 태스크가 모두 실행되었으면
        tl->now = 0;                // 처음으로 돌아감
    }
    if (taskctl->lv_change != 0) {              // 레벨 변경이 필요하면
        task_switchsub();                       // 실행 중인 태스크 중 가장 높은 레벨로 변경
        tl = &taskctl->level[taskctl->now_lv];  // 현재 레벨 갱신
    }
    new_task = tl->tasks[tl->now];                  // 새로운 태스크 선택
    timer_settime(task_timer, new_task->priority);  // 타이머 설정 (RR 방식)
    if (new_task != now_task) {    // 태스크가 변경되었으면
        farjmp(0, new_task->sel);  // 태스크 전환 (TSS를 이용한 하드웨어 태스크 스위칭)
    }
    return;
}
