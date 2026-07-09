/**
 * @file fd.c
 * 
 * @brief FDC(Floppy Disk Controller) 관련 함수 구현
 * 
 */

#include "../include/bootpack.h"
#include "../include/fd.h"
#include <string.h>
#include <stdio.h>

#define MAX_CLUSTER     2880
#define CLUSTER_SIZE    512

#define FINFO_TOP       ((FDINFO*)(ADR_DISKIMG + 0x002600))
#define FINFO_MAX       224

#define DISK_FAT             (unsigned char*)(ADR_DISKIMG + 0x000200)
#define DISK_CLUSTER_DATA    (unsigned char*)(ADR_DISKIMG + 0x003e00)

#define CLUSTNO_FAT1     (1)       // FAT1:    0x000200 - 0x001400  ( 1~ 9)
#define CLUSTNO_FAT2     (10)      // FAT2:    0x001400 - 0x002600  (10~18)
#define CLUSTNO_ROOTDIR  (19)      // ROOTDIR: 0x002600 - 0x004200  (19~32)
#define CLUSTNO_ENTITY   (33 - 2)  // ENTITY:  0x004200 -           (33~)  (Cluster starts from 2)

static volatile unsigned int fdc_interrupt = 0; // for 0x26 FDC interrupt

static int fdc_chk_interrupt() 
{
    return fdc_interrupt;
}

static unsigned int fdc_wait_interrupt() 
{
    int timeout = 1000000;
    while (!fdc_chk_interrupt()) {
        timeout--;
        if (timeout == 0) {
            // sysPrints("\n[PANIC] FDC Interrupt Timeout!\n");
            return 0; // timeout
        }
    }
    return 1;
}

static void fdc_clear_interrupt() 
{
    fdc_interrupt = 0;
}

// IRQ-06 : FDC
void inthandler26(int *esp) 
{
    // sysPrints("IRQ6 ");
    ++fdc_interrupt;
    io_out8(PIC0_OCW2, 0x66);  // Notify IRQ-06 recv finish to PIC0
    return;
}

static void sysPrintc(int c) 
{
    struct CONSOLE* cons = task_now()->cons;
    char buf[2];
    sprintf(buf, "%c", c);
    cons_put_utf8(cons, buf, 1, 1);
}

static void sysPrints(const char* str) 
{
    struct CONSOLE* cons = task_now()->cons;
    cons_putstr(cons, str);
}

static void sysPrintBin(int x) 
{
    struct CONSOLE* cons = task_now()->cons;
    char buf[2];
    sprintf(buf, "%02x", x);
    cons_put_utf8(cons, buf, 1, 1);
}

#define DMA_DATABUF     1024
#define FDC_RESULT_MAXCOUNT 0x10

#define DMA_ADD_SEC     0x04  //channel2 low address
#define DMA_CNT_SEC     0x05  //channel2 count address
#define DMA_TOP         0x81  //channel2 high address

#define DMA_CMD_PRI     0xD0
#define DMA_CMD_SEC     0x08
#define DMA_REQ_PRI     0xD2
#define DMA_REQ_SEC     0x09
#define DMA_SGL_MSK_PRI 0xD4
#define DMA_SGL_MSK_SEC 0x0A
#define DMA_MOD_PRI     0xD6
#define DMA_MOD_SEC     0x0B
#define DMA_CLR_FLP_PRI 0x0C
#define DMA_CLR_FLP_SEC 0xD8
#define DMA_MSR_CLR_PRI 0xDA
#define DMA_MSR_CLR_SEC 0x0D
#define DMA_CLR_MSK_PRI 0xDC
#define DMA_CLR_MSK_SEC 0x0E
#define DMA_ALL_MSK_PRI 0xDE
#define DMA_ALL_MSK_SEC 0x0F


#define FDC_SRA  0x3f0  // FDC status registerA (R)
#define FDC_SRB  0x3f1  // FDC status registerB (R)
#define FDC_DOR  0x3f2  // FDC Control register (R/W)
#define FDC_MSR  0x3f4  // FDC Status register (R)
#define FDC_DSR  0x3f4  // FDC data rate select register (W)
#define FDC_DAT  0x3f5  // FDC Data(R/W)
#define FDC_DIR  0x3f7  // FDC digital input register (R)
#define FDC_CCR  0x3f7  // FDC configuration control register (W)

#define MSR_RQM    0x80
#define MSR_DIO    0x40
#define MSR_BUSY   0x10
#define MSR_READY  0

/* FDC CMD */
#define CMD_SPECIFY         0x03
#define CMD_RECALIBRATE     0x07
#define CMD_SENSE_INT_STS   0x08
#define CMD_SEEK            0x0f
#define CMD_READ            0x46  //MT=0,MF=1,SK=0
#define CMD_WRITE           0x45  //MT=0,MF=1,SK=0

#define CMD_SUB 0x00 //HD=0, US1 & US0 = 0

static unsigned char dma_databuf[DMA_DATABUF];

static struct _dma_trans {
    unsigned int count;
    unsigned int addr;
} dma_trans;

static struct FDC_RESULTS {
    unsigned char gets;
    unsigned char req_sense;
    unsigned int status_count;
    unsigned char status[10];
} fdc_results;

static void fdc_dma_start() 
{
    io_out8(DMA_SGL_MSK_SEC, 0x02);
}

static void fdc_dma_stop() 
{
    io_out8(DMA_SGL_MSK_SEC, 0x06);
}

void init_dma() 
{
    // DMAC reset
    io_out8(DMA_MSR_CLR_PRI, 0x00);
    io_out8(DMA_MSR_CLR_SEC, 0x00);

    io_out8(DMA_CMD_PRI, 0x00);
    io_out8(DMA_CMD_SEC, 0x00);

    // DMAC mode register setting
    io_out8(DMA_MOD_PRI, 0xc0);
    io_out8(DMA_MOD_SEC, 0x46);

    io_out8(DMA_SGL_MSK_PRI, 0x00);
}

static void init_dma_r() 
{
    fdc_dma_stop();

    io_out8(DMA_MSR_CLR_SEC, 0x00);
    io_out8(DMA_CLR_FLP_SEC, 0);

    io_out8(DMA_MOD_SEC, 0x46);
    io_cli();
    io_out8(DMA_ADD_SEC, dma_trans.addr >> 0);
    io_out8(DMA_ADD_SEC, dma_trans.addr >> 8);
    io_out8(DMA_TOP, dma_trans.addr >> 16);
    io_out8(DMA_CNT_SEC, dma_trans.count >> 0);
    io_out8(DMA_CNT_SEC, dma_trans.count >> 8);
    io_sti();
    fdc_dma_start();
}

static void init_dma_w() 
{
    fdc_dma_stop();

    io_out8(DMA_MSR_CLR_SEC, 0x00);
    io_out8(DMA_CLR_FLP_SEC, 0);

    io_out8(DMA_MOD_SEC, 0x4a);  // memory >> I/O
    io_cli();
    io_out8(DMA_ADD_SEC, dma_trans.addr >> 0);
    io_out8(DMA_ADD_SEC, dma_trans.addr >> 8);
    io_out8(DMA_TOP, dma_trans.addr >> 16);
    io_out8(DMA_CNT_SEC, dma_trans.count >> 0);
    io_out8(DMA_CNT_SEC, dma_trans.count >> 8);
    io_sti();
    fdc_dma_start();
}

static int fdc_wait_msrStatus(unsigned char mask, unsigned char expected) 
{
    int count;
    for (count = 0; count < FDC_RESULT_MAXCOUNT; ++count) {
        unsigned char status = io_in8(FDC_MSR);
        if ((status & mask) == expected)
            return status;
    }
    return -1;
}

static int fdc_cmd(const unsigned char* cmd, const int length) 
{
    //sysPrints("[FDC] cmd busy check.\n");
    if (!fdc_wait_msrStatus(MSR_BUSY, MSR_READY)) {
        sysPrints("[FDC] CMD가 바쁩니다.\n");
        return 0;
    }
    //sysPrints("[FDC] cmd busy check [OK]\n");

    //sysPrints("[FDC] cmd out and msr check.\n");
    int i;
    for (i = 0; i < length; ++i) {
        if (!fdc_wait_msrStatus(MSR_RQM | MSR_DIO, MSR_RQM)) {
            sysPrints("[FDC] msr RQM|DIO 오류\n");
            return 0;
        }
        io_out8(FDC_DAT, cmd[i]);
    }
    //sysPrints("[FDC] cmd out and msr check [OK]\n");

    return 1;
}

static int fdc_read_results() 
{
    fdc_results.status_count = 0;

    if (fdc_wait_msrStatus(MSR_RQM | MSR_DIO, MSR_RQM | MSR_DIO) < 0) {
        sysPrints("fdc 결과 단계 오류 1\n");
        return 0;
    }

    unsigned char* msr = &fdc_results.status[0];
    int i;
    for (i = 0; ; ++i) {
        *msr++ = io_in8(FDC_DAT);
        ++fdc_results.status_count;

        int status = fdc_wait_msrStatus(MSR_RQM, MSR_RQM);
        if (status < 0) {
            sysPrints("fdc 결과 단계 오류 2\n");
            return 0;
        }
        if (!(status & MSR_DIO))
        return 1;
    }
}

static int fdc_sense_interrupt() 
{
    static const unsigned char cmd[] = { CMD_SENSE_INT_STS };

    fdc_clear_interrupt();
    if (fdc_cmd(cmd, sizeof(cmd)) != 1) {
        sysPrints("[FDC] sense 인터럽트 상태 명령 오류\n");
        return 0;
    }
    fdc_read_results();
    return 1;
}

static void fdc_motor_on() 
{
    io_out8(FDC_DOR, 0x1c);
}

static void fdc_motor_off() 
{
    io_out8(FDC_DOR, 0x0c);
}

static int fdc_recalibrate() 
{
    static const unsigned char cmd[] = { CMD_RECALIBRATE, CMD_SUB };

    fdc_clear_interrupt();

    //sysPrints("[FDC] Recalibrate Cmd.\n");
    if (!fdc_cmd(cmd, sizeof(cmd))) {
        sysPrints("[FDC] Recalibrate Cmd 오류\n");
        return 0;
    }
    //sysPrints("[FDC] Recalibrate Cmd [OK]\n");

    if (!fdc_wait_interrupt())
        sysPrints("[FDC] 대기 인터럽트 오류\n");

    /* get result */
    fdc_sense_interrupt();

    if (!fdc_wait_msrStatus(MSR_BUSY, MSR_READY)) {
        sysPrints("[FDC] Recalibrate  대기 실패\n");
        return 0;
    }
    return 1;
}

static void fdc_specify() 
{
    fdc_motor_on();

    static const unsigned char specify_cmd[] = {
        CMD_SPECIFY,
        0xc1,  
        0x10   
    };
    fdc_clear_interrupt();
    fdc_cmd(specify_cmd, sizeof(specify_cmd));

    fdc_motor_off();
}

void init_fdc() 
{
    init_dma();
    dma_trans.addr = (unsigned int)&dma_databuf[0];
    dma_trans.count = 512;

    io_out8(FDC_DOR, 0x0);
    io_out8(FDC_CCR, 0x0);
    io_out8(FDC_DOR, 0xc);

    fdc_specify();

    io_out8(0x21, io_in8(0x21) & 0xbf);
}

static int fdc_seek(unsigned char cyl) 
{
    int drive = 0, head = 0;
    unsigned char cmd[] = {
        CMD_SEEK,       // 0x0f
        (head << 2) | drive,
        cyl
    };

    fdc_clear_interrupt();

    //sysPrints("[FDC] seek cmd check.\n");
    if (!fdc_cmd(cmd, sizeof(cmd))) {
        sysPrints("[FDC] seek cmd 오류\n");
        return 0;
    }
    //sysPrints("[FDC] seek cmd check [OK]\n");

    if (!fdc_wait_interrupt()) {
        sysPrints("[FDC][SEEK] 대기 인터럽트 오류\n");
        return 0;
    }

    /* get result */
    if (!fdc_sense_interrupt()) {
        sysPrints("[FDC][SEEK] SIS 오류\n");
        return 0;
    }

    return 1;
}

void* fdc_read(int head, int track, int sector) 
{
    init_dma_r();

    fdc_motor_on();

    if (!fdc_recalibrate()) {
        sysPrints("[FDC][READ] 재보정 오류\n");
        return 0;
    }

    if (!fdc_seek(track)) {
        sysPrints("[FDC][READ] 탐색 오류\n");
        return 0;
    }

    unsigned char cmd[] = {
        CMD_READ,
        head << 2,   // head
        track,       // track
        head,        // head
        sector,      // sector
        0x2,         // sector length (0x2 = 512byte)
        0x12,        // end of track (EOT)
        0x1b,        // dummy GSR
        0            // dummy STP
    };

    fdc_cmd(cmd, sizeof(cmd));
    fdc_dma_stop();
    fdc_read_results();

    fdc_motor_off();

    // write the binary which we get from DMA
    sysPrints("READ DATA:\n");
    int i, j;
    for (j = 0; j < 1; ++j) {
        for (i = 0; i < 16; ++i) {
            sysPrintBin(dma_databuf[j * 16 + i]);
            sysPrintc(' ');
        }
        sysPrintc('\n');
    }

    return 0;
}

int fdc_write(void* buf, int head, int cyl, int sector) 
{
    init_dma_w();
    fdc_motor_on();

    // sysPrints("FDC_WRITE\n");
    if (!fdc_recalibrate()) {
        sysPrints("[FDC][WRITE] recalibrate 오류\n");
        return 0;
    }

    // sysPrints("SEEK\n");
    if (!fdc_seek(cyl)) {
        sysPrints("[FDC][WRITE] 탐색 오류\n");
        return 0;
    }

    memcpy(dma_databuf, buf, 512);

    unsigned char cmd[] = {
        CMD_WRITE,
        head << 2,      // head
        cyl,            // cylinder
        head,           // head
        sector,         // sector
        0x2,            // sector length (0x2 = 512byte)
        0x12,           // end of track (EOT)
        0x1b,           // dummy GSR
        0               // dummy STP
    };

    fdc_clear_interrupt();

    // sysPrints("WRITE\n");
    if (!fdc_cmd(cmd, sizeof(cmd))) {
        sysPrints("[FDC][WRITE] cmd 오류\n");
        return 0;
    }

    if (!fdc_wait_interrupt()) {
        sysPrints("[FDC][WRITE] 대기 인터럽트 오류\n");
        return 0;
    }

    if (!fdc_read_results()) {
        sysPrints("[FDC][WRITE] 결과 읽기 오류\n");
        return 0;
    }
    // sysPrints("WRITE OK\n");

    fdc_motor_off();
    return 1;
}

static int writeSector(int sector) 
{
    const int SECTOR_COUNT = 18, HEAD_COUNT = 2;
    int cyl = sector / SECTOR_COUNT / HEAD_COUNT;
    int head = (sector / SECTOR_COUNT) & 1;
    int sec = (sector % SECTOR_COUNT) + 1;

    unsigned char* buf = (unsigned char*)(ADR_DISKIMG + sector * CLUSTER_SIZE);
    return fdc_write(buf, head, cyl, sec);
}

short get_next_cluster(short cluster) 
{
    const unsigned char* p = DISK_FAT + (cluster >> 1) * 3;
    if ((cluster & 1) == 0) {
        return (p[0] | p[1] << 8) & 0xfff;
    } else {
        return (p[1] >> 4 | p[2] << 4) & 0xfff;
    }
}

static void set_next_cluster(short cluster, short next) 
{
    unsigned char* p = DISK_FAT + (cluster >> 1) * 3;
    if ((cluster & 1) == 0) {
        p[0] = next;
        p[1] = (p[1] & 0xf0) | ((next >> 8) & 0x0f);
    } else {
        p[1] = (p[1] & 0x0f) | ((next << 4) & 0xf0);
        p[2] = next >> 4;
    }
}

// Returns updated clusters (represented in 9 bits, 9 comes from (0x1200 - 0x200) / 512)
static int deleteFatClusters(short startCluster) 
{
    int updatedClusterBits = 0;
    short cluster;
    for (cluster = startCluster; cluster < 0xff0; ) {
        short next = get_next_cluster(cluster);
        set_next_cluster(cluster, 0x000);  // Free
        updatedClusterBits |= 1 << (cluster / CLUSTER_SIZE);
        cluster = next;
    }
    return updatedClusterBits;
}

static int writeDirTable(FDINFO* finfo) 
{
  int filePos = finfo - FINFO_TOP;  // File position in the directory table.
  int offset = filePos * sizeof(FDINFO) / CLUSTER_SIZE;
  return writeSector(offset + CLUSTNO_ROOTDIR);
}

static int writeFat(int fatBits) 
{
  const int FAT_CLUSTERS = 9;
  // Write FAT.
  int i;
  for (i = 0; i < FAT_CLUSTERS; ++i) {
    if (!(fatBits & (1 << i)))
      continue;
    if (!writeSector(i + CLUSTNO_FAT1))
      return 0;
  }
  return 1;
}

// Write back file entry from memory to floppy disk.
static int writeBack(FDHANDLE* fh, int fatBits) 
{
  // Write file body.
  short cluster = fh->finfo->clustno;

  if (cluster >= 2) {
    for (; ;) {
        if (!writeSector(cluster + CLUSTNO_ENTITY)) return 0;
        cluster = get_next_cluster(cluster);
        if (cluster >= 0xff0) break;
    }
  }

  // Write directory entry.
  writeDirTable(fh->finfo);

  // Detect which area in the FAT is updated.
  for (cluster = fh->finfo->clustno; ; cluster = get_next_cluster(cluster)) {
    int p = cluster / 2 * 3 + (cluster & 1);
    fatBits |= (1 << (p / CLUSTER_SIZE));
    fatBits |= (1 << ((p + 1) / CLUSTER_SIZE));
    if (cluster >= 0xff0)
        break;
  }

  if (!writeFat(fatBits))
    return 0;

  sysPrints("디스크 쓰기 성공\n");
  return 1;
}

void fd_close(FDHANDLE* fh) 
{
    if (fh->modified) {
        int fatBits = 0;
        fh->modified = 0;
        if (fh->cluster > 0) {
            fatBits = deleteFatClusters(fh->cluster);
            set_next_cluster(fh->cluster, 0xfff);  // End mark.
        }
        fh->finfo->size = fh->pos;

        // Update timestamp.
        // unsigned char t[5];
        // int year = read_rtc(t);
        // fh->finfo->date = ((year - 1980) << 9) | ((t[0] - 1) << 5) | (t[1] - 1);
        // fh->finfo->time = (t[2] << 11) | (t[3] << 5) | (t[4] / 2);

        // Write back to floppy disk.
        {
        init_dma_w();
        fdc_motor_on();

        sysPrints("FDC_WRITE\n");
        if (!fdc_recalibrate()) {
            sysPrints("[FDC][WRITE] 재보정 오류\n");
        } else {
            if (!writeBack(fh, fatBits)) {
                sysPrints("WRITE_BACK 실패\n");
            }
        }

        fdc_motor_off();
        }
        sysPrints("[RAM-DISK] 메모리에 파일이 저장되었습니다.\n");
    }
    fh->finfo = 0;
}

static void make_file_name83(char s[12], const char* name) 
{
    memset(s, ' ', 11);
    int j = 0;
    int i;
    for (i = 0; j < 11 && name[i] != '\0'; ++i) {
        char c = name[i];
        if ('a' <= c && c <= 'z') {
            c -= 0x20; 
        }
        if (c == '.') {
            j = 8;
        } else {
            s[j++] = c;
        }
    }
}

int fd_writeopen(FDHANDLE* fh, const char* filename) 
{
    fh->finfo = 0;
    fh->pos = 0;
    fh->cluster = 0;
    fh->modified = 0;

    char s[12];
    make_file_name83(s, filename);
    FDINFO* finfo;
    int i;
    for (i = 0; i < FINFO_MAX; ++i) {
        finfo = &FINFO_TOP[i];
        if (finfo->name[0] == 0x00 || finfo->name[0] == 0xe5) {  // End of table, or deleted entry.
            memset(finfo, 0x00, sizeof(FDINFO));
            memcpy(finfo->name, s, 11);
            finfo->type = 0x20;  // Normal file.
            goto found;
        }
        if ((finfo->type & 0x18) == 0) {
            if (strncmp((char*)finfo->name, s, 11) == 0)
                goto found;
        }
    }
    return 0;

found:
    fh->finfo = finfo;
    return 1;
}

static FDINFO* fd_search(const char* filename) 
{
    char s[12];
    make_file_name83(s, filename);
    int i;
    for (i = 0; i < FINFO_MAX; ++i) {
        FDINFO* finfo = &FINFO_TOP[i];
        if (finfo->name[0] == 0x00)  // End of table.
            return 0;
        if ((finfo->type & 0x18) == 0) {
            if (strncmp((char*)finfo->name, s, 11) == 0)
                return finfo;
        }
    }
    return 0;
}

int fd_delete(const char* filename) 
{
    FDINFO* finfo = fd_search(filename);
    if (finfo == 0)
        return 0;

    finfo->name[0] = 0xe5;  // Delete mark.
    int deletedClusterBits = deleteFatClusters(finfo->clustno);

    // Write to FD.
    writeDirTable(finfo);
    writeFat(deletedClusterBits);

    return 1;
}

int fd_open(FDHANDLE* fh, const char* name) 
{
    fh->pos = 0;
    fh->cluster = 0;
    fh->modified = 0;

    fh->finfo = fd_search(name);
    if (fh->finfo == 0)
        return 0;
    fh->cluster = fh->finfo->clustno;
    return 1;
}

static unsigned char* clusterData(int cluster) 
{
    return DISK_CLUSTER_DATA + cluster * CLUSTER_SIZE;
}

int fd_read(FDHANDLE* fh, void* dst, int requestSize) 
{
    int readSize = 0;
    unsigned char* p = (unsigned char *)dst;

    // debug---------------------------
    // char debug[128];
    // unsigned char* start_src = clusterData(fh->cluster) + (fh->pos % CLUSTER_SIZE);
    
    // sprintf(debug, "[KERNEL READ] Clust:%d, Pos:%d, Size:%d, FirstByte:%02X, DstAddr:%08X\n", 
    //         fh->cluster, fh->pos, fh->finfo->size, *start_src, (int)dst);
    // sysPrints(debug);
    // ---------------------------------

    while (requestSize > 0) {
        if (fh->pos >= (int)fh->finfo->size) break;

        int offset_in_cluster = fh->pos % CLUSTER_SIZE;

        int bytes_left_in_cluster = CLUSTER_SIZE - offset_in_cluster;
        int bytes_left_in_file = fh->finfo->size - fh->pos;

        int chunk = requestSize;
        if (chunk > bytes_left_in_cluster) chunk = bytes_left_in_cluster;
        if (chunk > bytes_left_in_file) chunk = bytes_left_in_file;

        unsigned char *src = clusterData(fh->cluster) + offset_in_cluster;
        memcpy(p, src, chunk);

        fh->pos += chunk;
        p += chunk;
        readSize += chunk;
        requestSize -= chunk;

        if (offset_in_cluster + chunk == CLUSTER_SIZE && fh->pos < (int)fh->finfo->size) {
            fh->cluster = get_next_cluster(fh->cluster);
            if (fh->cluster >= 0xff0) break;
        }
    }

    return readSize;
}

short allocate_cluster(void) 
{
    int i;
    for (i = 2; i < MAX_CLUSTER; ++i) {
        if (get_next_cluster(i) == 0) {
            set_next_cluster(i, 0xfff);  // Write end mark.
            return i;
        }
    }
  return -1;
}

int fd_write(FDHANDLE* fh, const void* srcData, int requestSize) {
    if (requestSize <= 0) return 0;

    fh->modified = 1;
    int writeSize = 0;
    unsigned char* src = (unsigned char*)srcData;

    if (fh->cluster == 0) {
        if (fh->finfo->clustno > 0) {
            fh->cluster = fh->finfo->clustno;
        } else {
            fh->cluster = allocate_cluster();
            fh->finfo->clustno = fh->cluster;
        }
    }

    // char debug[64];
    // sprintf(debug, "Source: %x, Cluster: %x, Pos: %d, RS: %d\n", (int)src, fh->cluster, fh->pos, requestSize);
    // sysPrints(debug);

    while (requestSize > 0) {
        int offset_in_cluster = fh->pos % CLUSTER_SIZE;
        int bytes_available = CLUSTER_SIZE - offset_in_cluster;

        int chunk = (requestSize < bytes_available) ? requestSize : bytes_available;

        unsigned char* dst = clusterData(fh->cluster) + offset_in_cluster;
        // char debug[64];
        // dst(목적지), src(소스), chunk(크기), cluster(현재 클러스터) 모두 출력
        // sprintf(debug, "WR: dst=%08X, src=%08X, len=%d, clus=%d\n", 
        //         (int)dst, (int)src, chunk, fh->cluster);
        // sysPrints(debug);

        // [안전장치] 만약 dst가 GDT 영역(0x270000) 근처라면 강제 정지!
        if ((int)dst >= 0x00260000 && (int)dst <= 0x00280000) {
            sysPrints("[치명적 오류] GDT 영역 침범! 파일쓰기를 중단합니다.\n");
            for(;;) io_hlt(); // 여기서 멈춰서 로그 확인
        }
        memcpy(dst, src, chunk);

        fh->pos += chunk;
        src += chunk;
        writeSize += chunk;
        requestSize -= chunk;

        if (fh->pos > (int)fh->finfo->size) {
            fh->finfo->size = fh->pos;
        }

        if (requestSize > 0) {
            short nextCluster = get_next_cluster(fh->cluster);
            if (nextCluster >= 0xff0) {  // No next cluster, allocate new one.
                nextCluster = allocate_cluster();
                set_next_cluster(fh->cluster, nextCluster);
            }
            fh->cluster = nextCluster;
        }
    }
    return writeSize;
}

static int calc_cluster(FDHANDLE* fh, int newpos) 
{
    int clusterCount = newpos / CLUSTER_SIZE - fh->pos / CLUSTER_SIZE;
    int cluster = fh->cluster;
    if (newpos < fh->pos) {  // If the new position is backward,
        // Then search target cluster from the top.
        cluster = fh->finfo->clustno;
        clusterCount = newpos / CLUSTER_SIZE;
    }

    int i;
    for (i = 0; i < clusterCount; ++i)
        cluster = get_next_cluster(cluster);
    return cluster;
}

void fd_seek(FDHANDLE* fh, int offset, int origin) {
    int newpos = fh->pos;
    switch (origin) {
        case 0:  newpos = offset; break;
        case 1:  newpos += offset; break;
        case 2:  newpos = fh->finfo->size + offset; break;
    }

    if (newpos < 0)
        newpos = 0;
    else if (newpos > (int)fh->finfo->size)
        newpos = fh->finfo->size;
    fh->cluster = calc_cluster(fh, newpos);
    fh->pos = newpos;
}
