// dsctbl.c
// 8bytes structure which compose GDT

#ifndef _DSCTBL_H_
#define _DSCTBL_H_

struct SEGMENT_DESCRIPTOR {
    short limit_low, base_low;
    char base_mid, access_right;
    char limit_high, base_high;

    // base = base_high << 24 + base_mid << 16 + base_low = 32bit
};

// 8bytes structure which compose IDT
struct GATE_DESCRIPTOR {
    short offset_low, selector;
    char dw_count, access_right;
    short offset_high;
};

// GDT/IDT functions implented in dsctbl.c
void init_gdtidt(void);
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar);
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar);

#endif  // _DSCTBL_H_
