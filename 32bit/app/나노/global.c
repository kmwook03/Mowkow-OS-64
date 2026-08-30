/**************************************************************************
 *   global.c                                                             *
 *                                                                        *
 *   Copyright (C) 1999 Chris Allegretta                                  *
 *   This program is free software; you can redistribute it and/or modify *
 *   it under the terms of the GNU General Public License as published by *
 *   the Free Software Foundation; either version 1, or (at your option)  *
 *   any later version.                                                   *
 *                                                                        *
 *   This program is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 *   GNU General Public License for more details.                         *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program; if not, write to the Free Software          *
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.            *
 *                                                                        *
 **************************************************************************/

#include "tip.h"

int cur_x = 0, cur_y = 0;
int center_x = 0, center_y = 0;		/* Center of screen */
void *edit;				/* The file portion of the editor  */
void *topwin;				/* Top line of screen */
void *bottomwin;			/* Bottom buffer */
int file;				/* Actual file pointer */
char filename[132] = "";		/* Name of the file */
int modified = 0;			/* Has file been modified? */
int editwinrows = 0;			/* How many rows long is the edit
					   window? */
int editwineob = 0;			/* Last Line in edit buffer
					   (0 - editwineob) */
filestruct *current;			/* Current buffer pointer */
int current_x = 0, current_y = 0;	/* Current position of X and Y in
					   the editor - relative to edit
					   window (0,0) */
int posible_max = 0;			/* The X value we'd like to be able to get to on a line */
filestruct *fileage = 0;		/* Our file buffer */
filestruct *edittop = 0;		/* Pointer to the top of the edit
					   buffer with respect to the
					   file struct */

filestruct *editbot = 0;		/* Same for the bottom */
filestruct *filebot = 0;		/* Last node in the file struct */
filestruct *cutbuffer = 0;		/* A place to store cut text */
filestruct *cutbottom = 0;		/* Pointer to end of cutbuffer */
int keep_cutbuffer = 0;			/* Clear out the cutbuffer? */

char answer[132] = "";			/* Answer str to many questions */
char last_search[132] = "";		/* Last string we searched for */
char last_replace[132] = "";		/* Last replacement string */
int totlines = 0;			/* Total number of lines in the
					   file */
int suspend = 0;			/* Can TIP be suspended */
int case_sensitive = 0;			/* Are we doing case sensitive
					   searches */
int placewewant = 0;			/* The collum we'd like the cursor
					    to jump to when we go to the
					    next or previous line */

int statblank = 0;			/* Number of keystrokes left after
					   we call statubar() before we
					   actually blank the statusbar */

shortcut main_list[MAIN_LIST_LEN] = 
{
{TIP_GOTO_KEY, "줄로가기"},			// "Goto Line"
{TIP_EXIT_KEY, "나가기"},					// "Exit"
{TIP_WRITEOUT_KEY, "파일쓰기"},		// "Write File"
{TIP_INSERTFILE_KEY, "파일읽기"},		// "Read File"
{TIP_REPLACE_KEY, "대체"},			// "Replace"
{TIP_WHEREIS_KEY, "찾기"},			// "Where Is"
{TIP_PREVPAGE_KEY, "이전 쪽"},		// "Prev Page"
{TIP_NEXTPAGE_KEY, "다음 쪽"},		// "Next Page"
{TIP_CUT_KEY, "잘라내기"},				// "Cut Text"
{TIP_UNCUT_KEY, "붙여넣기"},			// "Uncut Txt"
{TIP_CURSORPOS_KEY, "커서위치"},			// "Cur Pos"
{TIP_SPELLING_KEY, "맞춤법"}			// "To Spell"
};

shortcut whereis_list[WHEREIS_LIST_LEN] = 
{
{TIP_CASE_KEY, "대소구분"},			// "Case Sens"
{TIP_CANCEL_KEY, "취소"},				// "Cancel"
{TIP_FIRSTLINE_KEY, "첫 줄"},		// "First Line"
{TIP_LASTLINE_KEY, "끝 줄"},		// "Last Line"
};

shortcut replace_list[REPLACE_LIST_LEN] = 
{
{TIP_CASE_KEY, "대소구분"},			// "Case Sens"
{TIP_CANCEL_KEY, "취소"},				// "Cancel"
{TIP_FIRSTLINE_KEY, "첫 줄"},		// "First Line"
{TIP_LASTLINE_KEY, "끝 줄"},		// "Last Line"
};

shortcut goto_list[GOTO_LIST_LEN] = 
{
{TIP_FIRSTLINE_KEY, "첫 줄"},		// "First Line"
{TIP_LASTLINE_KEY, "끝 줄"},		// "Last Line"
{TIP_CANCEL_KEY, "취소"},				// "Cancel"
};

shortcut writefile_list[WRITEFILE_LIST_LEN] = 
{
{TIP_CANCEL_KEY, "취소"},
};
