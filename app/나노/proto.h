/**************************************************************************
 *   proto.h                                                              *
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

/* Externs */

#include "tip.h"

extern int center_x, center_y, file, modified, editwinrows, editwineob;
extern int current_x, current_y, posible_max, keep_cutbuffer, totlines;
extern int suspend, case_sensitive, placewewant, statblank;

extern void *edit, *topwin, *bottomwin;
extern char filename[132], answer[132], last_search[132], last_replace[132];
extern struct stat fileinfo;
extern filestruct *current, *fileage, *edittop, *editbot, *filebot; 
extern filestruct *cutbuffer, *cutbottom;

extern shortcut *shortcut_list;
extern shortcut main_list[MAIN_LIST_LEN], whereis_list[WHEREIS_LIST_LEN];
extern shortcut replace_list[REPLACE_LIST_LEN], goto_list[GOTO_LIST_LEN];
extern shortcut writefile_list[WRITEFILE_LIST_LEN];

void lowercase(char *src);
char *strcasestr(char *haystack, char *needle);
char *strstrwrapper(char *haystack, char *needle);
void strip_newline(char *str);
int search_init(void);
void do_search(void);
void blank_bottombars(void);
