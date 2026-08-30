#ifndef _TIP_H_
#define _TIP_H_

#define COLORS          8
#define COLOR_PAIRS     64
#define COLS            80
#define ESCDELAY        0
#define LINES           24
#define TABSIZE         4

#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

#define MAIN_LIST_LEN 12
#define WHEREIS_LIST_LEN 4
#define REPLACE_LIST_LEN 4
#define GOTO_LIST_LEN 3
#define WRITEFILE_LIST_LEN 1

#define TIP_CONTROL_A 1
#define TIP_CONTROL_B 2
#define TIP_CONTROL_C 3
#define TIP_CONTROL_D 4
#define TIP_CONTROL_E 5
#define TIP_CONTROL_F 6
#define TIP_CONTROL_G 7
#define TIP_CONTROL_H 8
#define TIP_CONTROL_I 9
#define TIP_CONTROL_J 10
#define TIP_CONTROL_K 11
#define TIP_CONTROL_L 12
#define TIP_CONTROL_M 13
#define TIP_CONTROL_N 14
#define TIP_CONTROL_O 15
#define TIP_CONTROL_P 16
#define TIP_CONTROL_Q 17
#define TIP_CONTROL_R 18
#define TIP_CONTROL_S 19
#define TIP_CONTROL_T 20
#define TIP_CONTROL_U 21
#define TIP_CONTROL_V 22
#define TIP_CONTROL_W 23
#define TIP_CONTROL_X 24
#define TIP_CONTROL_Y 25
#define TIP_CONTROL_Z 26

#define TIP_INSERTFILE_KEY	TIP_CONTROL_R
#define TIP_EXIT_KEY 		TIP_CONTROL_X
#define TIP_WRITEOUT_KEY	TIP_CONTROL_O
#define TIP_GOTO_KEY		TIP_CONTROL_G
#define TIP_WHEREIS_KEY		TIP_CONTROL_W
#define TIP_REPLACE_KEY		TIP_CONTROL_P
#define TIP_PREVPAGE_KEY	TIP_CONTROL_Y
#define TIP_NEXTPAGE_KEY	TIP_CONTROL_V
#define TIP_CUT_KEY		    TIP_CONTROL_K
#define TIP_UNCUT_KEY		TIP_CONTROL_U
#define TIP_CURSORPOS_KEY	TIP_CONTROL_C
#define TIP_SPELLING_KEY	TIP_CONTROL_T
#define TIP_FIRSTLINE_KEY	TIP_PREVPAGE_KEY
#define TIP_LASTLINE_KEY	TIP_NEXTPAGE_KEY
#define TIP_CANCEL_KEY		TIP_CONTROL_C
#define TIP_CASE_KEY		TIP_CONTROL_A
#define TIP_REFRESH_KEY		TIP_CONTROL_L
#define TIP_SPELL_KEY		TIP_CONTROL_T

#define TIP_LEFT_KEY      4
#define TIP_RIGHT_KEY     6
#define TIP_UP_KEY        8
#define TIP_DOWN_KEY      2

typedef struct filestruct {
    char *data;
    struct filestruct *next;	/* Next node */
    struct filestruct *prev;	/* Previous node */
    long bytes;			/* # of Bytes before this line */
    int wrapline;		/* Is this line newly created by a wrap */
} filestruct;

typedef struct shortcut {
   int val;              /* Actual sequence that generates the keystroke */
   char desc[50];       /* Description, e.g. "Page Up" */
} shortcut;

void insert_char_at_cursor(int key);
void delete_char_at_cursor();
void update_line(filestruct *fileptr);

#endif  /* _TIP_H_ */
