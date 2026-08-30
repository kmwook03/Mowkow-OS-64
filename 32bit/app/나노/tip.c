/**************************************************************************
 *   tip.c                                                                *
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

#include "../include/apilib.h"
#include "../include/apihan.h"
#include "../include/mylib.h"
#include "tip.h"
#include "compat.h"
#include "proto.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

extern char answer[132];
extern int cur_x;

char *winbuf_global;
int win_width_global;

struct HANGUL_STATE h_state;
int lang_mode = 0;

char *skipspace(char *p)
{
   for (; *p==' '; p++) { }
   return p;
}

void finish(int win)
{
   blank_bottombars();
   wrefresh(bottomwin);
   endwin(win);
   api_end();
}

void global_init(void)
{
   center_x = COLS / 2;
   center_y = LINES / 2;
   current_x = 0;
   current_y = 0;
   editwinrows = LINES - 5;
   editwineob = editwinrows - 1;
   fileage = 0;
   cutbuffer = 0;
}

filestruct *copy_node(filestruct *src)
{
   filestruct *dst;

   dst = nano_malloc(sizeof(filestruct));
   dst->data = nano_malloc(my_strlen(src->data)+2);

   dst->next = src->next;
   dst->prev = src->prev;
   my_strncpy(dst->data, src->data, my_strlen(src->data));
   return dst;
}

/* Unlink a node from the rest of the struct */
void unlink_node(filestruct *fileptr)
{
   if (fileptr->prev != 0) {
      fileptr->prev->next = fileptr->next;
   }

   if (fileptr->next != 0) {
      fileptr->next->prev = fileptr->prev;
   }
}

void delete_node(filestruct *fileptr)
{
   if (fileptr->data != 0) nano_free(fileptr->data);
   nano_free(fileptr);
}

filestruct *copy_filestruct(filestruct *src)
{
   filestruct *dst, *tmp, *head, *prev;

   head = copy_node(src);
   dst = head;			/* Else we barf on copying just one line :-) */
   tmp = src->next;
   prev = head;

   while (tmp != 0) {
      dst = copy_node(tmp);
      dst->prev = prev;
      prev->next = dst;

      prev = dst;
      tmp = tmp->next;
   }

   dst->next = 0;
   return head;
}

int free_node (filestruct *src)
{
   if (src == 0) return 0;

   if (src->next != 0) nano_free(src->data);

   nano_free(src);
   return 1;
}

int free_filestruct(filestruct *src)
{
   filestruct *fileptr = src;

   if (src == 0) return 0;

   while (fileptr->next != 0) {
      fileptr = fileptr->next;
      free_node(fileptr->prev);
   }

   free_node(fileptr);

   return 1;
}

void reset_cursor(void)
{
   filestruct *ptr = edittop;

   current_y = 0;

   while (ptr != current && ptr != editbot && ptr->next != 0) {
      ptr = ptr->next;
      current_y++;
   }
   wmove(edit, current_y, current_x);
}

void blank_bottombars(void)
{
   int i, j;

   for (j = 1; j <= 2; j++) {
      for (i = 0; i <= COLS - 1; i++) {
         mvwaddch(bottomwin, j, i, ' ');
      }
   }

  reset_cursor();
}

void blank_statusbar(void)
{
   int i;

   for (i = 0; i <= COLS - 1; i++) {
      mvwaddch(bottomwin, 0, i, ' ');
   }

  reset_cursor();
}

void blank_statusbar_refresh(void)
{
   blank_statusbar();
   wrefresh(bottomwin);
}

void check_statblank(void)
{
   if (statblank > 1) statblank--;
   else if (statblank == 1) {
      statblank--;
      blank_statusbar_refresh();
   }
}

int tipgetstr(char *buf, char *def, shortcut s[], int slen, int start_x)
{
   int x = start_x;
   int i = 0;
   int key;

   struct HANGUL_STATE h_state;
   HAN_CONTEXT ctx;

   ctx.target = TARGET_STATUSBAR;
   ctx.win = bottomwin;
   ctx.x_ptr = &x;

   apihan_init(&h_state, nano_han_writer, &ctx);

   mvwaddstr(bottomwin, 0, 0, buf);

   if (def != 0 && def[0] != '\0') {
      int len = (my_strlen(def) > 130) ? 130 : my_strlen(def);
      strncpy(answer, def, len);
      answer[len] = 0;

      i = my_strlen(answer);
      nano_han_writer(answer, &ctx);
   } else {
      answer[0] = 0;
      i = 0;
   }
   wrefresh(bottomwin);

   int lang_mode = 1; // 0: English, 1: Hangul

   for (;;) {
      wmove(bottomwin, 0, x);
      wrefresh(bottomwin);

      key = wgetch(edit);

      if (key == 4) {
         if (i > 0) {
            int char_len = 1;
            int visual_len = 1;

            if (i >- 3 && (unsigned char)answer[i-1] >= 0x80) {
               char_len = 3;
               visual_len = 2;
            }
            i -= char_len;
            x -= visual_len;
         }
         continue;
      }
      if (key == 6) {
         if (answer[i] != 0) {
            int char_len = 1;
            int visual_len = 1;

            // 현재 위치의 글자가 한글 시작 바이트(0xE0 이상)인지 확인
            if ((unsigned char)answer[i] >= 0xE0) {
               char_len = 3;
               visual_len = 2;
            }

            i += char_len;
            x += visual_len;
         }
         continue;
      }
      if (key == 13) {
         if (lang_mode == 1) {
            apihan_run(&h_state, 0, answer, &i);
         }

         answer[i] = 0; // 문자열 끝(NULL) 처리
         return 0;      // 성공 리턴
      }

      if (key == 127) {
         if (lang_mode == 1) {
            apihan_backspace(&h_state, answer, &i);
         } else {
            if (i > 0) {
               int char_len = 1;
               int visual_len = 1;

               if ((unsigned char)answer[i-1] < 0x80) {
                  char_len = 1;
                  visual_len = 1;
               } else if (i >= 3) {
                  char_len = 3;
                  visual_len = 2;
               }

               i -= char_len;
               x -= visual_len;

               mvwaddstr(bottomwin, 0, x, (visual_len == 2) ? "  " : " ");
               wmove(bottomwin, 0, x);
            }
         }
         continue;
      }
      if (key == 27) { // ESC
         return -1;
      }

      if (key == 0xFF) {
         lang_mode ^= 1;
         apihan_run(&h_state, key, answer, &i);
         continue;
      }
      if (i >= 130) continue;

      if (lang_mode == 1) {
         apihan_run(&h_state, key, answer, &i);
      } else {
         if (key >= 32 && key <= 126) {
            answer[i] = key;
            i++;
            char temp[2];
            temp[0] = key;
            temp[1] = 0;
            nano_han_writer(temp, &ctx);
         }
      }
   }
}

void horizbar(void *win, int y)
{
   int i = 0;

   wattron(win, A_REVERSE);
   for (i = 0; i <= COLS - 1; i++) {
      mvwaddch(win, y, i, ' ');
   }
   wattroff(win, A_REVERSE);
}

void titlebar(void)
{
   wattron(topwin, A_REVERSE);

   int i;
   wmove(topwin, 0, 0);
   for (i=0; i<COLS; i++) {
      waddch(topwin, ' ');
   }

   mvwaddstr(topwin, 0, 4, "그누 나노"); // GNU nano
   mvwaddstr(topwin, 0, COLS/2, filename);

   if (modified) mvwaddstr(topwin, 0, COLS - 10, "수정됨"); // Modified

   wattroff(topwin, A_REVERSE);
   wrefresh(topwin);
   reset_cursor();
}

void onekey(char *keystroke, char *desc)
{
   char description[80];
   int i;

   description[0] = ' ';
   for (i=0; i<80; i++) {
      if (desc[i] == 0) break;
      description[i+1] = desc[i];
   }
   for(; i<11; i++) description[i+1] = ' ';
   description[i+1] = 0;

   wattron(bottomwin, A_REVERSE);
   waddstr(bottomwin, keystroke);
   wattroff(bottomwin, A_REVERSE);
   waddstr(bottomwin, description);
}

void clear_bottomwin(void)
{
   int i;

   for (i = 0; i <= COLS - 1; i++) {
      mvwaddch(bottomwin, 1, i, ' ');
      mvwaddch(bottomwin, 2, i, ' ');
   }
   wrefresh(bottomwin);
}

void bottombars(shortcut s[], int slen)
{
   int i;
   char keystr[10];

   clear_bottomwin();
   wmove(bottomwin, 1, 0);
   for (i = 0; i <= slen - 1; i += 2) {
      keystr[0] = '^';
      keystr[1] = s[i].val + 64;
      keystr[2] = 0;

      onekey(keystr, s[i].desc);
   }
   wmove(bottomwin, 2, 0);
   for (i = 1; i <= slen - 1; i += 2) {
      keystr[0] = '^';
      keystr[1] = s[i].val + 64;
      keystr[2] = 0;

      onekey(keystr, s[i].desc);
   }
   wrefresh(bottomwin);

}

void update_line(filestruct *fileptr)
{
   filestruct *filetmp;
   int line = 0;

  for (filetmp = edittop; filetmp != fileptr && filetmp != editbot; filetmp = filetmp->next) line++;

  mvwaddstr(edit, line, 0, filetmp->data);
}

void center_cursor(void)
{
   current_y = editwinrows / 2;
   wmove(edit, current_y, current_x);
   wrefresh(edit);
}

void edit_refresh(void)
{
   int lines = 0, j = 0;
   filestruct *temp;

   temp = edittop;
    
   while (lines <= editwinrows - 1 && temp != 0) {
      mvwaddstr(edit, lines, 0, temp->data);
   
      for (j = cur_x; j < COLS; j++) {
         mvwaddch(edit, lines, j, ' ');
      }
      
      temp = temp->next;
      lines++;
   } 

   for (; lines <= editwinrows - 1; lines++) {
      for(j = 0; j < COLS; j++) {
         mvwaddch(edit, lines, j, ' ');
      }
   }
   
   editbot = temp;
}

void edit_update(filestruct *fileptr)
{
   int lines = 0, i = 0, j = 0;
   filestruct *temp;

   temp = fileptr;
   while (i <= editwinrows / 2 && temp->prev != 0) {
      i++;
      temp = temp->prev;
   }
   edittop = temp;

   while (lines <= editwinrows - 1 && lines <= totlines && temp != 0 && temp != filebot) {
      mvwaddstr(edit, lines, 0, temp->data);
      temp = temp->next;
      lines++;
   } 

   if (temp == filebot) {
      mvwaddstr(edit, lines, 0, filebot->data);
      lines++;
      for (i = lines; i <= editwinrows - 1; i++) {
         for(j = 0; j <= COLS - 1; j++) {
            mvwaddch(edit, i, j, ' ');
         }
      }
   }
   editbot = temp;

}

void do_first_line(void)
{
   current = fileage;
   placewewant = 0;
   current_x = 0;
   edit_update(current);
}

void do_last_line(void)
{
   current = filebot;
   placewewant = 0;
   current_x = 0;
   edit_update(current);
}

int statusq(shortcut s[], int slen, char *def, char *msg, ...)
{
   va_list ap;
   char foo[133];
   int ret;

   bottombars(s, slen); 

   foo[0] = '\0';
   if (msg) strcpy(foo, msg);
   strcat(foo, ": ");

   wattron(bottomwin, A_REVERSE);

   ret = tipgetstr(foo, def, s, slen, (strlen(foo)));
   wattroff(bottomwin, A_REVERSE);

   switch (ret) {
      case TIP_FIRSTLINE_KEY:
         do_first_line();
         break;
      case TIP_LASTLINE_KEY:
         do_last_line();
         break;
   }

   /* Then blank the screen */
   blank_statusbar_refresh();


   return ret;
}

int do_yesno(int all, char *msg, ...)
{
   va_list ap;
   char foo[133];
   int kbinput, ok = -1;

   clear_bottomwin();
   wattron(bottomwin, A_REVERSE);
   blank_statusbar_refresh();
   wattroff(bottomwin, A_REVERSE);

   wmove(bottomwin, 1, 0);
   onekey(" Y", "예");          // " Y" "Yes"
   if (all) onekey(" A", "모두");       // " A" "All"
   wmove(bottomwin, 2, 0);
   onekey(" N", "아니오");           // " N" "No"
   onekey("^C", "취소");       // "^C" "Cancel"
   
   va_start(ap, msg);
   mini_vsnprintf(foo, 132, msg, ap);
   va_end(ap);
   wattron(bottomwin, A_REVERSE);
   mvwaddstr(bottomwin, 0, 0, foo);
   wattroff(bottomwin, A_REVERSE);
   wrefresh(bottomwin);

   reset_cursor();

   while (ok == -1) {
      kbinput = wgetch(edit);

      switch (kbinput) {
         case 'Y': case 'y':
            ok = 1;
            break;
         case 'N': case 'n':
            ok = 0;
            break;
         case 'A': case 'a':
            if (all)
               ok = 2;
            break;
         case TIP_CONTROL_C:
            ok = -2;
            break;
      }
   }
   /* Then blank the screen */
   blank_statusbar_refresh();

   if (ok == -2)
      return -1;
   else
      return ok;
}

void statusbar(char *msg, ...)
{
   va_list ap;
   char foo[133];
   int start_x = 0;

   va_start(ap, msg);
   mini_vsnprintf(foo, 132, msg, ap);
   va_end(ap);

   start_x = center_x - strlen(foo) / 2 - 1;
   blank_statusbar_refresh();

   wmove(bottomwin, 0, start_x);
   wattron(bottomwin, A_REVERSE);

   waddstr(bottomwin, "[ ");
   waddstr(bottomwin, foo);
   waddstr(bottomwin, " ]");
   wattroff(bottomwin, A_REVERSE);
   wrefresh(bottomwin);

   statblank = 25;
}

void total_refresh(int win)
{
   int i, j;

   redrawin(win);
   bottombars(main_list, MAIN_LIST_LEN);
   titlebar();
   for (i = 0 ; i <= LINES - 1; i++) {
      for (j = i; j != COLS; j++)
         mvwaddch(edit, i, j, ' ');
   }
   wrefresh(edit);

   edit_refresh();
   reset_cursor();
   wrefresh(edit);
   wrefresh(topwin);
   wrefresh(bottomwin);
}

void previous_line(void)
{
   if (current_y > 0)
      current_y--;
   else
      edit_refresh();

   reset_cursor();
}

void load_file(void)
{
   current = fileage;
   wmove(edit, current_y, current_x);
   edit_update(fileage);
   wrefresh(edit);
}

void open_file(char *f_name)
{
   long size, totsize = 0, linetemp = 0;
   int fh;
   char input[2]; /* buffer */
   char buf[2000] = ""; 
   filestruct *fileptr;

   titlebar();
   fileptr = fileage;

   fh = api_fopen(f_name, 0);

   if (fh == 0)	/* We have a new file */
   {
      statusbar("새 파일");                       // "New File"
      fileage = nano_malloc(sizeof(filestruct));
      fileage->data = nano_malloc(2);
      fileage->data[0] = '\n';
      fileage->data[1] = 0;
      fileage->prev = 0;
      fileage->next = 0;

      filebot = fileage;
      fileptr = fileage;
      current = fileage;
      totlines = 1;
   }
   else			/* File is A-OK */
   {
      statusbar("파일 읽는 중");                      // "Reading File"

      fileage = 0;
      filebot = 0;
      totlines = 0;

      /* Read the entire file into file struct */
      while (api_fread(input, 1, fh) != 0)
      {
         if (input[0] == '\n')
         {
            if (fileage == 0)
            {
               fileage = nano_malloc(sizeof(filestruct));
               fileage->data = nano_malloc(strlen(buf)+2);
               strcpy(fileage->data, buf);
               strcat(fileage->data, "\n");
               fileage->prev = 0;
               fileage->next = 0;
               filebot = fileage;
               fileptr = fileage;
            }
            else
            {
               filebot->next = nano_malloc(sizeof(filestruct));
               filebot->next->data = nano_malloc(strlen(buf)+2);
               strcpy(filebot->next->data, buf);
               strcat(filebot->next->data, "\n");
               filebot->next->prev = filebot;
               filebot->next->next = 0;
               filebot = filebot->next;
            }
            totlines++;
            buf[0] = 0;
         }
         else
         {
            int len = my_strlen(buf);
            if (len < 1998) {
               buf[len] = input[0];
               buf[len + 1] = 0;
            }
         }

         totsize++;
      }
      current = fileage;
      edittop = fileage;
      editbot = fileage;

      statusbar("%d 바이트 (%d 줄) 읽음", totsize, totlines);         // "%d bytes (%d lines) read"
      api_fclose(fh);
   }
}

void update_cursor(void)
{
   int i = 0;

   wmove(edit, current_y, current_x);

   current = edittop;
   while (i <= current_y - 1 && current->next != 0) {
      current = current->next;
      i++;
   }

   wrefresh(edit);

}

void page_down(void)
{
  if (editbot->next != 0 && editbot->next != filebot) {
      edit_update(editbot->next);
      center_cursor();
   } else if (editbot != filebot) {
      edit_update(editbot);
      center_cursor();
   } else while (current != filebot) {
      current = current->next;
      current_y++;
   }

   update_cursor();
}



filestruct *make_new_node(filestruct *prevnode)
{
   filestruct *newnode;

   newnode = nano_malloc(sizeof(filestruct));
   newnode->data = 0;
   newnode->prev = prevnode;
   newnode->next = 0;

   return newnode;
}

void add_to_cutbuffer(filestruct *inptr)
{
   filestruct *tmp;

   tmp = cutbuffer;
   if (cutbuffer == 0)
   {
      cutbuffer = inptr;
      inptr->next = 0;
      inptr->prev = 0;
      return;
   }
   else
   {
      while(tmp->next != 0)
         tmp = tmp->next;
   }

   tmp->next = inptr;
   inptr->prev = tmp;
   inptr->next = 0;
   cutbottom = inptr;
}

void do_cut_text(filestruct *fileptr)
{
   filestruct *tmp;
      
   tmp = fileptr->next;

   if (!keep_cutbuffer)
   {
      free_filestruct(cutbuffer);
      cutbuffer = 0;
   }

   if (fileptr == fileage)
   {
      if (fileptr->next != 0)
      {
         fileptr = fileptr->next;
         tmp = fileptr;
         fileage = fileptr;
         add_to_cutbuffer(fileptr->prev);
         fileptr->prev = 0;
         edit_update(fileage);
      }
      else
      {
         add_to_cutbuffer(fileptr);
         fileage = make_new_node(0);
      }
   } 
   else
   {
      (fileptr->prev)->next = fileptr->next;
      if (fileptr->next != 0)
         (fileptr->next)->prev = fileptr->prev;
      add_to_cutbuffer(fileptr);
   }

   if (tmp != NULL)
      current = tmp;
   else /* FIXME - wrong */
      tmp = make_new_node(tmp);
   
   if (fileptr == edittop)
      edittop = current;

   edit_refresh();
   wrefresh(edit);

   reset_cursor();

   keep_cutbuffer = 1;
}

void do_uncut_text(filestruct *fileptr)
{
   filestruct *tmp = fileptr, *newbuf, *newend;

   if (cutbuffer == 0 || fileptr == 0)
      return;	/* AIEEEEEEEEEEEE */

   newbuf = copy_filestruct(cutbuffer);
   /* Make newend = last element in newbuf */
   for (newend = newbuf; newend->next != 0 && newend != 0; 
           newend = newend->next)
      ;

   /* Hook newbuf into fileptr */
   if (fileptr != fileage)
   {
      tmp = fileptr->prev;
      tmp->next = newbuf;
      newbuf->prev = tmp;
   }
   else
      fileage = newbuf;

   /* Connect the end of the buffer to the filestruct */
   newend->next = fileptr;
   fileptr->prev = newend;
   edit_update(current);
   reset_cursor();
   wrefresh(edit);

}

void do_early_abort(void)
{
   blank_statusbar_refresh();
   bottombars(main_list, MAIN_LIST_LEN);
   reset_cursor();
}

int search_init(void)
{
   int i;

   if (strcmp(last_search, ""))	/* There's a previous search stored */
   {
      if (case_sensitive)
         i = statusq(whereis_list, WHEREIS_LIST_LEN, "", 
                     "대소문자 구분 검색 [%s]", last_search);              // "Case Sensitive Search [%s]"
      else
         i = statusq(whereis_list, WHEREIS_LIST_LEN, "", "검색 [%s]",       // "Search [%s]"
                     last_search);

      if (i == -1) /* Aborted enter */
         strncpy(answer, last_search, 132);
      else if (i == 0) /* They actually entered something */
      {
         strncpy(last_search, answer, 132);

         /* Blow away last_replace because they entered a new search
            string....uh, right? =) */
         strcpy(last_replace, "");
      }
      else if (i == TIP_CASE_KEY) /* They asked for case sensitivity */
      {
         case_sensitive = 1 - case_sensitive;
         return 1;
      }
      else /* First page, last page, for example could get here */
      {
         do_early_abort();
         return -1;
      }
   }
   else /* last_search is empty */
   {
      if (case_sensitive)
         i = statusq(whereis_list, WHEREIS_LIST_LEN, "",
                     "대소문자 구분 검색");                            // "Case Sensitive Search"
      else
         i = statusq(whereis_list, WHEREIS_LIST_LEN, "", "검색");        // "Search"
      if (i == -1)
      {
         statusbar("중단됨");                                             // "Aborted"
         reset_cursor();
         return -1;
      }
      else if (i == 0) /* They entered something new */
         strncpy(last_search, answer, 132);
      else if (i == TIP_CASE_KEY) /* They want it case sensitive */
      {
         case_sensitive = 1 - case_sensitive;
         return 1;
      }
      else /* First line key, etc. */
      {
         do_early_abort();
         return -1;
      }
   }

   return 0;
}

filestruct *findnextstr(int quiet, filestruct *begin, char *needle)
{
   filestruct *fileptr;
   char *searchstr, *found, *tmp;

   fileptr = current;

   searchstr = &current->data[current_x+1]; 
   /* Look for searchstr until EOF */
   while (fileptr != 0 && (found = strstrwrapper(searchstr, needle)) == 0)
   {
       fileptr = fileptr->next;

      if (fileptr == begin)
         return 0;

      if (fileptr != 0)
         searchstr = fileptr->data;
   }

   /* If we're not at EOF, we found an instance */
   if (fileptr != 0)
   {
      current = fileptr;
      current_x = 0;
      for (tmp = fileptr->data; tmp != found; tmp++)
         current_x++;

      edit_update(current);
      reset_cursor();
   }
   else	/* We're at EOF, go back to the top, once */
   {
      fileptr = fileage;

      while(fileptr != current && fileptr != begin && (found = strstrwrapper(fileptr->data,  needle)) == 0)
         fileptr = fileptr->next;

      if (fileptr == begin)
         return 0;

      if (fileptr != current)	/* We found something */
      {
         current = fileptr;
         current_x = 0;
         for (tmp = fileptr->data; tmp != found; tmp++)
            current_x++;

         edit_update(current);
         reset_cursor();

         if (!quiet)
            statusbar("Search Wrapped");           // ""
      }
      else	/* Nada */
      {
         if (!quiet)
            statusbar("검색 문자열을 찾을 수 없습니다");           // "Search string not found"
         return 0;
      }
   }

   return fileptr;
}

void do_search(void)
{
   int i;

   if ((i = search_init()) == -1)
      return;
   else if (i == 1)
   {
      do_search();
      return;
   }

   findnextstr(0, current, answer);
}

void print_replaced(int num)
{
   if (num > 1)
      statusbar("%d개 항목 교체됨", num);             // "Replaced %d occurences"
   else if (num == 1)
      statusbar("1개 항목 교체됨");                    // "Replaced 1 occurence"
}

void do_replace (void)
{
   int i, j, replaceall = 0, numreplaced = 0, beginx;
   filestruct *fileptr, *begin;
   char *tmp, *copy, prevanswer[132] = "";

   if ((i = search_init()) == -1)
      return;
   else if (i == 1)
   {
      do_replace();
      return;
   }

   strncpy(prevanswer, answer, 132);

   if (strcmp(last_replace, ""))	/* There's a previous replace str */
   {
      i = statusq(replace_list, REPLACE_LIST_LEN, "", "[%s]로 교체", last_replace);          // "Replace with [%s]"

      if (i == -1) /* Aborted enter */
         strncpy(answer, last_replace, 132);
      else if (i == 0) /* They actually entered something */
         strncpy(last_replace, answer, 132);
      else if (i == TIP_CASE_KEY) /* They asked for case sensitivity */
      {
         case_sensitive = 1 - case_sensitive;
         do_replace();
         return;
      }
      else /* First page, last page, for example could get here */
      {
         do_early_abort();
         return;
      }
   }
   else /* last_search is empty */
   {
      i = statusq(replace_list, REPLACE_LIST_LEN, "", "다음으로 교체");        // "Replace with"
      if (i == -1)
      {
         statusbar("중단됨");                                    // "Aborted"
         reset_cursor();
         return;
      }
      else if (i == 0) /* They entered something new */
         strncpy(last_replace, answer, 132);
      else if (i == TIP_CASE_KEY) /* They want it case sensitive */
      {
         case_sensitive = 1 - case_sensitive;
         do_replace();
         return;
      }
      else /* First line key, etc. */
      {
         do_early_abort();
         return;
      }
   }
   
   begin = current;
   beginx = current_x;
   while (1)
   {

      if (replaceall)
         fileptr = findnextstr(1, begin, prevanswer);
      else 
         fileptr = findnextstr(0, begin, prevanswer);

      if (fileptr == NULL)
      {
         current = begin;
         current_x = beginx;
         edit_update(current);
         print_replaced(numreplaced);
         return;
      }
   
      /* If we're here, we've found the search string */
      if (!replaceall)
         i = do_yesno(1, "이 항목을 교체하시겠습니까?");         // "Replace this instance?"

      if (i == 1 || replaceall) /* Yes, replace it!!!! */
      {

         /* FIXME - lots of ugly code */
         copy = nano_malloc(strlen(current->data) - strlen(last_search) + strlen(last_replace) + 1);

         strncpy(copy, current->data, current_x);
         copy[current_x] = 0;

         strcat(copy, last_replace);

         for (j = 1, tmp = current->data; j <= 
             (strlen(last_search) + current_x) && *tmp != 0; j++)
            tmp++;

         if (*tmp != 0)
            strcat(copy, tmp);

         tmp = current->data;
         current->data = copy;

         nano_free(tmp);

         edit_refresh();

         if (!modified)
         {
            modified = 1;
            titlebar();
         }
         numreplaced++;
      }
      else if (i == 2) /* replace all, aieeeeeeeeeeeeee */
         replaceall = 1;
      else if (i == -1) /* Abort, else do nothing and continue loop */
         break;
      }

   print_replaced(numreplaced);
}


void do_down(void)
{
   if (current->next != 0) {
      if (placewewant > 0)
         current_x = placewewant;

      if (current_x > my_strlen(current->next->data) - 1)
         current_x = my_strlen(current->next->data) - 1;
   }
   
   if (current_y < editwineob && current != editbot)
      current_y++;
   else
      page_down();

   wrefresh(edit);
}

void page_up(void)
{
   if (edittop != fileage)
   {
      edit_update(edittop);
      center_cursor();
   }
   else
      current_y = 0;

   update_cursor();
}

void do_up(void)
{
   if (current->prev != 0)
   {
      if (placewewant > 0)
         current_x = placewewant;

      if (current_x > strlen(current->prev->data) - 1)
         current_x = strlen(current->prev->data) - 1;
   }

   if (current_y > 0)
      current_y--;
   else
      page_up();

  wrefresh(edit);
}

void do_right(void)
{
   int char_len = 1;
   unsigned char ch = (unsigned char)current->data[current_x];

   // 현재 커서 위치의 문자가 몇 바이트짜리인지 확인
   if (ch >= 0xE0) {       // 1110xxxx: 3바이트 문자 (한글)
      char_len = 3;
   } else if (ch >= 0xC0) { // 110xxxxx: 2바이트 문자
      char_len = 2;
   } else {                 // 0xxxxxxx: 1바이트 문자 (ASCII)
      char_len = 1;
   }

   if (current_x + char_len <= my_strlen(current->data) - 1)
      current_x += char_len;
   else {
      current_x = 0;
      placewewant = 0;
      do_down();
   }

   placewewant = current_x;
}

void do_left(void)
{
   if (current_x > 0) {
      int step = 1;
      while (current_x - step > 0 && ((unsigned char)current->data[current_x - step] & 0xC0) == 0x80) {
         step++;
      }
      current_x -= step;
   }
   else if (current != fileage)
   {
      current_x = my_strlen(current->prev->data) - 1;
      placewewant = 0;
      do_up();
   }
   else
      statusbar("삐이익!");        // "Beep!"

   placewewant = current_x;
}

void delete_buffer(filestruct *inptr)
{
   if (inptr != 0)
   {
      delete_buffer(inptr->next);
      nano_free(inptr->data);
      nano_free(inptr);
   }
}

void do_backspace(void)
{
   filestruct *previous;

   if (current_x != 0) {
      int delete_len = 1;
      unsigned char *chk_ptr = (unsigned char *)current->data;

      while ((current_x - delete_len) > 0) {
         if ((chk_ptr[current_x - delete_len] & 0xC0) == 0x80) {
            delete_len++;
         } else {
            break; // 헤더(11xxxxxx)나 ASCII(0xxxxxxx)를 만나면 중단
         }
      }

      /* Let's get dangerous */
      memmove(&current->data[current_x - delete_len], &current->data[current_x], strlen(current->data) - current_x + 2);
      current->data = realloc(current->data, strlen(current->data) + 1);
      current_x -= delete_len;
   } else {
      if (current == fileage) return;	/* Can't delete past top of file */

      previous = current->prev;
      current_x = strlen(previous->data) - 1;
      previous->data = realloc(previous->data,
                       strlen(previous->data) + strlen(current->data) + 2);
      strip_newline(previous->data);
      strcat(previous->data, current->data);

      unlink_node(current);
      delete_node(current);

      if (current == edittop) page_up();
      
      current = previous;
      previous_line();
      wrefresh(edit);
   }

   if (!modified) {
      modified = 1;
      titlebar();
   }

   edit_refresh();
   update_cursor();
   wrefresh(edit);
   keep_cutbuffer = 0;
}

void do_enter(filestruct *inptr)
{
   filestruct *new;
   char *tmp;

   new = make_new_node(inptr);

   tmp = &current->data[current_x];
   new->data = nano_malloc(strlen(tmp) + 2);
   strcpy(new->data, tmp);
   *tmp++ = '\n';
   *tmp = 0;

   new->next = inptr->next;
   inptr->next = new;
   new->next->prev = new;

   current = new;
   current_x = 0;

   inptr->data = realloc(inptr->data, strlen(inptr->data) + 2);  

   if (current_y == editwinrows - 1)
      edit_update(current);
   else
      edit_refresh();

   reset_cursor();
   wrefresh(edit);
   totlines++;

}

void do_wrap(filestruct *inptr)
{
   filestruct *new;
   char *tmp, *foo;
   int backup = 0, jumptonext = 0;

   new = make_new_node(inptr);

   int i = 0;
   int visual = 0;
   char *data = inptr->data;
   while (data[i] != 0) {
      unsigned char c = (unsigned char)data[i];
      int char_len = (c >= 0xE0) ? 3 : 1; // UTF-8 문자 길이 계산
      int char_width = (c >= 0xE0) ? 2 : 1; // 문자 너비 계산

      if (visual + char_width > COLS) break;

      visual += char_width;
      i += char_len;
   }

   tmp = inptr->data + i;

   while (tmp < inptr->data) {
      if (*tmp == ' ') break;
      tmp--;
      while (tmp > inptr->data && (*(unsigned char *)tmp & 0xC0) == 0x80) {
         tmp--;
      }
   }

   if (tmp == inptr->data) tmp = inptr->data + i;
   else tmp++;

   new->data = nano_malloc(strlen(tmp) + 2);

   strcpy(new->data, tmp);
   *tmp++ = '\n';
   *tmp++ = 0;
   inptr->data = realloc(inptr->data, strlen(inptr->data) + 1);  

   if (inptr->next != 0 && inptr->next->wrapline == 1)
   {
      foo = nano_malloc(strlen(new->data) + strlen(inptr->next->data) + 2);
      strcpy(foo, new->data);
      strip_newline(foo);
      strcat(foo, inptr->next->data);
      inptr->next->data = foo;

      nano_free(new);
   }
   else
   {
      new->next = inptr->next;
      inptr->next = new;
      new->next->prev = new;
      new->wrapline = 1;
   }

   if (jumptonext == 1)
   {
      current = inptr->next;
      current_x = backup - (COLS - 1 - current_x);
   }
   else if (current_x == COLS - 1)
   {
      current = inptr->next;
      current_x = strlen(new->data)-1;
   }
   
   edit_refresh();
   reset_cursor();
   wrefresh(edit);
   totlines++;

   return;
}

int get_visual_width(char *str) {
    int width = 0;
    int i = 0;
    while (str[i] != 0) {
        unsigned char c = (unsigned char)str[i];
        if (c >= 0xE0) { 
            width += 2;  
            i += 3;
        } else {         
            width += 1;  
            i += 1;
        }
    }
    return width;
}

void check_wrap(filestruct *inptr)
{
   if (get_visual_width(inptr->data) <= COLS)
      return;
   else
      do_wrap(inptr);
}

void do_gotoline(void)
{
   long line, i = 1, j = 0;
   filestruct *fileptr;

   j = statusq(replace_list, REPLACE_LIST_LEN, "", "줄 번호 입력");         // "Enter line number"
   if (j == -1)
   {
      statusbar("중단됨");      // "Aborted"
      reset_cursor();
      return;
   }
   else if (j != 0)
   {
      do_early_abort();
      return;
   }

   if (!strcmp(answer, "$"))
   {
      current = filebot;
      current_x = 0;
      edit_update(current);
      reset_cursor();
      return;
   }

   line = my_atoi(answer);
   /* Bounds check */
   if (line <= 0)
   {
      statusbar("제발, 합리적으로 생각하세요");   // "Come on, be reasonable"
      return;
   }

   if (line > totlines) /* FIXME - make totlines update when a new line is
			   added / lines are uncut */
   {
      statusbar("사용 가능한 줄은 %d줄 뿐입니다, 마지막 줄로 건너뜁니다", totlines);     // "Only %d lines available, skipping to last line"
      current = filebot;
      current_x = 0;
      edit_update(current);
      reset_cursor();
   }
   else
   {
      for (fileptr = fileage; fileptr != NULL && i < line; i++)
         fileptr = fileptr->next;

      current = fileptr;
      current_x = 0;
      edit_update(current);
      reset_cursor();
   }   

}

void wrap_reset(void)
{
   if (current != NULL)
      current->wrapline = 0;
   else
      return;

   if (current->next != NULL)
      current->next->wrapline = 0;
}

int write_file(char *name)
{
   int fh;
   filestruct *fileptr = fileage;
   int lineswritten = 0;

   fh = api_fopen(name, 1); // nano p.txt << 이 파일에 대한 초기 정보가 fh에 들어가있음
   if (fh == 0) {
      statusbar("파일을 쓰기 위해 열 수 없습니다");         // "Could not open file for writing"
      return -1;
   }

   statusbar("쓰는 중...");         // "Writing..."

   while (fileptr != 0) {
      int len = my_strlen(fileptr->data);
      if (len > 0) {
         api_fwrite(fileptr->data, len, fh); // 데이터는 fileptr에 있고, 같이 넘겨주는게 fh다
      }

      fileptr = fileptr->next;
      lineswritten++;
   }


   api_fclose(fh);

   statusbar("%d 줄 씀", lineswritten);      // "%d lines written"
   return 0;
}

void do_writeout(void)
{
   int i;
   i = statusq(writefile_list, WRITEFILE_LIST_LEN, filename, 
                  "저장할 파일 이름");         // "File Name to write" 
   if (i != -1)
   {
      if (write_file(answer) == 0) {
         strncpy(filename, answer, 132);
         modified = 0;
         titlebar();
         statusbar("%s(으)로 씀", filename);    // "Wrote to %s"
      }
   }

   bottombars(main_list, MAIN_LIST_LEN);
   return;
}

void do_exit(int win)
{
   int i;

   if (!modified)
      finish(win);

   i = do_yesno(0, "수정된 버퍼를 저장하시겠습니까? (\"아니오\"를 선택하면 변경 내용이 사라집니다)");    // "Save modified buffer (ANSWERING \"No\" WILL DESTROY CHANGES) ?"

   if (i == 1)
      do_writeout();

   if (i != -1) 
      finish(win);

}

void do_tab(void)
{
   int i;
   for (i=0; i<4; i++) {
      insert_char_at_cursor(' ');
   }

   update_line(current);
   wrefresh(edit);

   if (!modified) {
      modified = 1;
      titlebar();
   }
}

void nano_han_flush(struct HANGUL_STATE *h, char *buf, int *pos)
{
   if (h->state != 0) {
      apihan_run(h, 0xFFFF, buf, pos);
      *pos = 0;
   }
   return;
}

void HariMain(void)
{
   char s[30], *p, *q = 0, *r = 0; // p: 커맨드라인 포인터, q: 파일이름 시작 포인터, r: 파일이름 끝 포인터
   int win_width = COLS * 8 + 48;
   int win_height = LINES * 16 + 36;
   int win;

   api_initmalloc();
   char *winbuf = (char *)nano_malloc(win_width * win_height);
   winbuf_global = winbuf;
   win_width_global = win_width;

   api_cmdline(s, 30);
   for (p = s; (unsigned char)*p > ' '; p++) { }
   for (; (unsigned char)*p != 0; ) {
	   p = skipspace(p);
      if (q != 0) {
			api_putstr("> 나노 파일\n");     // "nano file\n"
			api_end();
		}
		q = p;
		for (; (unsigned char)*p > ' '; p++) { }
		r = p;
   }

   if (r != 0) { *r = 0; }

   if (q == 0) {
        q = "";          // "newfile.txt"
   }

   strncpy(filename, q, 130);
   filename[130] = '\0';

   win = api_openwin(winbuf, win_width, win_height, -1, "나노");     // "nano"
	api_boxfilwin(win, 6, 27, win_width - 6, win_height - 6, 0);

   global_init();

   current_win = win;
   topwin = (void *)1;
   edit   = (void *)2;
   bottomwin = (void *)3;

   open_file(q);

   titlebar();
   bottombars(main_list, MAIN_LIST_LEN);

   edit_update(current);
   api_refreshwin(win, 0, 0, win_width, win_height);

   HAN_CONTEXT editor_ctx;
   editor_ctx.target = TARGET_EDITOR;
   editor_ctx.win = edit;
   editor_ctx.x_ptr = 0;
   
   apihan_init(&h_state, nano_han_writer, &editor_ctx);
   lang_mode = 1; // 한글 모드로 시작

   // apihan 사용을 위한 버퍼 (나노에서는 안 씀)
   char dummy_buf[32];
   int dummy_pos = 0;

   for (;;) {
      dummy_pos = 0;
      int key = wgetch(edit);

      switch(key) {
         case 0xFF:
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            lang_mode ^= 1;
            break;
         case 127:   // backspace
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            do_backspace();
            break;
         case 13:
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            do_enter(current);
            break;
         case 0xFE:     // tab
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            do_tab();
            break;
         case TIP_UP_KEY:
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            wrap_reset();
            do_up();
            update_cursor();
            keep_cutbuffer = 0;
            check_statblank();          
            break;
         case TIP_DOWN_KEY:
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            wrap_reset();
            do_down();
            update_cursor();
            keep_cutbuffer = 0;
            check_statblank();          
            break;
         case TIP_LEFT_KEY:
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            do_left();
            update_cursor();
            keep_cutbuffer = 0;
            check_statblank();          
            break;
         case TIP_RIGHT_KEY:
            if (lang_mode == 1) nano_han_flush(&h_state, dummy_buf, &dummy_pos);
            do_right();
            update_cursor();
            keep_cutbuffer = 0;
            check_statblank();          
            break;
	      case TIP_EXIT_KEY:
            do_exit(win);
            break;
         case TIP_WRITEOUT_KEY:
            do_writeout();
            bottombars(main_list, MAIN_LIST_LEN);
            break;
         case TIP_GOTO_KEY:
            wrap_reset();
            do_gotoline();
            keep_cutbuffer = 0;
            bottombars(main_list, MAIN_LIST_LEN);
            break;
         case TIP_WHEREIS_KEY:
            wrap_reset();
            do_search();
            keep_cutbuffer = 0;
            bottombars(main_list, MAIN_LIST_LEN);
            wrefresh(bottomwin);
            break;
         case TIP_CUT_KEY:
            do_cut_text(current);
            break;
         case 1:		/* ^A */
            current_x = 0;
            placewewant = 0;
            break;
         case TIP_INSERTFILE_KEY:
            wrap_reset();
            keep_cutbuffer = 0;
            break;
         case TIP_PREVPAGE_KEY:
            wrap_reset();
            current_x = 0;
            page_up();
            keep_cutbuffer = 0;
            check_statblank();          
            break;
         case TIP_NEXTPAGE_KEY:
            wrap_reset();
            current_x = 0;
            page_down();
            keep_cutbuffer = 0;
            check_statblank();          
            break;
         case TIP_UNCUT_KEY:
            wrap_reset();
            do_uncut_text(current);
            keep_cutbuffer = 0;
            break;
         case TIP_SPELL_KEY:
            keep_cutbuffer = 0;
            break;
         case TIP_REPLACE_KEY:
            do_replace();
            keep_cutbuffer = 0;
            bottombars(main_list, MAIN_LIST_LEN);
            break;
         case TIP_REFRESH_KEY:
            total_refresh(win);
            break;
         default:
            if (lang_mode == 0) {
               insert_char_at_cursor(key);
               check_wrap(current);
               update_line(current);
               wrefresh_rows(current_y, current_y);
            } else {
               apihan_run(&h_state, key, dummy_buf, &dummy_pos);
            }
            if (!modified) {
               modified = 1;
               titlebar();
            }
            break;
         }
      reset_cursor();
   }
   finish(win);
   api_end();
}

