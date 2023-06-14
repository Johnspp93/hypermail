/* 
** Copyright (C) 1994, 1995 Enterprise Integration Technologies Corp.
**         VeriFone Inc./Hewlett-Packard. All Rights Reserved.
** Kevin Hughes, kev@kevcom.com 3/11/94
** Kent Landfield, kent@landfield.com 4/6/97
** Hypermail Project 1998-2023
** 
** This program and library is free software; you can redistribute it and/or 
** modify it under the terms of the GNU (Library) General Public License 
** as published by the Free Software Foundation; either version 3
** of the License, or any later version. 
** 
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of 
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
** GNU (Library) General Public License for more details. 
** 
** You should have received a copy of the GNU (Library) General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA 
*/

#include <fcntl.h>

#include "hypermail.h"
#include "dmatch.h"
#include "setup.h"
#include "struct.h"
#include "printfile.h"
#include "print.h"
#include "parse.h"
#include "txt2html.h"
#include "finelink.h"
#include "getname.h"

#include "threadprint.h"

#include "proto.h"

#ifdef HAVE_DIRENT_H
#ifdef __LCC__
#include "../lcc/dirent.h"
#else
#include <dirent.h>
#endif
#else
#ifdef __LCC__
#include <direct.h>
#else
#include <sys/dir.h>
#endif
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

/* conditions that say when a message's body may be removed */
#define REMOVE_MESSAGE(email) (email->is_deleted && set_delete_level != DELETE_LEAVES_TEXT \
			       && !(email->is_deleted == 2 && set_delete_level == DELETE_LEAVES_EXPIRED_TEXT))

static char *indextypename[NO_INDEX];

#ifdef GDBM

#include "gdbm.h"

/*
** store a single message summary to an already open-for-write GDBM index
**/

int togdbm(void *gp, struct emailinfo *ep)
{
  datum key;
  datum content;
  char *buf;
  char *dp;
  int rval;
  int num = ep->msgnum;
  char *name = ep->name;
  char *email = ep->emailaddr;
  char *date = ep->datestr;
  char *msgid = ep->msgid;
  char *subject = ep->subject;
  char *inreply = ep->inreplyto;
  char *fromdate = ep->fromdatestr;
  char *charset = ep->charset;
  char *isodate = strsav(secs_to_iso(ep->date));
  char *isofromdate = strsav(secs_to_iso(ep->fromdate));
  char *exp_time_str = strsav(ep->exp_time == -1 ? "" : secs_to_iso(ep->exp_time));
  char is_deleted_str[32];
  trio_snprintf(is_deleted_str, sizeof(is_deleted_str), "%d", ep->is_deleted);

  key.dsize = sizeof(num); /* the key is the message number */
  key.dptr = (char *)&num;

  /* malloc() a string long enough for our data */
  /* AUDIT biege: trailing \0 missing */
  if (!(buf = (char *)calloc((name ? strlen(name) : 0) + (email ? strlen(email) : 0) + (date ? strlen(date) : 0) + (msgid ? strlen(msgid) : 0) + (subject ? strlen(subject) : 0) + (inreply ? strlen(inreply) : 0) + (fromdate ? strlen(fromdate) : 0) + (charset ? strlen(charset) : 0) + (isodate ? strlen(isodate) : 0) + (isofromdate ? strlen(isofromdate) : 0) + strlen(exp_time_str) + strlen(is_deleted_str) + 13, sizeof(char)))) {
    return -1;
  }

  strcpy(dp = buf, fromdate ? fromdate : "");
  dp += strlen(dp) + 1;
  strcpy(dp, date ? date : "");
  dp += strlen(dp) + 1;
  strcpy(dp, name ? name : "");
  dp += strlen(dp) + 1;
  strcpy(dp, email ? email : "");
  dp += strlen(dp) + 1;
  strcpy(dp, subject ? subject : "");
  dp += strlen(dp) + 1;
  strcpy(dp, msgid ? msgid : "");
  dp += strlen(dp) + 1;
  strcpy(dp, inreply ? inreply : "");
  dp += strlen(dp) + 1;
  strcpy(dp, charset ? charset : "");
  dp += strlen(dp) + 1;
  strcpy(dp, isofromdate ? isofromdate : "");
  dp += strlen(dp) + 1;
  strcpy(dp, isodate ? isodate : "");
  dp += strlen(dp) + 1;
  strcpy(dp, exp_time_str);
  dp += strlen(dp) + 1;
  strcpy(dp, is_deleted_str);
  dp += strlen(dp) + 1;
  content.dsize = dp - buf;
  content.dptr = buf; /* the value is in this string */
  rval = gdbm_store((GDBM_FILE) gp, key, content, GDBM_REPLACE);
  free(buf);
  free(exp_time_str);
  free(isodate);
  free(isofromdate);
  return !rval;

} /* end togdbm() */

#endif

/* Uses threadlist to find the next message after
 * msgnum in the thread containing msgnum.
 * Returns NULL if there are no more messages in 
 * the thread.
 * The skip_deleted argument instructs the function to
 * skip all deleted mails in a thread until it finds
 * a non-deleted one.
 */
static struct emailinfo *nextinthread_common(int msgnum, bool skip_deleted)
{
    struct reply *rp = threadlist;

#ifdef FASTREPLYCODE
	for (rp = threadlist_by_msgnum[msgnum]; (rp != NULL) && (rp->msgnum != msgnum); rp = rp->next) {
	;
    }
#else
	for (rp = threadlist; (rp != NULL) && (rp->msgnum != msgnum); rp = rp->next) {
	;
    }
#endif

    if (rp == NULL) {		/* msgnum not found in threadlist */
	return NULL;
    }

    rp = rp->next;

    if (skip_deleted) {
        while (rp && rp->frommsgnum != -1 && rp->data->is_deleted) {
            rp = rp->next;
        }
    }
    
    if ((rp == NULL) || (rp->frommsgnum == -1)) {	
        /*end of thread - no next msg */
	return NULL;
    }
    return rp->data;
}

/* Uses threadlist to find the next message after
 * msgnum in the thread containing msgnum.
 * Returns NULL if there are no more messages in 
 * the thread.
 */
struct emailinfo *nextinthread(int msgnum)
{
    return nextinthread_common(msgnum, FALSE);
}

/* similar to nextinthread but skips all deleted messages 
** in the thread
*/
struct emailinfo *nextinthread_skip_deleted(int msgnum)
{
    return nextinthread_common(msgnum, TRUE);
}

#if 0
/*
** Output a menu line with hyperlinks for table display
**
** All parameters are inputs only.  Parameters:
**   fp       : HTML output file pointer
**   idx      : Type of page this menu is for.
**   archives : "" or else the URL of more hypermail archives.
**   currentid: "" or else the id of the "current" message.
**   cursub   : "" or else the subject of the "current" message.
**   pos      : Called at the top or bottom of the page.
*/

void fprint_menu(FILE *fp, mindex_t idx, char *archives, char *currentid, char *cursub, int pos, struct emailsubdir *subdir)
{
    char *ptr;
    int dlev = (subdir != NULL);
    int i;
    int count_l = 0;
#if DEBUG_HTML
    printcomment(fp, "fprint_menu", "begin");
#endif
	fprintf(fp, "<div class=\"center\">\n<table border=\"2\" width=\"100%%\" class=\"links\">\n<tr>\n");

    if (set_mailcommand) {
	if (set_hmail) {
			ptr = makemailcommand(set_newmsg_command, set_hmail, currentid, cursub);
			if (strcmp(ptr, "NONE") != 0)
				fprintf(fp, "<th><a href=\"%s\">%s</a></th>\n", ptr ? ptr : "", lang[MSG_NEW_MESSAGE]);
	    if (ptr)
		free(ptr);

			if ((currentid != NULL && currentid[0] != '\0') || (cursub != NULL && cursub[0] != '\0')) {

				ptr = makemailcommand(set_replymsg_command, set_hmail, currentid, cursub);
				if (strcmp(ptr, "NONE") != 0)
					fprintf(fp, "<th><a href=\"%s\">%s</a></th>\n", ptr ? ptr : "", lang[MSG_REPLY]);
		if (ptr)
		    free(ptr);
	    }
	}
    }

    if (set_about && *set_about)
		fprintf(fp, "<th><a href=\"%s\">%s</a></th>\n", set_about, lang[MSG_ABOUT_THIS_LIST]);

    if (set_show_index_links && set_show_index_links != (pos == PAGE_TOP ? 4 : 3)) {
        if (idx != NO_INDEX && !set_reverse) {
	    if (pos == PAGE_TOP)
				fprintf(fp, "<th><a href=\"#end\">%s</a></th>\n", lang[MSG_END_OF_MESSAGES]);
	    else
				fprintf(fp, "<th><a id=\"end\" href=\"#\">%s</a></th>\n", lang[MSG_START_OF_MESSAGES]);
	}

	for (i = 0; i <= AUTHOR_INDEX; ++i) {
	    if (idx != i && show_index[dlev][i]) {
				fprintf(fp, "<th><a href=\"%s\">%s</a></th>\n", index_name[dlev][i], lang[MSG_DATE_VIEW + i]);
		++count_l;
	    }
	}

	if (show_index[dlev][ATTACHMENT_INDEX]) {
	    if (idx != ATTACHMENT_INDEX) {
				fprintf(fp, "<th><a href=\"%s\">%s</a></th>\n", index_name[dlev][ATTACHMENT_INDEX], lang[MSG_ATTACHMENT_VIEW]);
		++count_l;
	    }
	}
	if (subdir && idx != NO_INDEX) {
	    int f_cols = (subdir->prior_subdir != NULL)
	      + (subdir->next_subdir != NULL);
			const char *colspan = 2 * f_cols <= count_l ? " colspan=\"2\"" : "";
	    fprintf(fp, "</tr><tr>");
	    if (subdir->prior_subdir)
				fprintf(fp, "<th%s><a href=\"%s%s%s\">%s, %s</a></th>", colspan, subdir->rel_path_to_top, subdir->prior_subdir->subdir, index_name[dlev][idx], lang[MSG_PREV_DIRECTORY], lang[MSG_DATE_VIEW + idx]);
	    if (subdir->next_subdir)
				fprintf(fp, "<th%s><a href=\"%s%s%s\">%s, %s</a></th>", colspan, subdir->rel_path_to_top, subdir->next_subdir->subdir, index_name[dlev][idx], lang[MSG_NEXT_DIRECTORY], lang[MSG_DATE_VIEW + idx]);
	    if (show_index[0][FOLDERS_INDEX])
				fprintf(fp, "<th><a href=\"%s%s\">%s</a></th>", subdir->rel_path_to_top, index_name[0][FOLDERS_INDEX], lang[MSG_FOLDERS_INDEX]);
	}
    }

    if (archives && *archives)
		fprintf(fp, "<th><a href=\"%s\">%s</a></th>\n", archives, lang[MSG_OTHER_MAIL_ARCHIVES]);

    fprintf(fp, "</tr>\n</table>\n</div>\n");
#if DEBUG_HTML
    printcomment(fp, "fprint_menu", "end");
#endif
}

#endif

/* non-tables version of fprint_menu */

void fprint_menu0(FILE *fp, struct emailinfo *email, int pos)
{
  int dlev = (email->subdir != NULL);
  int num = email->msgnum;
  int loc_cmp = (pos == PAGE_BOTTOM ? 3 : 4);
  char *ptr;
  char *id= (pos == PAGE_TOP) ? "options2" : "options3";

#ifdef HAVE_ICONV
  size_t tmplen;
  char *tmpptr=i18n_convstring(email->subject,"UTF-8",email->charset,&tmplen);
#endif

  if (!(set_show_msg_links && set_show_msg_links != loc_cmp)
      || (set_show_index_links && set_show_index_links != loc_cmp)) {
    fprintf(fp, "<ul class=\"links hmenu_container\">\n");
  }

  if (set_mailcommand && set_hmail) {
    fprintf(fp, "<li id=\"%s\"><span class=\"heading\">%s</span>: "
	    "<ul class=\"hmenu\">", 
	    id,lang[MSG_MAIL_ACTIONS]);
    if ((email->msgid && email->msgid[0]) || (email->subject && email->subject[0])) {
#ifdef HAVE_ICONV
      ptr = makemailcommand(set_replymsg_command, set_hmail, email->msgid, tmpptr);
#else
      ptr = makemailcommand(set_replymsg_command, set_hmail, email->msgid, email->subject);
#endif
      fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", ptr ? ptr : "", lang[MSG_MA_REPLY]);
      if (ptr)
	free(ptr);
    }
#ifdef HAVE_ICONV
    ptr = makemailcommand(set_newmsg_command, set_hmail, email->msgid, tmpptr);
#else
    ptr = makemailcommand(set_newmsg_command, set_hmail, email->msgid, email->subject);
#endif
    fprintf(fp, "<li><a href=\"%s\">%s</a></li>", ptr ? ptr : "", lang[MSG_MA_NEW_MESSAGE]);
    if (ptr)
      free(ptr);
    fprintf (fp, "</ul></li>\n");
  }

  if (set_show_index_links && set_show_index_links != loc_cmp) {
    
    if (!(set_mailcommand && set_hmail)) {
        /* add the anchor if we didn't do so in the block above */
        fprintf (fp, "<li id=\"%s\">", id);
    } else  {
        fprintf (fp, "<li>");
    }
    
    fprintf(fp, "<span class=\"heading\">%s</span>: "
	    "<ul class=\"hmenu\">", lang[MSG_CONTEMPORARY_MSGS_SORTED]);
    if (show_index[dlev][DATE_INDEX])
      fprintf(fp, "<li><a href=\"%s#%s%d\">%s</a></li>\n", 
	      index_name[dlev][DATE_INDEX], set_fragment_prefix, num, 
	      lang[MSG_BY_DATE]);
    if (show_index[dlev][THREAD_INDEX])
      fprintf(fp, "<li><a href=\"%s#%s%d\">%s</a></li>\n",
	      index_name[dlev][THREAD_INDEX], set_fragment_prefix, num, 
	      lang[MSG_BY_THREAD]);
    if (show_index[dlev][SUBJECT_INDEX])
      fprintf(fp, "<li><a href=\"%s#%s%d\">%s</a></li>\n", 
	      index_name[dlev][SUBJECT_INDEX], set_fragment_prefix, num, 
	      lang[MSG_BY_SUBJECT]);
    if (show_index[dlev][AUTHOR_INDEX])
      fprintf(fp, "<li><a href=\"%s#%s%d\">%s</a></li>\n", 
	      index_name[dlev][AUTHOR_INDEX], set_fragment_prefix, num, 
	      lang[MSG_BY_AUTHOR]);
    if (show_index[dlev][ATTACHMENT_INDEX])
      fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
	      index_name[dlev][ATTACHMENT_INDEX], 
	      lang[MSG_BY_ATTACHMENT]);
    fprintf (fp, "</ul></li>\n");
    if (ihtmlhelpupfile)
      fprintf(fp, "<li><span class=\"heading\">%s</span>: %s</li>\n", lang[MSG_HELP], ihtmlhelpupfile);
  }
  
  if (set_custom_archives && *set_custom_archives)
    fprintf(fp, "<li><span class=\"heading\">%s</span>: %s</li>\n", lang[MSG_OTHER_MAIL_ARCHIVES], set_custom_archives);

  if (!(set_show_msg_links && set_show_msg_links != loc_cmp)
      || (set_show_index_links && set_show_index_links != loc_cmp)) {
    fprintf (fp,"</ul>\n");
  }

#ifdef HAVE_ICONV
  if(tmpptr)
    free(tmpptr);
#endif
}

/*----------------------------------------------------------------------------*/
#if 0

void fprint_summary(FILE *fp, int pos, long first_d, long last_d, int num)
{
#if DEBUG_HTML
    printcomment(fp, "fprint_summary", "begin");
#endif
    fprintf(fp, "<div class=\"center\">\n");
    fprintf(fp, "<table>\n");

    if (pos == PAGE_TOP) {
		fprintf(fp, "<tr>\n<th colspan=\"4\">%d %s</th>\n</tr>\n", num, lang[MSG_MESSAGES]);
		fprintf(fp, "<tr>\n  <th>%s:</th><td><em>%s</em></td>\n", lang[MSG_STARTING], getdatestr(first_d));
		fprintf(fp, "  <th>%s:</th><td><em>%s</em></td>\n</tr>\n", lang[MSG_ENDING], getdatestr(last_d));
    }
    else { /* bottom of page */
		fprintf(fp, "<tr><th><a id=\"end\">%s: </a></th><td><em>%s</em></td>\n", lang[MSG_LAST_MESSAGE_DATE], getdatestr(last_d));
		fprintf(fp, "<th>%s: </th><td><em>%s</em></td>\n", lang[MSG_ARCHIVED_ON], getlocaltime());
    }
    fprintf(fp, "</table>\n</div>\n");
#if DEBUG_HTML
    printcomment(fp, "fprint_summary", "end");
#endif
}

#endif

/* prints an index notice stating that there are no messages
in the archive. This may happen if, for example, you process
a mbox where each message has been annotated either as spam
or deleted */
void print_empty_archive_notice(FILE *fp, int end_date_num)
{
    fprintf(fp, "<main class=\"messages-list\">\n");
    if (set_empty_archive_notice) {
        fprintf(fp, "%s\n", set_empty_archive_notice);
    } else {
        fprintf(fp, "<p class=\"archive-notice\">%s</p>\n",
                lang[MSG_EMPTY_ARCHIVE_NOTICE]);
    }
    printlaststats(fp, end_date_num);
    fprintf(fp, "</main>\n");
}

/*----------------------------------------------------------------------------*/

void print_index_header_links (FILE *fp, mindex_t called_from, long startdatenum, long enddatenum, int amountmsgs, struct emailsubdir *subdir)
{
    /* 
     * Print out the links for
     * 
     *      About this archive 
     *      Other mail archives 
     *      Most recent messages 
     *      Messages sorted by: [ date ][ subject ][ author ] 
     *
     * as appropriate.
     */
    char *ptr;
    int dlev = (subdir != NULL);

#if DEBUG_HTML
    printcomment(fp, "index_header_links", "begin");
#endif


    fprintf(fp, "<nav id=\"navbar\">\n");
    fprintf(fp, "<ul class=\"hmenu_container\">\n");
    /*
     * Printout the Dates for the Starting and Ending messages 
     * in the archive, along with a count of the messages.
     */

    if ((called_from != AUTHOR_INDEX && show_index[dlev][AUTHOR_INDEX]) 
	|| (called_from != DATE_INDEX && show_index[dlev][DATE_INDEX]) 
	|| (called_from != THREAD_INDEX && show_index[dlev][THREAD_INDEX]) 
	|| (called_from != SUBJECT_INDEX && show_index[dlev][SUBJECT_INDEX])) {
      fprintf(fp, "<li><span class=\"heading\"><a href=\"#first\">"
	      "%d %s</a></span>:"
	      " <span class=\"heading\">%s</span> %s,",
	      amountmsgs, lang[MSG_ARTICLES],
	      lang[MSG_STARTING], getdatestr(startdatenum));
      fprintf(fp, " <span class=\"heading\">%s</span> %s</li>\n",
	      lang[MSG_ENDING], getdatestr(enddatenum));

      if (!set_reverse && (called_from != AUTHOR_INDEX && called_from != SUBJECT_INDEX))
	fprintf (fp, "<li><span class=\"heading\">%s</span>: <a href=\"#end\">%s</a></li>\n", lang[MSG_THIS_PERIOD],
		 lang[MSG_MOST_RECENT_MESSAGES]);

      fprintf (fp, "<li><span class=\"heading\">%s</span>: <ul class=\"hmenu\">", lang[MSG_CSORT_BY]);
    }

    /* print the links to the other indexes */
    if (show_index[dlev][THREAD_INDEX]) {
      if (called_from != THREAD_INDEX)
	fprintf(fp, "<li><a href=\"%s\" rel=\"alternate\">%s</a></li>\n", 
		index_name[dlev][THREAD_INDEX], lang[MSG_THREAD]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_THREAD]);
    }

    if (show_index[dlev][AUTHOR_INDEX]) {
      if (called_from != AUTHOR_INDEX)
	fprintf(fp, "<li><a href=\"%s\" rel=\"alternate\">%s</a></li>\n", 
		index_name[dlev][AUTHOR_INDEX], lang[MSG_AUTHOR]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_AUTHOR]);
    }

    if (show_index[dlev][DATE_INDEX]) {
      if (called_from != DATE_INDEX)
	fprintf(fp, "<li><a href=\"%s\" rel=\"alternate\">%s</a></li>\n", 
		index_name[dlev][DATE_INDEX], lang[MSG_DATE]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_DATE]);
    }

    if (show_index[dlev][SUBJECT_INDEX]) {
      if (called_from != SUBJECT_INDEX)
	fprintf(fp, "<li><a href=\"%s\" rel=\"alternate\">%s</a></li>\n", 
		index_name[dlev][SUBJECT_INDEX], lang[MSG_SUBJECT]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_SUBJECT]);
    }

    if (set_attachmentsindex) {
      if (called_from != ATTACHMENT_INDEX) {
      fprintf(fp, "<li><a href=\"%s\" rel=\"alternate\">%s</a></li>\n", 
	      index_name[dlev][ATTACHMENT_INDEX],
	      lang[MSG_ATTACHMENT]);
      }
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_ATTACHMENT]);
    }
    fprintf (fp, "</ul></li>\n");

    /* print the mail actions */
    if (set_mailcommand && set_hmail) {
      ptr = makemailcommand("mailto:$TO", set_hmail, "", "");
      fprintf (fp, "<li><span class=\"heading\">%s</span>: <ul class=\"hmenu\"><li><a href=\"%s\">%s</a></li></ul></li>\n",
	       lang[MSG_MAIL_ACTIONS], ptr ? ptr : "", lang[MSG_MA_NEW_MESSAGE]);
      if (ptr)
	free (ptr);
    }

    if (subdir) {
      fprintf(fp, "<li><span class=\"heading\">%s</span>:<ul class=\"hmenu\">", lang[MSG_OTHER_PERIODS]);
      if (subdir->prior_subdir)
	fprintf(fp, "<li><a href=\"%s%s%s\">%s, %s</a></li>", 
		subdir->rel_path_to_top, subdir->prior_subdir->subdir, 
		index_name[dlev][called_from], 
		lang[MSG_PREVPERIOD], 
		lang[MSG_DATE_VIEW + called_from]);
      if (subdir->next_subdir)
	fprintf(fp, "<li><a href=\"%s%s%s\">%s, %s</a></li>", subdir->rel_path_to_top, 
		subdir->next_subdir->subdir, index_name[dlev][called_from], 
		lang[MSG_NEXTPERIOD], 
		lang[MSG_DATE_VIEW + called_from]);
      if (show_index[0][FOLDERS_INDEX])
	fprintf(fp, "<li><a href=\"%s%s\">%s</a></li>", subdir->rel_path_to_top, 
		index_name[0][FOLDERS_INDEX],
		lang[MSG_FOLDERS_INDEX]);
      fprintf (fp, "</ul></li>\n");
    }
    
    /* the following are the custom options */
    if (ihtmlhelpupfile)
      fprintf(fp, "<li><span class=\"heading\">%s</span>: %s</li>", lang[MSG_HELP], ihtmlhelpupfile);     

    if ((set_about && *set_about) 
	|| (set_archives && *set_archives))
      {
	fprintf (fp, "<li><span class=\"heading\">%s</span>:<ul class=\"hmenu\">", lang[MSG_NEARBY]);
	if (set_about && *set_about)
	  fprintf(fp, "<li><a href=\"%s\">%s</a></li>", set_about, lang[MSG_ABOUT_THIS_ARCHIVE]);
	
	if (set_archives && *set_archives)
	  fprintf(fp, "<li><a href=\"%s\">%s</a></li>", set_archives, lang[MSG_OTHER_MAIL_ARCHIVES]);
	fprintf (fp, "</ul></li>\n");

      }

    if (set_custom_archives && *set_custom_archives)
      fprintf(fp, "<li><span class=\"heading\">%s</span>: %s</li>\n", lang[MSG_OTHER_MAIL_ARCHIVES], 
	      set_custom_archives);

    fprintf (fp, "</ul>\n</nav>\n");

    /*
     * Printout the Dates for the Starting and Ending messages 
     * in the archive, along with a count of the messages.
     */
 
#if DEBUG_HTML
    printcomment(fp, "index_header_links", "end");
#endif
}

void print_index_footer_links(FILE *fp, mindex_t called_from, long enddatenum, int amountmsgs, struct emailsubdir *subdir)
{
    /* 
     * Print out the links for
     * 
     *      Messages sorted by: [ date ][ subject ][ author ] 
     *      Other mail archives 
     *
     * as appropriate.
     */
     char *ptr;
     int dlev = (subdir != NULL);

#if DEBUG_HTML
    printcomment(fp, "index_footer_links", "begin");
#endif

    fprintf (fp, "<footer class=\"foot\">\n");
    fprintf (fp, "<nav id=\"navbarfoot\">\n");
    fprintf (fp, "<ul class=\"hmenu_container\">\n");
    
    if ((called_from != AUTHOR_INDEX && show_index[dlev][AUTHOR_INDEX]) 
	|| (called_from != DATE_INDEX && show_index[dlev][DATE_INDEX]) 
	|| (called_from != THREAD_INDEX && show_index[dlev][THREAD_INDEX]) 
	|| (called_from != SUBJECT_INDEX && show_index[dlev][SUBJECT_INDEX]))
      fprintf(fp, "<li><span class=\"heading\"><a href=\"#first\">%d %s</a>; "
	      "%s</span>:\n"
              "<ul class=\"hmenu\">\n",
	      amountmsgs, lang[MSG_ARTICLES],
	      lang[MSG_SORT_BY]);

        /* print the links to the other indexes */
    if (show_index[dlev][THREAD_INDEX]) {
      if (called_from != THREAD_INDEX)
	fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
		index_name[dlev][THREAD_INDEX], lang[MSG_THREAD]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_THREAD]);
    }

    if (show_index[dlev][AUTHOR_INDEX]) {
      if (called_from != AUTHOR_INDEX)
	fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
		index_name[dlev][AUTHOR_INDEX], lang[MSG_AUTHOR]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_AUTHOR]);
    }

    if (show_index[dlev][DATE_INDEX]) {
      if (called_from != DATE_INDEX)
	fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
		index_name[dlev][DATE_INDEX], lang[MSG_DATE]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_DATE]);
    }

    if (show_index[dlev][SUBJECT_INDEX]) {
      if (called_from != SUBJECT_INDEX)
	fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
		index_name[dlev][SUBJECT_INDEX], lang[MSG_SUBJECT]);
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_SUBJECT]);
    }

    if (set_attachmentsindex) {
      if (called_from != ATTACHMENT_INDEX) {
      fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
	      index_name[dlev][ATTACHMENT_INDEX],
	      lang[MSG_ATTACHMENT]);
      }
      else
	fprintf(fp, "<li>%s</li>\n", lang[MSG_ATTACHMENT]);
    }
    fprintf (fp, "</ul>\n</li>");

    /* print the mail actions */
    if (set_mailcommand && set_hmail) {
      ptr = makemailcommand("mailto:$TO", set_hmail, "", "");
      fprintf (fp, "<li><span class=\"heading\">%s</span>: <ul class=\"hmenu\"><li><a href=\"%s\">%s</a></li></ul></li>\n",
	       lang[MSG_MAIL_ACTIONS], ptr ? ptr : "", lang[MSG_MA_NEW_MESSAGE]);
      if (ptr)
	free (ptr);
    }

    if (subdir) {
      fprintf(fp, "<li><span class=\"heading\">%s</span>:", lang[MSG_OTHER_PERIODS]);
      if (subdir->prior_subdir)
	fprintf(fp, "<ul class=\"hmenu\"><li><a href=\"%s%s%s\">%s, %s</a></li>", 
		subdir->rel_path_to_top, subdir->prior_subdir->subdir, 
		index_name[dlev][called_from], 
		lang[MSG_PREVPERIOD], 
		lang[MSG_DATE_VIEW + called_from]);
      if (subdir->next_subdir)
	fprintf(fp, "<li><a href=\"%s%s%s\">%s, %s</a></li>", subdir->rel_path_to_top, 
		subdir->next_subdir->subdir, index_name[dlev][called_from], 
		lang[MSG_NEXTPERIOD], 
		lang[MSG_DATE_VIEW + called_from]);
      if (show_index[0][FOLDERS_INDEX])
	fprintf(fp, "<li><a href=\"%s%s\">%s</a></li>", subdir->rel_path_to_top, 
		index_name[0][FOLDERS_INDEX],
		lang[MSG_FOLDERS_INDEX]);
      fprintf (fp, "</ul>\n</li>\n");
    }

    if (ihtmlhelplowfile)
      fprintf(fp, "<li><span class=\"heading\">%s</span>: %s</li>", lang[MSG_HELP], ihtmlhelplowfile);     

    if ((set_about && *set_about) 
	|| (set_archives && *set_archives))
      {
	fprintf (fp, "<li><span class=\"heading\">%s</span>:\n"
                 "<ul class=\"hmenu\">", lang[MSG_NEARBY]);
	if (set_about && *set_about)
	  fprintf(fp, "<li><a href=\"%s\">%s</a></li>", set_about, lang[MSG_ABOUT_THIS_ARCHIVE]);
	if (set_archives && *set_archives)
	  fprintf(fp, "<li><a href=\"%s\">%s</a></li>", set_archives, lang[MSG_OTHER_MAIL_ARCHIVES]);
	fprintf (fp, "</ul>\n</li>\n");
      }

    if (set_custom_archives && *set_custom_archives)
      fprintf(fp, "<li><span class=\"heading\">%s</span>: %s</li>\n", lang[MSG_OTHER_MAIL_ARCHIVES], 
	      set_custom_archives);
    
    fprintf (fp, "</ul>\n</nav>\n");

#if DEBUG_HTML
    printcomment(fp, "index_footer_links", "end");
#endif
}

/*----------------------------------------------------------------------------*/

static void print_haof_indices(FILE *fp, struct emailsubdir *subdir)
{
    int dlev = (subdir != NULL);

    fprintf(fp, "    <indices>\n");

    if (show_index[dlev][DATE_INDEX])
		fprintf(fp, "       <dateindex>%s</dateindex>\n", index_name[dlev][DATE_INDEX]);

    if (show_index[dlev][SUBJECT_INDEX])
		fprintf(fp, "       <subjectindex>%s</subjectindex>\n", index_name[dlev][SUBJECT_INDEX]);

    if (show_index[dlev][THREAD_INDEX])
		fprintf(fp, "       <threadindex>%s</threadindex>\n", index_name[dlev][THREAD_INDEX]);

    if (show_index[dlev][AUTHOR_INDEX])
		fprintf(fp, "       <authorindex>%s</authorindex>\n", index_name[dlev][AUTHOR_INDEX]);

    if (show_index[dlev][ATTACHMENT_INDEX])
		fprintf(fp, "       <attachmentindex>%s</attachmentindex>\n", index_name[dlev][ATTACHMENT_INDEX]);

    fprintf(fp, "    </indices>\n\n");
}

/*
** Prints a comment in the file.
*/

void printcomment(FILE *fp, char *label, char *value)
{
    if (label && *label && value && *value) {
	/* we only save the non-NULL ones */
        char *ext_label;
        char *ext_value;
	char *ptr;

	/* It's illegal in a comment to have a value which has a -- sequence */
	if (strstr (label, "--"))
	  ext_label = convdash (label);
	else
	  ext_label = label;
	if (strstr (value, "--"))
	  ext_value = convdash (value);
	else
	  ext_value = value;

	ptr = strchr(ext_value, '@');
	if (set_spamprotect_id && ptr && (!strcmp(label, "id")
				       || !strcmp(label, "inreplyto"))) {
	    struct Push retbuf;
	    INIT_PUSH(retbuf);
	    PushNString(&retbuf, ext_value, ptr - ext_value);
	    PushNString(&retbuf, set_antispam_at, strlen(set_antispam_at));
	    PushString(&retbuf, ptr + 1);
	    if (ext_value != value)
	      free (ext_value);
	    ext_value = PUSH_STRING(retbuf);
	}
	
	fprintf(fp, "<!-- %s=\"%s\" -->\n", ext_label, ext_value);
	if (ext_label != label)
	    free(ext_label);
	if (ext_value != value)
	    free(ext_value);
    }
}

/*
** Prints a line of HTML. Used to filter off unwanted tags from the output.
*/

void printhtml(FILE *fp, char *line)
{
    /* the banned tags ignore all of them that begin exactly like "<tag" or
       "</tag" (case insensitive). Any other letters within the tags destroy
       this filter capability. */

    char *banned_tags[] = { "html>" };	/* ending >-letter must be included */
    unsigned int i;
    int lindex;

    while (*line) {
	if ('<' != *line)
	    fputc(*line++, fp);
	else {
	    if ('/' == line[1])
		lindex = 2;
	    else
		lindex = 1;
			for (i = 0; i < sizeof(banned_tags) / sizeof(banned_tags[0]); i++) {
				if (!strncasecmp(&line[lindex], banned_tags[i], strlen(banned_tags[i]))) {
		    line += strlen(banned_tags[i]) + lindex;
		    break;
		}
	    }
	    if (sizeof(banned_tags) / sizeof(banned_tags[0]) == i) {
		fputc(*line++, fp);
		if (2 == lindex)
		    fputc(*line++, fp);
	    }
	}
    }

}

/*
** Pretty-prints the dates in the index files.
*/
void printdates(FILE *fp, struct header *hp, int year, int month, struct emailinfo *subdir_email,
		char *prev_date_str)
{
  char *subject=NULL,*name=NULL;
  const char *startline;
  const char *break_str;
  const char *endline;
  const char *subj_tag;
  const char *subj_end_tag;
  static char date_str[DATESTRLEN+40]; /* made static for smaller stack */
  static char *first_attributes = " id=\"first\"";

  if (hp != NULL) {
    struct emailinfo *em=hp->data;
    printdates(fp, hp->left, year, month, subdir_email, prev_date_str);
    if ((year == -1 || year_of_datenum(em->date) == year)
	&& (month == -1 || month_of_datenum(em->date) == month)
	&& !em->is_deleted
	&& (!subdir_email || subdir_email->subdir == em->subdir)) {

#ifdef HAVE_ICONV
      subject = convchars(em->subject, "utf-8");
      name = convchars(em->name, "utf-8");
#else
      subject = convchars(em->subject, em->charset);
      name = convchars(em->name, em->charset);
#endif
      
      if(set_indextable) {
	startline = "<tr><td>";
	break_str = "</td><td nowrap>";
	strcpy(date_str, getdateindexdatestr(hp->data->date));
	endline = "</td></tr>";
	subj_tag = "";
	subj_end_tag = "";
      }
      else {
	char *tmp;
	bool is_first;
	tmp = getdateindexdatestr(hp->data->date);
	if (strcmp (prev_date_str, tmp)) {
	  if (*prev_date_str)  { /* close the previous date item */
	    fprintf (fp, "</ul>\n");
	    is_first = FALSE;
	  }
	  else
	    is_first = TRUE;
	  trio_snprintf(date_str, sizeof(date_str), "<h2%s class=\"heading\">%s</h2>\n<ul>\n", 
			(is_first) ? first_attributes : "", tmp);
	  fprintf (fp, "%s", date_str);
	  strcpy (prev_date_str, tmp);
	}
	date_str[0] = 0;
	startline = "<li>";
	break_str = " ";
	endline = "</li>";
	subj_tag = "";
	subj_end_tag = "";
      }

      fprintf(fp,"%s<a id=\"%s%d\" href=\"%s\">%s%s%s</a>%s<span class=\"messages-list-author\">%s</span>%s%s%s\n",
	      startline, 
	      set_fragment_prefix, em->msgnum,
              msg_href(em, subdir_email, FALSE),
	      subj_tag, subject, subj_end_tag, break_str, 
	      name,
	      break_str, date_str, endline);

      free(subject);
      free(name);
    }
    printdates(fp, hp->right, year, month, subdir_email, prev_date_str);
  }
}

/*
** Pretty-prints the files with attachments in the index files.
** Returns the number of attachments that were printed.
*/
int printattachments(FILE *fp, struct header *hp, struct emailinfo *subdir_email, bool *is_first)
{
    char *subject=NULL,*name=NULL;
    char *attdir;
    char *msgnum;
    int  nb_attach = 0;
    static char *first_attributes = " id=\"first\"";

    const char *rel_path_to_top = (subdir_email ? subdir_email->subdir->rel_path_to_top : "");

    if (hp != NULL) {
	struct emailinfo *em = hp->data;
	nb_attach = printattachments(fp, hp->left, subdir_email, is_first);
	if ((!subdir_email || subdir_email->subdir == em->subdir)
	    && !em->is_deleted) {
            
	    subject = (set_i18n) ? em->subject : convchars(em->subject, em->charset);
            name = (set_i18n) ? em->name : convchars(em->name,em->charset);

	    /* See if there's a directory corresponding to this message */
	    msgnum = message_name(em);
	    trio_asprintf(&attdir, "%s%c" DIR_PREFIXER "%s", set_dir, PATH_SEPARATOR, msgnum);
	    if (isdir(attdir)) {
	        DIR *dir = opendir(attdir);
		
		/* consider that if there's an attachment directory, there are attachments */
		nb_attach++;
		if (set_indextable) {
		  fprintf(fp, "<tr><td>%s%s</a></td><td><a id=\"%s%d\"><em>%s</em></a></td>" "<td>%s</td></tr>\n", msg_href(em, subdir_email, TRUE), subject, set_fragment_prefix, em->msgnum, name, getindexdatestr(em->date));
		}
		else {
		  fprintf(fp, "<h2%s class=\"heading\"><a id=\"%s%d\" href=\"%s\">%s</a> " 
			  "<span class=\"messages-list-author\">%s</span> <span class=\"messages-list-date\">(%s)</span></h2>\n", 
			  (*is_first) ? first_attributes : "",
                          set_fragment_prefix, em->msgnum, 
			  msg_href(em, subdir_email, FALSE), subject, 
			  name,
			  getindexdatestr(em->date));
		  if (*is_first)
		    *is_first = FALSE;
		}
		if (dir) {
#ifdef HAVE_DIRENT_H
		    struct dirent *entry;
#else
		    struct direct *entry;
#endif
		    struct stat fileinfo;
		    char *filename, *stripped_filename;
		    const char *fmt2 = (set_indextable ? "<tr><td>&nbsp;&nbsp;&nbsp;&nbsp;<a href=\"%s%s\">%s</a></td>" "<td colspan=\"2\" align=\"center\">(%d %s)</td></tr>\n" : "<li><a href=\"%s%s\">%s</a> (%d %s)</li>\n");

		    int first_time = 1;
		    while ((entry = readdir(dir))) {
		        int file_size = -1;
			if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name))
				continue;
			nb_attach++;
			if (first_time && !set_indextable) {
			    first_time = 0;
			    fprintf(fp, "<ol class=\"messageslist-attachments\">\n");
			}
			trio_asprintf(&filename, "%s%c%s", attdir, PATH_SEPARATOR, entry->d_name);
			if (!stat(filename, &fileinfo))
			    file_size = (int)fileinfo.st_size;
			free(filename);
			trio_asprintf(&filename, DIR_PREFIXER "%s%c%s", message_name(em), PATH_SEPARATOR, entry->d_name);
			stripped_filename = strchr(entry->d_name, '-');
			if (stripped_filename)
				fprintf(fp, fmt2, rel_path_to_top, filename, stripped_filename + 1, file_size, lang[MSG_BYTES]);
			else if(strcmp(entry->d_name, ".meta"))
				fprintf(fp, fmt2, rel_path_to_top, filename, entry->d_name, file_size, lang[MSG_BYTES]);
			free(filename);
		    }
		    if (!first_time && !set_indextable) {
		        fprintf(fp, "</ol>\n");
		    }
		    closedir(dir);
		}
	    }
            
	    free(attdir);
            
            if (!set_i18n) {
                free(subject);
                free(name);
            }
	}
	nb_attach += printattachments(fp, hp->right, subdir_email, is_first);
    }
    return nb_attach;
}

int showheader_list(char *header)
{
    return (!inlist (set_skip_headers, header) 
	    && (inlist(set_show_headers, header) || inlist(set_show_headers, "*")));
}

int showheader_match(char *header, char *wildcard)
{
  if (Match(header, wildcard) || Match("*", wildcard))
      return 1;
  else
      return 0;
}

/*
 * ConvURLsWithHrefs handles lines with URLs that are already written as
 * href's, to avoid having ConvURLsString add a second href to those URLs.
*/

static char *ConvURLsWithHrefs(const char *line, char *mailid, char *mailsubject, char *c, 
			       char *charset)
{
    char *p;
    struct Push retbuf;
    char *tmpline5;
    char *tmpline6;
    char *tmpline1;

    /* only do the convertion if a complete href is found */
    p = strcasestr(c, "</a>");
    if (!p)
      return NULL;

    tmpline1 = (char *)emalloc(strlen(line) + 1);
    strncpy(tmpline1, line, c - line);	/* AUDIT biege: who gurantees that c-line doesnt become smaller 0? IOF? */
    tmpline1[c - line] = 0;
    INIT_PUSH(retbuf);

    /* complete href found; run ConvURLsString on text outside it */
    tmpline5 = ConvURLsString(tmpline1, mailid, mailsubject, charset);
	if (!tmpline5)
		return NULL;
    PushString(&retbuf, tmpline5);
    free(tmpline5);
    p += 4;
    tmpline6 = NULL;
    /* pcm 2007-10-01 disabled the following ConvURLsString call to have */
    /* it escape urls in mid line the same as it did for urls at start of */
    /* line. I'm still puzzled why the change works. */
    /* tmpline6 = ConvURLsString(p, mailid, mailsubject, charset); */
    if (!tmpline6) {
	free(PUSH_STRING(retbuf));
	return NULL;
    }
    strncpy(tmpline1, c, p - c);	/* AUDIT biege: who gurantees that p-c doesnt become smaller 0? IOF? */
    tmpline1[p - c] = 0;
    /* convert markup to lower case for XHTML compatiblity */
    p = tmpline1 + 1;
    while (*p != '=') {
      *p = tolower (*p);
      p++;
    }
    p = strstr (tmpline1, "</A>");
    if (p)
      *(p+2) = 'a';
    PushString(&retbuf, tmpline1);
    PushString(&retbuf, tmpline6);
    free(tmpline1);
    free(tmpline6);
    RETURN_PUSH(retbuf);
}

/*
 * ConvMsgid converts a msgid into a link to the message it refers to if that
 * message is in the archive, and call ConvURLsString on the rest of the line.
*/

static char *ConvMsgid(char *line, char *inreply, char *mailid,
		       char *mailsubject, char *charset)
{
    char *tmpline4;
    int subjmatch;
    char *tmpline1;
    char *c;
    struct Push buff;
    struct emailinfo *ep = hashreplylookup(-1, inreply, mailsubject, &subjmatch);

    if (ep == NULL || subjmatch)
	return NULL;		/* not a known msgid - maybe an email addr? */
    c = strstr(line, inreply);
    if (!c)
	return NULL;		/* should never happen */
    INIT_PUSH(buff);
    tmpline1 = (char *)emalloc(c - line + 1);
    strncpy(tmpline1, line, c - line);	/* AUDIT biege: who gurantees that c-line doesnt become smaller 0? IOF? */
    tmpline1[c - line] = 0;
    tmpline4 = ConvURLsString(tmpline1, mailid, mailsubject, charset);
    free(tmpline1);
    PushString(&buff, tmpline4);
    free(tmpline4);
    PushString(&buff, msg_href(ep, ep, TRUE));
    PushString(&buff, inreply);
    PushString(&buff, "</a>");
    tmpline4 = ConvURLsString(c + strlen(inreply), mailid, mailsubject, charset);
    if (tmpline4) {
	PushString(&buff, tmpline4);
	free(tmpline4);
    }
	else if (0)
		printf("cant ConvURLsString %s | %s.\n", c, inreply);
    RETURN_PUSH(buff);
}


/*
** Converts URLs, email addresses, and message IDs in a line to links,
** mail commands, and articles respectively. Write everything to the
** file pointed to by 'fp'.
*/

void ConvURLs(FILE *fp, char *line, char *mailid, char *mailsubject, char *charset)
{
    char *parsed = ConvURLsString(line, mailid, mailsubject, charset);
    if (parsed) {
	/* write it to the file! */
      /* add a call to convchars with charset */
	fwrite(parsed, strlen(parsed), 1, fp);
	free(parsed);
    }
}

char *ConvURLsString(char *line, char *mailid, char *mailsubject, char *charset)
{
    char *parsed;
    char *newparse;
    char *c;

#ifdef HAVE_ICONV
    size_t tmplen;
    char *tmpptr=i18n_convstring(mailsubject,"UTF-8",charset,&tmplen);
    mailsubject=tmpptr;
#endif

    if (set_href_detection) {
      if ((c = strcasestr(line, "<A HREF=\"")) != NULL && !strcasestr(c + 9, "mailto")) {
	parsed = ConvURLsWithHrefs(line, mailid, mailsubject, c, charset);
	if (parsed) {
#ifdef HAVE_ICONV
            if (tmpptr)
                free(tmpptr);
#endif
            return parsed;
        }
      }
    }

    if (set_linkquotes) {
	char *inreply = getreply(line);
	if (inreply) {
	    parsed = ConvMsgid(line, inreply, mailid, mailsubject, charset);
	    free(inreply);
	    if (parsed) {
#ifdef HAVE_ICONV
                if (tmpptr)
                    free(tmpptr);
#endif                
	        return parsed;
            }
	}
    }

    parsed = parseurl(line, charset);

    /* as at this point we don't know how to separate the href convertions from mailto
       ones in the same line, we keep frmo doing the mailto convertion if we find a
       complete href */
    if (parsed && strcasestr(parsed, "</a>")) {
#ifdef HAVE_ICONV            
        if (tmpptr)
            free(tmpptr);
#endif                        
      return parsed;
    }
    
    /* we didn't find any previous href convertion, we try to do a mailto: convertion */
    if (use_mailcommand) {
	/* Exclude headers that are not mail type headers */

	if (parsed && *parsed) {
	    newparse = parseemail(parsed,	/* source */
				  mailid,	/* mail's Message-Id: */
				  mailsubject,	/* mail's Subject: */
                                  /* either obfuscate the address or convert it to a mailto: */
				  (use_mailcommand == 2) ? OBFUSCATE_ADDRESS : MAKEMAILCOMMAND);
	    free(parsed);
	    parsed = newparse;
	}
    }
#ifdef HAVE_ICONV
    if(tmpptr)
        free(tmpptr);
#endif

    return parsed;
}


static struct body *printheaders(FILE *fp, struct emailinfo *email, struct body *from_bp, bool msg_rfc822)
{
    struct body *bp = NULL;
    char *id = email->msgid;
    char *subject = email->subject;
    char head[128];
    char head_lower[128];
    char *header_content;

    struct hmlist *show_headers_list;
    
    if (REMOVE_MESSAGE(email)) {
      /* the following message is now shown when printing the body; we
      ** only need to return */
#if 0
      int d_index = MSG_DELETED;
      if (email->is_deleted == 2)
	d_index = MSG_EXPIRED;
      if (email->is_deleted == 4 || email->is_deleted == 8)
	d_index = MSG_FILTERED_OUT;
      fprintf(fp, "<span id=\"start\" class=\"message-deleted\">(%s)</span>\n", lang[d_index]);
#endif
      return NULL;
    }

    /* if we are dealing with a message/rfc822 attachment, use
       set_show_msg_rfc_headers if defined, otherwise fall back
       to set_show_headers */
    if (msg_rfc822 && set_show_headers_msg_rfc822) {
        show_headers_list = set_show_headers_msg_rfc822;
    } else {
        show_headers_list = set_show_headers;
    }
    
    if (show_headers_list) {
        struct hmlist *shp;
        
        for (shp = show_headers_list; shp != NULL; shp = shp->next) {

            /* if from_bp is initialized, we are dealing with an attachment, 
            ** we want to print out the headers we usually skip */
            if (inlist(set_skip_headers, shp->val)) {
                continue;
            }

            if (from_bp) {
                bp = from_bp;
            } else {
                bp = email->bodylist;
            }
            
            while (bp != NULL && bp->header) {
                if ((bp->line)[0] == '\n') {   /* don't try to convert newline */
                    break;
                }
                
                if (sscanf(bp->line, "%127[^:]", head) == 1 && showheader_match(head, shp->val)) {
                    /* this is a header we want to show */
                    
                    strcpy (head_lower, head);
                    strtolower (head_lower);

                    /* we print the header, escaping it as needed */

                    header_content = bp->line + strlen (head) + 2;
                    fprintf (fp, "<li><span class=\"%s\"><span class=\"heading\">%s</span>: ",
                             head_lower, head);


                    /* JK: avoid converting Message-Id: headers for all messages.
                       We also avoid converting all mail address  headers in 
                       message/rfc822 attachments */
                    if (use_mailcommand
                        && (!strcmp(head_lower, "message-id")
                            || msg_rfc822))
                        {
                            int prev_use_mail_command = use_mailcommand;
                            /* we instruct ConvURls to only obfuscate addresses */
                            use_mailcommand = 2;
                            ConvURLs(fp, header_content, id, subject, email->charset);
                            use_mailcommand = prev_use_mail_command;
                        }
                    else {
#ifdef HAVE_ICONV
                        size_t tmplen;
                        char *tmpptr=i18n_convstring(header_content,"UTF-8",email->charset,&tmplen);
                        ConvURLs(fp, tmpptr, id, subject, email->charset);
                        if (tmpptr)
                            free(tmpptr);
#else
                        ConvURLs(fp, header_content, id, subject, email->charset);
#endif
                    }
                    fprintf (fp, "</span></li>\n");
                }
	
                /* go to the next header or stop if we reached the end of the headers 
                   (signaled thru the \n char). */
                if ((bp->line)[0] != '\n') {
                    bp = bp->next;
                    continue;
                }
                else
                    break;
            }
        }
    } else {
        /* if we didn't print the main mail headers, caller expects bp to 
           point to next line that has to be processed */
        bp = from_bp;
    }

    /* returns last line that was processed */
    return bp;
    
} /* printheaders */

/* Closes all open sections and resets the flags. This
** function factorizes code that is used in multiple places
** in printbody()
*/
static void close_open_sections(FILE *fp, int *pre_open, int *showhtml_open,
                                int *inlinehtml_open, int *attachment_open)
{
    /* close all open tag sections */

    /* pre is used for signatures even when
    ** showhtml is enabled 
    */
    if (*pre_open) {
        fprintf(fp, "</pre>\n");
        *pre_open = FALSE;
    }
    
    if (set_showhtml == 2 && !*inlinehtml_open) {
        end_txt2html(fp);
    }
    
    if (*showhtml_open || *inlinehtml_open) {
        fprintf(fp, "</div>\n");
        *showhtml_open = FALSE;
        *inlinehtml_open = FALSE;
    }
    
    if (*attachment_open) {
        /* fprintf(fp, "</section>\n"); */
        *attachment_open = FALSE;
    }
}

/*
** The heuristics for displaying an otherwise ordinary line (a non-quote,
** non-sig, non-inhtml, non-blank line) if 'showhtml' is 1 (which really means
** we're *not* doing <pre> or txt2html to format the text) have changed so
** that indented text shows up correctly.  First, leading spaces become HTML
** non-breaking spaces (&nbsp;).  In order for this to work, those lines
** must have been preceded by a <br /> or <p>.  We accomplish this by checking
** ahead when we print each line... if we determine that the next line
** should be broken before, we do so.
*/

void printbody(FILE *fp, struct emailinfo *email, int maybe_reply, int is_reply)
{
    int insig = 0;
    int inblank = 1;
    struct body *bp = email->bodylist;
    char *id = email->msgid;
    char *subject = email->subject;
    int msgnum = email->msgnum;
    char *body_start_attribute = " id=\"start\"";
    char inheader = FALSE;	/* we always start in a mail header */
    int body_start = TRUE;      /* used to put the anchor to the first line of the body */
    int pre_open = FALSE;       /* controls if a <pre> is open */
    int showhtml_open = FALSE;  /* if using showhtml controls if the special 
				** <div> surrounding that content is open */
    int inlinehtml_open = FALSE;  /* if using inline_html controls if
				  ** the special <div> surrounding that content is open */
    int attachment_open = FALSE; /* if we generated a list of attachments, controls if the
                                 ** the <section> surrounding the list is open */
    int attachment_link_open = FALSE; /* set to true if we opened a section for the list pointing
                                         to external attachments */
    int inquote = 0;
    int quote_num = 0;
    int quoted_percent;
    bool replace_quoted;
    bool prefered_charset_is_utf8;
    
    /* used to generate unique ids for each forwarded-message
       (message/rfc822) section */
    int forwarded_message_count = 0; 
    /* used to generate unique ids for each list of stored
       attachments section */
    int list_of_stored_attachments_count = 0; 
    
    if (set_linkquotes || set_showhtml == 2)
        /* should be changed to unconditional after tested for a while?
           - pcm@rahul.net 1999-09-09 */
        find_quote_prefix(email->bodylist, is_reply);
    
    if (set_quote_hide_threshold <= 100)
	quoted_percent = compute_quoted_percent(bp);
    else
        quoted_percent = 100;
    replace_quoted = (quoted_percent > set_quote_hide_threshold);

    if (set_showprogress && replace_quoted)
        printf("\nMessage %d quoted text (%d %%) replaced by links\n", msgnum, quoted_percent);

    if (!strncasecmp (email->charset, "UTF-8", 5)) {
        prefered_charset_is_utf8 = TRUE;
    } else {
        prefered_charset_is_utf8 = FALSE;
    }
    
    /* for deleted messages, print a specific message, either default
    ** or configuration given */
    if (email->is_deleted && set_delete_level != DELETE_LEAVES_TEXT && !(email->is_deleted == 2 && set_delete_level == DELETE_LEAVES_EXPIRED_TEXT)) {
        int d_index = MSG_DELETED;
        if (email->is_deleted == 2)
            d_index = MSG_EXPIRED;
        if (email->is_deleted == 4 || email->is_deleted == 8)
            d_index = MSG_FILTERED_OUT;
        if (email->is_deleted == 64)
            d_index = MSG_DELETED_OTHER;
        switch(d_index) {
        case MSG_DELETED:
            if(set_htmlmessage_deleted_spam){
                fprintf(fp,"%s\n",set_htmlmessage_deleted_spam);
                break;
            }
        case MSG_DELETED_OTHER:
            if(set_htmlmessage_deleted_other){
                fprintf(fp,"%s\n",set_htmlmessage_deleted_other);
                break;
	}
        default:
            fprintf(fp, "<pre%s class=\"body\">\n", body_start_attribute);
            fprintf(fp, "<span class=\"message-deleted\">%s</span>\n", lang[d_index]);
            fprintf(fp, "</pre>");
        }
        return;
    }

    /* deal with messages that were edited */
    if (email->annotation_content == ANNOTATION_CONTENT_EDITED) {
        if (set_htmlmessage_edited)
            fprintf(fp,"%s\n",set_htmlmessage_edited);
        else {
            fprintf(fp, "<p%s class=\"message-edited\">%s</p>\n", body_start_attribute,
                    lang[MSG_EDITED]);
            body_start = FALSE;
        }
    }

    while (bp != NULL) {

#ifdef PRINTBODY_DEBUG
        fprintf(stderr, "========================\n");
        fprintf(stderr, "%s\n", bp->line);
        fprintf(stderr, "header: %d\n", bp->header);
        fprintf(stderr, "parsed: %d\n", bp->parsedheader);
#ifdef DELETE_ME
        fprintf(stderr, "attached: %d\n", bp->attached);
#endif
	fprintf(stderr, "attachment_rfc822: %d\n", bp->attachment_rfc822);
        fprintf(stderr, "demimed: %d\n", bp->demimed);
        fprintf(stderr, "html: %d\n", bp->html);
#endif
        
        /* skip all headers */
        if (bp->header) {
            
            if (!inheader) {
                inheader= TRUE;

                /* close open sections */
                close_open_sections(fp, &pre_open, &showhtml_open,
                                    &inlinehtml_open, &attachment_open);
            }
            bp = bp->next;
            continue;
        }

        /* we're not in a header anymore */
        
        if (inheader) {
            inheader = FALSE;
            /* initialize the variables used for each attachment */
            inquote = 0;
            quote_num = 0;
            inblank = 1;
            insig = 0;
        }

        if (bp->attachment_flags) {
            if (bp->attachment_flags & BODY_ATTACHMENT_START) {
                /* close open sections */
                close_open_sections(fp, &pre_open, &showhtml_open,
                                    &inlinehtml_open, &attachment_open);
                if (bp->attachment_rfc822) {
                    forwarded_message_count++;
                    fprintf(fp, "<section%s class=\"message-body-part\" "
                            "aria-labelledby=\"fm%d\">\n",
                            (body_start) ? body_start_attribute : "",
                            forwarded_message_count);
                    fprintf(fp, "<h2 id=\"fm%d\" class=\"forwarded-message-notice\">%s</h2>\n",
                            forwarded_message_count,
                            lang[MSG_FORWARDED_MESSAGE_NOTICE]);
                    fprintf(fp, "<div class=\"message-forwarded\">\n");
                } else {
                    fprintf(fp, "<section%s class=\"message-body-part\">\n",
                            (body_start) ? body_start_attribute : "");
                }
                if (body_start) {
                    body_start = FALSE;
                }
                /* when debugging, reveal the sections */
                if (set_debug_level == 4 && !bp->attachment_rfc822) {
                    fprintf(fp, "<h2 class=\"attached-message-notice\">%s:</h2>\n",
                            lang[MSG_ATTACHED_MESSAGE_NOTICE]);
                }

                if (bp->attachment_rfc822 && bp->next) {
                    bp = bp->next;
                    if (bp->header) {
                        /* if it's a header and the user wants to show them,  then print it 
                           and all the other headers that follow */
              
                        /* @@ check for duplicate ids */
                        bp = print_headers_rfc822_att(fp, email, bp);
                        /* reset this flag to as we just printed the attachment headers */
                        inblank = 1;
                        if (bp)
                            bp = bp->next;
                        continue;
                    }
                }
            }
            else if (bp->attachment_flags & BODY_ATTACHMENT_END) {
                /* close open sections */
                close_open_sections(fp, &pre_open, &showhtml_open,
                                    &inlinehtml_open, &attachment_open);
                if (bp->attachment_rfc822) {
                    fprintf(fp, "</div>\n");
                }
                fprintf(fp, "</section>\n");
            }
            
            bp = bp->next;
            continue;
        }

        /* handle start and end markup for list of external attachments.
           we need to improve this so it is attached to the body or message/rfc822
           and avoid closing all open sections */
        else if (bp->attachment_links_flags) {
            if (bp->attachment_links_flags & BODY_ATTACHMENT_LINKS_START) {
                /* close open sections */
                close_open_sections(fp, &pre_open, &showhtml_open,
                                    &inlinehtml_open, &attachment_open);

                list_of_stored_attachments_count++;
                fprintf(fp, "<section%s class=\"message-body-part attachment-links\" "
                        "aria-labelledby=\"lsa%d\">\n",
                        (body_start) ? body_start_attribute : "",
                        list_of_stored_attachments_count);
                fprintf(fp, "<h2 id=\"lsa%d\">%s</h2>\n",
                        list_of_stored_attachments_count,
                        lang[MSG_LIST_OF_STORED_ATTACHMENTS_NOTICE]);
                fprintf(fp, "<ul>\n");
                attachment_link_open = TRUE;
                
            } else if (bp->attachment_links_flags & BODY_ATTACHMENT_LINKS_END) {
                /* close open list and open section */
                fprintf(fp, "</ul>\n"
                        "</section>\n");
                attachment_link_open = FALSE;            
            }
            bp = bp->next;
            continue;
        }
        
        /* skip any trailing newlines at the beginning of the attachment */
	if (!bp->attachment_links  && (bp->line)[0] == '\n' && inblank) {
            bp = bp->next;
            continue;
	}
	else
            inblank = 0;
        
#if 0
	/* if we have headers that are inside an rfc822, they
	**   are marked as headers and attached, we print them out
            ** in that case */
	if (bp->attachment_rfc822 && set_show_headers && bp->next) {
	  char head[128];

          /* close open sections */
          close_open_sections(fp, &pre_open, &showhtml_open,
                              &inlinehtml_open, &attachment_open);
		
          fprintf(fp, "<section%s class=\"message-body-part\">\n",
                  (body_start) ? body_start_attribute : "");
          if (set_debug_level == 4) {
              fprintf(fp, "<h2 class=\"attached-message-notice\">%s:</h2>\n",
                      lang[MSG_ATTACHED_MESSAGE_NOTICE]);
          }
          
          fprintf(fp, "%s\n", bp->line);
          attachment_open = TRUE;
          bp = bp->next;
	  if (sscanf(bp->line, "%127[^:]", head) == 1) {
              /* if it's a header and the user wants to show them,  then print it 
                 and all the other headers that follow */
              
              /* @@ check for duplicate ids */
              bp = print_headers_rfc822_att(fp, email, bp);
	      /* reset this flag to as we just printed the attachment headers */
	      inblank = 1;
	      if (bp)
                  bp = bp->next;
              continue;
          }
        }
#endif

	if (bp->html) {
            /* already in HTML, don't touch. It may be either inline
             * html or an attachment list */

            if (bp->attachment_links) {
                if (!attachment_link_open) {
                    if (!attachment_open) {
                        /* close open sections */
                        close_open_sections(fp, &pre_open, &showhtml_open,
                                            &inlinehtml_open, &attachment_open);
                        
                        fprintf(fp, "<section%s class=\"message-body-part\">\n",
                                (body_start) ? body_start_attribute : "");
                        
                        if (set_debug_level == 4) {
                            fprintf(fp, "<h2 class=\"attached-message-notice\">%s:</h2>\n",
                                    lang[MSG_ATTACHED_MESSAGE_NOTICE]);
                        }
                        attachment_link_open = TRUE;
                    }
                }
                
                /* attachment descriptions and comments are coded in utf-8.
                   If the prefered charset for the message is not utf-8,
                   we convert the line before printing it */
#ifdef HAVE_ICONV
                if (!prefered_charset_is_utf8) {
                    size_t tmplen;
                    char *tmpline = i18n_convstring( bp->line, "UTF-8", email->charset, &tmplen);

                    free(bp->line);
                    bp->line = tmpline;
                }
#endif
            }

            else if (!inlinehtml_open) {
                /* close open sections */
                close_open_sections(fp, &pre_open, &showhtml_open,
                                    &inlinehtml_open, &attachment_open);
                fprintf(fp, "<div%s class=\"inlinehtml-body\">\n",
                        (body_start) ? body_start_attribute : "");
                inlinehtml_open = TRUE;
            }
            
            if (body_start) {
                body_start = FALSE;
            }

            fprintf(fp, "%s", bp->line);
            bp = bp->next;
            continue;
        }

        /* if we get here, decide if we need to convert the body to html or 
        ** print it inside a pre, with an exception for sigs which are always 
        ** in pre sections when showhtml == 1*/
        if (set_showhtml) {
            if (!showhtml_open) {
                fprintf(fp, "<div%s class=\"showhtml-body\">\n",
                        (body_start) ? body_start_attribute : "");
                if (body_start) {
                    body_start = FALSE;
                }
                if (set_showhtml == 2) {
                    init_txt2html();
                }
                showhtml_open = TRUE;
            }
	}

	if (set_showhtml == 2) {
	    txt2html(fp, email, bp, replace_quoted, maybe_reply);
            bp = bp->next;
            continue;
	}

	if (set_showhtml) {
            if (is_sig_start(bp->line)) {
                insig = 1;
                if (!pre_open) {
                    fprintf(fp, "<pre>\n");
                    pre_open = TRUE;
                }
            }
	  
            if ((bp->line)[0] == '\n')
                /* within the <pre></pre> statements you do not need to
                   insert <p> statements since text is already preformated.
                   the W3C HTML validation script fails for such pages 
                   Akis Karnouskos <akis@ceid.upatras.gr>     */
                {
                    if (!pre_open)
                        fprintf(fp, "<br />\n");
                }
            else {
                if (insig) {
                    ConvURLs(fp, bp->line, id, subject, email->charset);
                }
                else if (isquote(bp->line)) {
                    if (set_linkquotes) {
                        if (handle_quoted_text(fp, email, bp, bp->line, inquote, quote_num, replace_quoted, maybe_reply)) {
                            ++quote_num;
                            inquote = 1;
                        }
                    }
                    else {
                        fprintf(fp, "<%s class=\"quote %s\">", set_iquotes ? "em" : "span", find_quote_class(bp->line));
                        
                        ConvURLs(fp, bp->line, id, subject, email->charset);
                        
                        fprintf(fp, "%s<br />\n", (set_iquotes) ? "</em>" : "</span>");
                    }
                }
                else if ((bp->line)[0] != '\0' && !bp->header) {
                    char *sp;
                    sp = print_leading_whitespace(fp, bp->line);
	      
                    /* JK: avoid converting Message-Id: headers */
                    /* @@ change this for a strstr */
                    if (bp->header && bp->parsedheader && !strncasecmp(bp->line, "Message-Id:", 11)
                        && use_mailcommand) {
                        /* we desactivate it just during this conversion */
                        use_mailcommand = 0;
                        ConvURLs(fp, sp, id, subject, email->charset);
                        use_mailcommand = 1;
                    }
                    else
                        ConvURLs(fp, sp, id, subject, email->charset);
	      
                    /*
                     * Determine whether we should break.
                     * We could check for leading spaces
                     * or quote lines, but in general,
                     * non-alphanumeric lines should be
                     * broken before.
                     */
	      
                    if ((set_showbr) || ((bp->next != NULL) && !isalnum(bp->next->line[0])))
                        fprintf(fp, "<br />");
                    fprintf(fp, "\n");
                    }
            }
        }

        /* this section prints the text message inside <pre> */
	else if ((bp->line)[0] != '\0' && !bp->header) {
            if (!pre_open) {
                fprintf(fp, "<pre%s class=\"body\">\n", (body_start) ? body_start_attribute : "");
                pre_open = TRUE;
                if (body_start) {
                    body_start = FALSE;
                }
            }
            
            /* JK: avoid converting Message-Id: headers that may appear in replies */
            /* @@ add strstr here */
            if (bp->header && bp->parsedheader && !strncasecmp(bp->line, "Message-Id:", 11)
                && use_mailcommand) {
                /* we desactivate it just during this conversion */
                use_mailcommand = 0;
                ConvURLs(fp, bp->line, id, subject, email->charset);
                use_mailcommand = 1;
            }
            else
                ConvURLs(fp, bp->line, id, subject, email->charset);
	}
        
	if (!isquote(bp->line))
            inquote = 0;
        
	bp = bp->next;
    }

    if (attachment_link_open) {
        fprintf(fp, "</section>\n");
        attachment_link_open = FALSE;
    }
    
    /* close all open tags */
    close_open_sections(fp, &pre_open, &showhtml_open,
                        &inlinehtml_open, &attachment_open);
}

char *print_leading_whitespace(FILE *fp, char *sp)
{
    while (*sp && (*sp == ' ' || *sp == '\t')) {
        if (*sp == '\t')
	    fprintf(fp, "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
	else
	    fprintf(fp, "&nbsp;");
	sp++;
    }
    return sp;
}

void print_headers(FILE *fp, struct emailinfo *email, int in_thread_file)
{
 /*
  * Print the message's author info and date.
  * General form: <li><span class=\"heading\">from:<span class=\"heading\">: name <email></span></li> 
  */

  char *tmp_oea;
  
#ifdef HAVE_ICONV
  size_t tmplen;
  char *tmpsubject=i18n_convstring(email->subject,"UTF-8",email->charset,&tmplen);
  char *tmptmpname=i18n_convstring(email->name,"UTF-8",email->charset,&tmplen); 
  char *tmpname=convchars(tmptmpname,"utf-8");
  free(tmptmpname);
#else
  char *tmpsubject=0;
  char *tmpname=convchars(email->name, email->charset);
#endif
  
  tmp_oea = obfuscate_email_address(email->emailaddr);

  fprintf(fp, "<ul class=\"headers\" aria-label=\"message headers\">\n");

  /* the from header */
  fprintf (fp, "<li><span class=\"from\">\n");
  fprintf (fp, "<span class=\"heading\">%s</span>: ", lang[MSG_FROM]);
  if (REMOVE_MESSAGE(email)) {
    /* don't show the email address and name if we have deleted the message */
    fprintf(fp, "&lt;%s&gt;", lang[MSG_SENDER_DELETED]);
  } 
  else if (!strcmp(email->name, email->emailaddr)) {
    if (use_mailcommand) {
      char *ptr = makemailcommand(set_mailcommand,
				  email->emailaddr,
#ifdef HAVE_ICONV
				  email->msgid, tmpsubject);
#else
				  email->msgid, email->subject);
#endif
      fprintf(fp, "&lt;<a href=\"%s\">%s</a>&gt;", ptr ? ptr : "",
              tmp_oea);
      if (ptr)
          free(ptr);
    }
    else
      fprintf(fp, "%s", tmpname);
  }
  else {
      if (use_mailcommand && strcmp(email->emailaddr, "(no email)") != 0) {
	char *ptr = makemailcommand(set_mailcommand,
				    email->emailaddr,
#ifdef HAVE_ICONV
				    email->msgid, tmpsubject);
#else
				    email->msgid, email->subject);
#endif
	fprintf(fp, "%s &lt;<a href=\"%s\">%s</a>&gt;", tmpname, ptr ? ptr : "",
		tmp_oea);
      if (ptr)
	free(ptr);
    }
    else {
      fprintf(fp, "%s &lt;%s&gt;", tmpname, 
	      (strcmp(email->emailaddr, "(no email)") != 0) ? email->emailaddr : "no email");
    }
  }
  fprintf (fp, "\n</span></li>\n");
  
  /* subject */
  if (in_thread_file)
#ifdef HAVE_ICONV
    fprintf(fp, "<li><span class=\"subject\"><span class=\"heading\">%s</span>: %s</span></li>\n", lang[MSG_SUBJECT], tmpsubject);
#else
    fprintf(fp, "<li><span class=\"subject\"><span class=\"heading\">%s</span>: %s</span></li>\n", lang[MSG_SUBJECT], tmpsubject=convchars(email->subject,email->charset));
#endif
  /* date */
  fprintf(fp, "<li><span class=\"date\"><span class=\"heading\">%s</span>: %s</span></li>\n", lang[MSG_CDATE], email->datestr);

  printheaders(fp, email, NULL, FALSE);

  fprintf(fp, "</ul>\n");

  if (set_email_address_obfuscation && tmp_oea) 
      free(tmp_oea);

    if(tmpsubject)
     free(tmpsubject);
    if(tmpname)
     free(tmpname);
}

struct body *print_headers_rfc822_att(FILE *fp, struct emailinfo *email, struct body *bp)
{
    struct body *head;
    char *date = NULL;
    char *namep = NULL;
    char *emailp = NULL;
    char *subject = NULL;
    bool hasdate = FALSE;
    bool hasfrom = FALSE;
    bool hassubject = FALSE;
    char *tmpsubject = NULL;
    char *tmpname = NULL;
    
    /* find the date, author, and subject, from a message/rfc822 attachment */
  
    for (head = bp; head->header; head = head->next) {
        char head_name[128];
        
        /*
        if (head->header && !head->demimed) {
            head->line =
                mdecodeRFC2047(head->line, strlen(head->line),charsetsave);
        */
        if (!sscanf(head->line, "%127[^:]", head_name))
            continue;

        if (!strncasecmp(head->line, "Date:", 5)) {
            date = getmaildate(head->line);
            hasdate = TRUE;
        }
        else if (!strncasecmp(head->line, "From:", 5)) {
            getname(head->line, &namep, &emailp);
            head->parsedheader = TRUE;
            if (set_spamprotect) {
                emailp = spamify(strsav(emailp));
                /* we need to "fix" the name as well, as sometimes
                   the email ends up in the name part */
                namep = spamify(strsav(namep));
            }
            hasfrom = TRUE;
        }
        else if (!strncasecmp(head->line, "Subject:", 8)) {
            subject = getsubject(head->line);
            hassubject = TRUE;
            head->parsedheader = TRUE;
        }
        
        if (hassubject && hasfrom && hasdate) {
            break;
        }
    }

    /*
   * Print the message's author info and date.
   * General form: <span class=\"heading\">from:<span class=\"heading\">: name <email></span><br /> 
   */

    fprintf(fp, "<ul class=\"headers\" aria-label=\"message headers\">\n");

#ifdef HAVE_ICONV
    if (subject) {
        size_t tmplen;
        char *tmptmpsubject;
        tmptmpsubject = i18n_convstring( subject, "UTF-8", email->charset, &tmplen);
        tmpsubject = convchars(tmptmpsubject, "utf-8");
        free(tmptmpsubject);
    }
    if (namep) {
        char *tmptmpname;
        size_t tmplen;
        
        tmptmpname = i18n_convstring(namep, "UTF-8", email->charset, &tmplen); 
        tmpname = convchars(tmptmpname, "utf-8");
        free(tmptmpname);
    }
#else
    tmpsubject = NULL;
    tmpname = convchars(namep, email->charset); 
#endif
  
    /* the from header */
    if (hasfrom) {
        char *tmp_oea = NULL;
        
        if (emailp) {
            tmp_oea = obfuscate_email_address(emailp);
        }
        
        fprintf (fp, "<li><span class=\"from\">\n");
        fprintf (fp, "<span class=\"heading\">%s</span>: ", lang[MSG_FROM]);
        fprintf (fp, "%s &lt;%s&gt;",
                 (tmpname) ? tmpname : "",
                 (emailp) ? tmp_oea : NOEMAIL);
        fprintf (fp, "\n</span></li>\n");

        if (set_email_address_obfuscation && tmp_oea) {
            free(tmp_oea);
        }
    }

    if (hasdate) {
        /* date */
        fprintf(fp, "<li><span class=\"date\"><span class=\"heading\">%s</span>: %s</span></li>\n",
                lang[MSG_CDATE], (date) ? date : NODATE);
    }

    if (hassubject) {
        /* subject */
#ifdef HAVE_ICONV
        fprintf(fp, "<li><span class=\"subject\"><span class=\"heading\">%s</span>: %s</span></li>\n",
                lang[MSG_CSUBJECT], (tmpsubject) ? tmpsubject : NOSUBJECT);
#else
        fprintf(fp, "<li><span class=\"subject\"><span class=\"heading\">%s</span>: %s</span></li>\n",
                lang[MSG_CSUBJECT], tmpsubject=convchars((subject) ? subject : NOSUBJECT,
                                                         email->charset));
#endif
    }
    
    /* print the rest of the headers the user wants */
    bp = printheaders(fp, email, bp, TRUE);

    fprintf(fp, "</ul>\n");

    if (namep)
        free(namep);
    if (emailp)
        free(emailp);
    if (date)
        free(date);
    if (subject)
        free(subject);
    if (tmpsubject)
        free(tmpsubject);
    if (tmpname)
        free(tmpname);

    return bp;
}

static char *href01(struct emailinfo *email, struct emailinfo *email2, int in_thread_file, 
		    bool generate_markup)
{
	static char buffer[256];
	if (in_thread_file) {
	  if (generate_markup)
	    sprintf(buffer, "<a href=\"#%.4d\">", email2->msgnum);
	  else
	    sprintf(buffer, "#%.4d", email2->msgnum);
	  return buffer;
	} 
	else
	  return msg_href(email2, email, generate_markup);
}

static void
print_replies(FILE *fp, struct emailinfo *email, int num, int in_thread_file)
{
    struct reply *rp;
    struct emailinfo *email2;
    char *ptr;
    bool list_started = FALSE;
#ifdef FASTREPLYCODE
    for (rp = email->replylist; rp != NULL; rp = rp->next) {
	if (hashnumlookup(rp->msgnum, &email2)) {
#else
    for (rp = replylist; rp != NULL; rp = rp->next) {
        if (rp->frommsgnum == num && hashnumlookup(rp->msgnum, &email2)) {
#endif
            if (email2->is_deleted)
                continue;
            
	    if (!list_started) {
	        list_started = TRUE;
		fprintf (fp, "<li id=\"replies\">");
	    }
	    else
	        fprintf (fp, "<li>");

	    if (rp->maybereply)
		fprintf(fp, "<span class=\"heading\">%s</span>: ", lang[MSG_MAYBE_REPLY]);
	    else
	        fprintf(fp, "<span class=\"heading\">%s</span>: ", lang[MSG_REPLY]);
	    fprintf(fp, "<a href=\"%s\">",
		    href01(email, email2, in_thread_file, FALSE));
#ifdef HAVE_ICONV
            {
                char *tmpptr;
                ptr = i18n_utf2numref(email2->subject,1);
                tmpptr = i18n_utf2numref(email2->name,1);
                fprintf(fp, "%s: \"%s\"</a></li>\n", tmpptr, ptr);
                if (tmpptr)
                    free(tmpptr);
            }
#else
	    ptr = convchars(email2->subject, email2->charset);
	    fprintf(fp, "%s: \"%s\"</a></li>\n", email2->name, ptr);
#endif
	    if (ptr)
		free(ptr);
	}
    }
    printcomment(fp, "lreply", "end");
}

static int print_links_up(FILE *fp, struct emailinfo *email, int pos, int in_thread_file)
{
	int num = email->msgnum;
	int subjmatch;
	struct emailinfo *email2;
	struct emailinfo *email_next_in_thread = nextinthread(email->msgnum);
	char *ptr;
	struct reply *rp;
	int is_reply = 0;
	int loc_cmp = (pos == PAGE_BOTTOM ? 3 : 4);

	/*
	 * Should we print message links ?
	 */

	if (set_show_msg_links && set_show_msg_links != loc_cmp) {
	    if (pos == PAGE_TOP) {
                fprintf(fp, "<nav id=\"navbar\">\n");
            } else {
                fprintf(fp, "<nav id=\"navbarfoot\">\n");
            }
	    if (pos == PAGE_TOP)
                fprintf(fp, "<ul class=\"links hmenu_container\">\n");

	    fprintf(fp, "<li>\n");
	    fprintf(fp, "<span class=\"heading\">%s</span>: ", lang[MSG_THIS_MESSAGE]);
	    fprintf(fp, "<ul class=\"hmenu\">");
	    fprintf(fp, "<li><a href=\"#start\" id=\"options1\">"
		    "%s</a></li>\n", lang[MSG_MSG_BODY]);
	    if (set_mailcommand && set_hmail) {
	      if ((email->msgid && email->msgid[0]) || (email->subject && email->subject[0])) {
#ifdef HAVE_ICONV
		size_t tmplen;
		char *tmpptr=i18n_convstring(email->subject,"UTF-8",email->charset,&tmplen);
		ptr = makemailcommand(set_replymsg_command, set_hmail, email->msgid, 
				      tmpptr);
		if (tmpptr)
		  free(tmpptr);
#else
		ptr = makemailcommand(set_replymsg_command, set_hmail, email->msgid, 
				      email->subject);
#endif
		fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n",
			ptr, lang[MSG_RESPOND]);
		if (ptr)
		  free(ptr);
	      }
	    }
	    switch(set_show_index_links){
	    case 0:
	      break;
	    case 1:
	      fprintf(fp, "<li>%s (<a href=\"#options2\">top</a>, <a href=\"#options3\">bottom</a>)</li>\n", lang[MSG_MORE_OPTIONS]);
	      break;
	    case 3:
	      fprintf(fp, "<li><a href=\"#options2\">%s</a></li>\n", lang[MSG_MORE_OPTIONS]);
	      break;
	    case 4:
	      fprintf(fp, "<li><a href=\"#options3\">%s</a></li>\n", lang[MSG_MORE_OPTIONS]);
	      break;
	    }
	    fprintf(fp, "</ul></li>\n");
	    
	    fprintf(fp, "<li>\n");
	    fprintf(fp, "<span class=\"heading\">%s</span>: ", lang[MSG_RELATED_MESSAGES]);
	    fprintf(fp, "<ul class=\"hmenu\">\n");

	    /*
	     * Is there a next message?
	     */
	    printcomment(fp, "unext", "start");
	    email2 = neighborlookup(num, 1);
	    if (email2) {
	      fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
		      msg_href (email2, email, FALSE), 
		      lang[MSG_NEXT_MESSAGE]);
	    }

	    /*
	     * Is there a previous message?
	     */

	    email2 = neighborlookup(num, -1);
#if 0
	    /* pcm removed 2002-08-30, old_reply_to is always NULL */
	    if (set_linkquotes && old_reply_to && pos == PAGE_BOTTOM && num - 1 == (get_new_reply_to() == -1 ? old_reply_to->msgnum : get_new_reply_to()))
	      email2 = NULL;
#endif

	    if (email2) {
	      fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
		      msg_relpath(email2, email), 
		      lang[MSG_PREVIOUS_MESSAGE]);
	    }

	    /*
	     * Is this message a reply to another?
	     */

	    if (email->inreplyto[0]) {
	      email2 = hashreplylookup(email->msgnum, email->inreplyto, email->subject, &subjmatch);
	      if (email2) {
		is_reply = 1;
 
		fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
			href01(email, email2, in_thread_file, FALSE), 
                        (subjmatch) ? lang[MSG_MAYBE_IN_REPLY_TO] : lang[MSG_IN_REPLY_TO]);

	      } else if (set_inreplyto_command) {
		char *tmpptr;

		tmpptr = makeinreplytocommand(set_inreplyto_command, email->subject, email->inreplyto);
		if (tmpptr) {		
		  /* use an msgid resolver */
		  fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
			  tmpptr,
			  lang[MSG_IN_REPLY_TO]);
		  free (tmpptr);
		}
	      }
	    }
	
	    /*
	     * Is there a non-deleted message next in the thread?
	     */
	    printcomment(fp, "unextthread", "start");
	    if (email_next_in_thread) {
                struct emailinfo *email_next_in_thread_sd = nextinthread(email->msgnum);
                if (email_next_in_thread_sd) {
                    fprintf(fp, "<li><a href=\"%s\">%s</a></li>\n", 
                            href01(email, email_next_in_thread_sd, in_thread_file, FALSE),
                            lang[MSG_NEXT_IN_THREAD]);
                }
                email->initial_next_in_thread = email_next_in_thread->msgnum;
	    }
	
	    /*
	     * Does this message have replies? If so, just add one link to the
	     bottom reply section.
	     */
	
	    if (set_show_msg_links == 3) {
	        /* print_replies(fp, email, num, in_thread_file); */
		is_reply = print_links(fp, email, pos, in_thread_file);
	    }
	    else if (set_showreplies) {
#ifdef FASTREPLYCODE
	      for (rp = email->replylist; rp != NULL; rp = rp->next) {
		if (hashnumlookup(rp->msgnum, &email2)) {
#else
		  for (rp = replylist; rp != NULL; rp = rp->next) {
		    if (rp->frommsgnum == num && hashnumlookup(rp->msgnum, &email2)) {
#endif
		      fprintf (fp, "<li><a href=\"#replies\">%s</a></li>\n", 
			       lang[MSG_REPLIES]);
		      break;
		    }
		  }
		  printcomment(fp, "ureply", "end");
		}

		/* close the list */
		fprintf (fp,"</ul></li>\n</ul>\n</nav>\n");
	      }
	      return is_reply;
}

int print_links(FILE *fp, struct emailinfo *email, int pos, int in_thread_file)
{
	int num = email->msgnum;
	int subjmatch;
	struct emailinfo *email2;
	struct emailinfo *email_next_in_thread = nextinthread(email->msgnum);
	char *ptr,*ptr2;
	int is_reply = 0;
	int loc_cmp = (pos == PAGE_BOTTOM ? 3 : 4);

	/*
	 * Should we print message links ?
	 */

	if (set_show_msg_links && set_show_msg_links != loc_cmp) {
	  fprintf(fp, "<ul class=\"links hmenu_container\">\n");

	  /* 
	  ** format for items: <li><span class=\"heading\">Next</span>: <a href="0047.html">
	  subject of message</a></li>\n */
	     
	  fprintf (fp, "<li><span class=\"heading\">%s</span>: <span class=\"message_body\"><a href=\"#start\">%s</a></span></li>\n", 
		   lang[MSG_THIS_MESSAGE], lang[MSG_MSG_BODY]);
	  
	  printcomment(fp, "lnext", "start");
	  /*
	   * Is there a next message?
	   */
	  
	  email2 = neighborlookup(num, 1);
	  if (email2) {
#ifdef HAVE_ICONV
	    ptr =  i18n_utf2numref(email2->subject,1);
	    ptr2 = i18n_utf2numref(email2->name,1);
#else
	    ptr = convchars(email2->subject, email2->charset);
	    ptr2 = convchars(email2->name, email2->charset);
#endif
	    fprintf(fp, "<li><span class=\"heading\">%s</span>: ", lang[MSG_NEXT_MESSAGE]);
	    fprintf(fp, "<a href=\"%s\">%s: \"%s\"</a></li>\n", 
		    msg_href(email2, email, FALSE),
		    ptr2 ? ptr2 : "", ptr ? ptr : "");
	    if (ptr)
	      free(ptr);
	    if (ptr2)
	      free(ptr2);
	  }

	  /*
	   * Is there a previous message?
	   */
	  
	  email2 = neighborlookup(num, -1);
#if 0
	  /* pcm removed 2002-08-30, old_reply_to is always NULL */
	  if (set_linkquotes && old_reply_to && pos == PAGE_BOTTOM && num - 1 == (get_new_reply_to() == -1 ? old_reply_to->msgnum : get_new_reply_to()))
	    email2 = NULL;
#endif
	  if (email2) {
#ifdef HAVE_ICONV
	    ptr = i18n_utf2numref(email2->subject,1);
	    ptr2 = i18n_utf2numref(email2->name,1);
#else
	    ptr = convchars(email2->subject, email2->charset);
	    ptr2 = convchars(email2->name, email2->charset);
#endif
	    fprintf(fp, "<li><span class=\"heading\">%s</span>: ", lang[MSG_PREVIOUS_MESSAGE]);
	    fprintf(fp, "<a href=\"%s\">%s: \"%s\"</a></li>\n", 
		    msg_href(email2, email, FALSE),
		    ptr2 ? ptr2 : "", ptr);
	    if (ptr)
	      free(ptr);
	    if (ptr2)
	      free(ptr2);
	  }

	/*
	 * Is this message a reply to another?
	 */

	if (email->inreplyto[0]) {
	  email2 = hashreplylookup(email->msgnum, email->inreplyto, email->subject, &subjmatch);
	  if (email2) {
	    is_reply = 1;
            
            if (subjmatch)
                fprintf(fp, "<li><span class=\"heading\">%s</span>: ", lang[MSG_MAYBE_IN_REPLY_TO]);
            else
                fprintf(fp, "<li><span class=\"heading\">%s</span>: ", lang[MSG_IN_REPLY_TO]);

	    if (!email2->is_deleted) {
#ifdef HAVE_ICONV
                ptr = i18n_utf2numref(email2->subject,1);
                ptr2 = i18n_utf2numref(email2->name,1);
#else
                ptr = convchars(email2->subject, email2->charset);
                ptr2 = convchars(email2->name, email2->charset);
#endif
                fprintf(fp, "<a href=\"%s\">%s: \"%s\"</a></li>\n", 
                        href01(email, email2, in_thread_file, FALSE),
                        ptr2, ptr);
                
                if (ptr)
                    free(ptr);
                if (ptr2)
                    free(ptr2);
            } else {
                fprintf(fp, "<a href=\"%s\" class=\"deleted-message-link\">%s</a></li>\n", 
                        href01(email, email2, in_thread_file, FALSE),
                        lang[MSG_DEL_SHORT]);
            }

	  } else if (set_inreplyto_command) {
	    char *tmpptr;

	    tmpptr = makeinreplytocommand(set_inreplyto_command, email->subject, email->inreplyto);
	    if (tmpptr) {		
	      /* use an msgid resolver */
	      fprintf(fp, "<li><span class=\"heading\">%s</span>: ", lang[MSG_IN_REPLY_TO]);
	      fprintf(fp, "<a href=\"%s\">%s</a></li>\n", 
		      tmpptr,
		      lang[MSG_UNKNOWN_IN_REPLY_TO]);
	      free (tmpptr);
	    }
	  }
	}

	/*
	 * Is there a message next in the thread?
	 */
	printcomment(fp, "lnextthread", "start");
	if (email_next_in_thread) {
            struct emailinfo *email_next_in_thread_sd = nextinthread(email->msgnum);
            
            if (email_next_in_thread_sd) {
                fprintf(fp, "<li><span class=\"heading\">%s</span>: ", lang[MSG_NEXT_IN_THREAD]);

                if (!email_next_in_thread_sd->is_deleted) {
#ifdef HAVE_ICONV
                    ptr = i18n_utf2numref(email_next_in_thread_sd->subject, 1);
                    ptr2 = i18n_utf2numref(email_next_in_thread_sd->name,1);
#else
                    ptr = convchars(email_next_in_thread_sd->subject, email_next_in_thread_sd->charset);
                    ptr2 = convchars(email_next_in_thread_sd->name, email_next_in_thread_sd->charset);
#endif
                    fprintf(fp, "<a href=\"%s\">%s: \"%s\"</a></li>\n", 
                            href01(email, email_next_in_thread_sd, in_thread_file, FALSE), 
                            ptr2, ptr);
                    if (ptr)
                        free(ptr);
                    if (ptr2)
                        free(ptr2);
                } else {
                    fprintf(fp, "<a href=\"%s\" class=\"deleted-message-link\">%s</a></li>\n", 
                            href01(email, email_next_in_thread_sd, in_thread_file, FALSE), 
                            lang[MSG_DEL_SHORT]);
                }
                email->initial_next_in_thread = email_next_in_thread->msgnum;
            }
        }
        
	/*
	 * Does this message have replies? If so, print them all!
	 */

	if (set_showreplies) {
	    print_replies(fp, email, num, in_thread_file);

	    /* close the list */
	    fprintf(fp, "</ul>\n");
	}
	}
	return is_reply;
}

static int has_new_replies(struct emailinfo *email)
{
    struct reply *rp;
    static int max_old_msgnum = -2;
    if (max_old_msgnum == -2) {
      if (set_nonsequential)
	max_old_msgnum = find_max_msgnum_id();
      else
	max_old_msgnum = find_max_msgnum();
    }
    if (email->msgnum == max_old_msgnum)
	return 1;  /* next msg needs to be linked, even if no new reply */
#ifdef FASTREPLYCODE
    for (rp = email->replylist; rp != NULL; rp = rp->next) {
	if (rp->frommsgnum == email->msgnum && rp->msgnum > max_old_msgnum)
	    return 1;
    }
#endif
    return 0;
}

#ifdef GDBM
static GDBM_FILE gdbm_init(void);
static GDBM_FILE gdbm_init()
{
    char indexname[MAXFILELEN];
    static GDBM_FILE gp = NULL;
    if (set_usegdbm) {
	trio_snprintf(indexname, sizeof(indexname), (set_dir[strlen(set_dir) - 1] == '/')
                      ? "%s%s" : "%s/%s", set_dir, GDBM_INDEX_NAME);

      /* open the database, creating it if necessary */
	    
	if (!(gp = gdbm_open(indexname, 0, GDBM_WRCREAT, 0664, 0))) {

	    if (set_folder_by_date && set_increment && !is_empty_archive()) {
	        trio_snprintf(errmsg, sizeof(errmsg), "Cannot open or create file \"%s\". Unable to " "do\nincremental updates with the folder_by_date " "option without using that file.", indexname);
		progerr(errmsg);
	    }
	/* couldn't open; unlink it rather than risk running
	 * with an inconsistent version; it will be recreated if
	 * necessary */

	    unlink(indexname);
	}
	else {
	    datum key;
	    datum content;
	    char buf[512];
	    key.dptr = "delete_level";
	    key.dsize = strlen(key.dptr);
	    sprintf(buf, "%d", set_delete_level);
	    content.dsize = strlen(buf) + 1;
	    content.dptr = buf; /* the value is in this string */
	    gdbm_store(gp, key, content, GDBM_REPLACE);
	}
    }
    return gp;
}
#endif

/*
 * Perform deletions on old messages when run in incremental mode.
 */

void update_deletions(int num_old)
{
    struct hashemail *hlist;
    struct reply *rp;
    int save_ov = set_overwrite;
    set_overwrite = TRUE;
    for (hlist = deletedlist; hlist != NULL; hlist = hlist->next) {
	struct emailinfo *ep;
	int num = hlist->data->msgnum;
	if (num >= num_old)
	    continue;		/* new message - already done */
	if (hashnumlookup(num, &ep)) {
	    char *filename;
	    if (ep->deletion_completed == set_delete_level) /* done already? */
	        continue;
	    if (set_delete_level != DELETE_LEAVES_TEXT) {
	        filename = articlehtmlfilename(ep);
		if (set_delete_level != DELETE_REMOVES_FILES) {
		    struct body *bp = ep->bodylist;
		    if (bp == NULL)
		        parse_old_html(num, ep, 1, 0, NULL, 0);
		    writearticles(num, num + 1);
		}
		else if (isfile(filename)) {
		    unlink(filename);
		}
		free(filename);
	    }
#ifdef FASTREPLYCODE
	    for (rp = ep->replylist; rp != NULL; rp = rp->next) {
#else
	    for (rp = replylist; rp != NULL; rp = rp->next) {
#endif
	        int rnum = rp->data->msgnum;
		if (rnum < num_old) {
		    if (!rp->data->bodylist || !rp->data->bodylist->line[0])
		        parse_old_html(rnum, rp->data, TRUE, FALSE, NULL, 0);
                    writearticles(rnum, rnum + 1); /* update MSG_IN_REPLY_TO line */
		}
	    }
	}
    }
#ifdef GDBM
    if (set_usegdbm) {
        GDBM_FILE gp = gdbm_init();
	for (hlist = deletedlist; hlist != NULL; hlist = hlist->next) {
	    if (gp) {
	        struct emailinfo *ep;
		int num = hlist->data->msgnum;
		if (num >= num_old)
		    continue;		/* new message - already done */
		if (hashnumlookup(num, &ep)) {
		    togdbm((void *)gp, ep);
		}
	    }
	}
    }
#endif
    set_overwrite = save_ov;
}

/*
** Printing...the other main part of this program!
** This writes out the articles, beginning with the number startnum.
*/

void writearticles(int startnum, int maxnum)
{
    int num, newfile;
    int is_reply = 0;
    int maybe_reply = 0; /* const, why is this here? pcm 2002-08-30 */
    struct emailinfo *email;

    struct body *bp;
    struct reply *rp;
    FILE *fp;
    char *ptr = NULL;
#ifdef HAVE_ICONV
    char *localsubject=NULL,*localname=NULL;
    size_t convlen=0;
#endif


#ifdef GDBM

    /* A gdbm hack for avoiding opening all the message files to
     * get the header comments; see parse.c for details thereof. */

    GDBM_FILE gp = gdbm_init();
#endif

    num = startnum;

    if (set_showprogress)
	printf("%s \"%s\"...    ", lang[MSG_WRITING_ARTICLES], set_dir);

    while (num < maxnum) {

	char *filename;
	if ((bp = hashnumlookup(num, &email)) == NULL) {
	    ++num;
	    continue;
	}
	filename = articlehtmlfilename(email);

#ifdef HAVE_ICONV
	if(email->subject)
	  localsubject= i18n_convstring(email->subject,"UTF-8",email->charset,&convlen);
	if(email->name)
	  localname= i18n_convstring(email->name,"UTF-8",email->charset,&convlen);
#endif

	/*
	 * Determine to overwrite files or not
	 */

	if (isfile(filename))
	    newfile = 0;
	else
	    newfile = 1;

	is_reply = 0;
	set_new_reply_to(-1, -1);

	if (email->is_deleted && set_delete_level == DELETE_REMOVES_FILES) {
	    if (!newfile) {
		unlink(filename);
	    }
#ifdef GDBM
	    else if (gp) {
		togdbm((void *)gp, email);
	    }
#endif
	    ++num;
	    free(filename);
	    continue;
	}
	else if (!newfile && !set_overwrite && !has_new_replies(email)
		 && !(email->is_deleted && set_delete_msgnum)) {
	    num++;
	    free(filename);
	    continue;
	}
	else {
	  if ((fp = fopen(filename, "w")) == NULL) { /* AUDIT biege:where? */
	        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
		progerr(errmsg);
	  }
	  if (set_report_new_file) {
	      printf("%s\n", filename);
	  }
	}

	/*
	 * Create the comment fields necessary for incremental updating
	 */
#ifdef HAVE_ICONV
	print_msg_header(fp, set_label, localsubject, set_dir,
			 localname, email, filename,
			 REMOVE_MESSAGE(email));
#else
	print_msg_header(fp, set_label, email->subject, set_dir,
			 email->name, email, filename,
			 REMOVE_MESSAGE(email));
#endif
	fprintf (fp, "<header class=\"head\">\n");

	/* print the navigation bar to upper levels */
	if (mhtmlnavbar2upfile)
	  fprintf(fp, "<nav class=\"breadcrumb\" id=\"upper\">\n%s</nav>\n", 
		  mhtmlnavbar2upfile);

	/* reset the value of ptr before we actually start using it,
	   just in case one of the ternary operations here below
	   doesn't allocate any memory */
	ptr = NULL;

	/* write the title */
#ifdef HAVE_ICONV
	fprintf(fp, "<h1>%s</h1>\n", (REMOVE_MESSAGE(email)) ? lang[MSG_SUBJECT_DELETED] :
		(ptr = convchars(localsubject, email->charset)));
#else
	fprintf(fp, "<h1>%s</h1>\n", (REMOVE_MESSAGE(email)) ? lang[MSG_SUBJECT_DELETED] :
		(ptr = convchars(email->subject, email->charset)));
#endif
	if (ptr)
	  free(ptr);

	printcomment(fp, "received", email->fromdatestr);
	printcomment(fp, "isoreceived", secs_to_iso(email->fromdate));
	printcomment(fp, "sent", email->datestr);
	printcomment(fp, "isosent", secs_to_iso(email->date));
#ifdef HAVE_ICONV
	printcomment(fp, "name", localname);
#else
	printcomment(fp, "name", email->name);
#endif
	printcomment(fp, "email", ptr = obfuscate_email_address(email->emailaddr));
        if (set_email_address_obfuscation && ptr)
            free(ptr);
#ifdef HAVE_ICONV
	ptr = convcharsnospamprotect(localsubject, email->charset);
#else
	ptr = convcharsnospamprotect(email->subject, email->charset);
#endif
 	printcomment(fp, "subject", ptr);
	if (ptr)
	    free(ptr);
	printcomment(fp, "id", email->msgid);
	printcomment(fp, "charset", email->charset);
 	printcomment(fp, "inreplyto", ptr = convcharsnospamprotect(email->inreplyto, email->charset));
	if (ptr)
	    free(ptr);
	if (email->is_deleted) {
	    char num_buf[32];
	    sprintf(num_buf, "%d", email->is_deleted);
	    printcomment(fp, "isdeleted", num_buf);
	}
	printcomment(fp, "expires", email->exp_time == -1 ? "-1" : secs_to_iso(email->exp_time));
#ifdef GDBM
	if (gp) {
		togdbm((void *)gp, email);
	}
#endif
	/*
	 * This is here because it looks better here. The table looks
	 * better before the Author info. This stuff should be in 
	 * printfile() so it could be laid out as the user wants...
	 */


	is_reply = print_links_up(fp, email, PAGE_TOP, FALSE);

	if ((set_show_index_links == 1 || set_show_index_links == 3) && !set_usetable)
	    fprint_menu0(fp, email, PAGE_TOP);
	if ((set_show_msg_links && set_show_msg_links != 4) || !set_usetable)
	  {
	    fprintf(fp, "</header>\n");
	  }

	/*
	 * Finally...print the body!
	 */

	printcomment(fp, "body", "start");
	fprintf (fp, "<main class=\"mail\">\n");
	print_headers(fp, email, FALSE);
	printbody(fp, email, maybe_reply, is_reply);
	fprintf (fp, "<p class=\"received\"><span class=\"heading\">%s</span> %s</p>\n", 
		 lang[MSG_RECEIVED_ON],  getdatestr(email->fromdate));
	fprintf (fp, "</main>\n");
	printcomment(fp, "body", "end");

	/*
	 * Should we print out the message links ?
	 */

	fprintf (fp, "<footer class=\"foot\">\n");
	fprintf (fp, "<nav id=\"navbarfoot\">\n");
	
	print_links(fp, email, PAGE_BOTTOM, FALSE);

	fprint_menu0(fp, email, PAGE_BOTTOM);

	fprintf(fp, "</nav>\n");
	
	if (set_txtsuffix) {
	  fprintf(fp, "<p><a rel=\"nofollow\" href=\"%.4d.%s\">%s</a>", email->msgnum, set_txtsuffix, lang[MSG_TXT_VERSION]);
	}

        printfooter(fp, mhtmlfooterfile, set_label, set_dir, email->subject, filename, FALSE);
	fprintf (fp, "</footer>\n");
        fprintf(fp, "</body>\n</html>\n");
        
	fclose(fp);
	
	if (get_new_reply_to() != -1) {
	  /* will only be true if set_linkquotes is */
	  struct emailinfo *e3, *e4;
	  int was_correct = 0;
	  replace_maybe_replies(filename, email, get_new_reply_to());
	  for (rp = replylist; rp != NULL; rp = rp->next) {
	    /* get rid of old guesses for where this links */
	    if (rp->msgnum == num) {
#ifdef FASTREPLYCODE
	      struct reply *rp3;
	      was_correct = (rp->frommsgnum == get_new_reply_to());
	      if (was_correct)
		break;
	      hashnumlookup(get_new_reply_to(), &e4);
	      hashnumlookup(rp->frommsgnum, &e3);
	      for (rp3 = e3->replylist; rp3 != NULL && rp3->next != NULL; rp3 = rp3->next) {
		if (rp3->next->msgnum == num) {
		  rp3->next = rp3->next->next; /* remove */
		}
	      }
	      e4->replylist = addreply(e4->replylist, e4->msgnum, email, 0, NULL);
#endif
	      rp->frommsgnum = get_new_reply_to();
	      rp->maybereply = 0;
	      break; /* revisit me */
	    }
	  }
	  if (!rp) {
	    if (hashnumlookup(num, &e3)) {
#ifdef FASTREPLYCODE
	      hashnumlookup(get_new_reply_to(), &e4);
	      if(0)
		fprintf(stderr, "update reply %2d %2d \n", get_new_reply_to(), num);
	      replylist = addreply2(replylist, e4, e3, 0, &replylist_end);
#else
	      replylist = addreply(replylist, get_new_reply_to(), e3, 0, &replylist_end);
#endif
	    }
	  }
	  if (!was_correct)
	    fixreplyheader(set_dir, num, TRUE, num);
	}
	
	if (newfile && chmod(filename, set_filemode) == -1) {
	    trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	    progerr(errmsg);
	}

	if (maxnum && !(num % 5) && set_showprogress) {
	  printf("\b\b\b\b%03.0f%c", ((float)num / (float)maxnum) * 100, '%');
	  fflush(stdout);
	}
	free(filename);
	
	num++;

#ifdef HAVE_ICONV
	if (localsubject)
	  free(localsubject);
	if (localname)
	  free(localname);
#endif
    }
    
#ifdef GDBM
    if (gp) {
        datum key;
	datum content;
	int nkey = -1;
	char num_buf[32];
	key.dsize = sizeof(nkey); /* the key -1 is for the last msgnum */
		key.dptr = (char *)&nkey;
	sprintf(num_buf, "%d", max_msgnum);
	content.dsize = strlen(num_buf) + 1;
	content.dptr = num_buf;
	gdbm_store((GDBM_FILE) gp, key, content, GDBM_REPLACE);
	gdbm_close(gp);
    }
#endif

    if (set_showprogress)
      printf("\b\b\b\b    \n");
} /* end writearticles() */
 
/*
** Write the date index...
** If email != NULL, write index for the subdir in which that email is.
*/

void writedates(int amountmsgs, struct emailinfo *email)
{
    int newfile;
    char *filename;
    FILE *fp;
    char prev_date_str[DATESTRLEN + 40];
    char *datename = index_name[email && email->subdir != NULL][DATE_INDEX];
    time_t start_date_num = email && email->subdir ? email->subdir->first_email->date : firstdatenum;
    time_t end_date_num = email && email->subdir ? email->subdir->last_email->date : lastdatenum;

    filename = htmlfilename(datename, email, "");

    if (isfile(filename))
	newfile = 0;
    else
	newfile = 1;

    if ((fp = fopen(filename, "w")) == NULL) { /* AUDIT biege: where? */
	trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
	progerr(errmsg);
    }

    if (set_showprogress)
	printf("%s \"%s\"...", lang[MSG_WRITING_DATE_INDEX], filename);

    /*
     * Print out the index file header 
     */
    print_index_header(fp, set_label, set_dir, lang[MSG_BY_DATE], datename, email);

    /* 
     * Print out archive information links at the top of the index
     */
    print_index_header_links(fp, DATE_INDEX, start_date_num, end_date_num,
			     amountmsgs, email ? email->subdir : NULL);

    fprintf (fp, "</header>\n");
    /*
     * Print out the actual message index lists. Here's the beef.
     */
    if (amountmsgs > 0) {
        if (set_indextable)
            fprintf(fp, "<div class=\"center\">\n<table>\n<tr><td><strong>%s</strong></td><td><strong>%s</strong></td><td><strong>%s</strong></td></tr>\n", lang[MSG_CSUBJECT], lang[MSG_CAUTHOR], lang[MSG_CDATE]);
        else {
            fprintf (fp, "<main class=\"messages-list\">\n");	
        }
        prev_date_str[0] = '\0';
        printdates(fp, datelist, -1, -1, email, prev_date_str);
        
        if (set_indextable) {
            fprintf(fp, "</table>\n</div>\n");
            printlaststats (fp, end_date_num);
        } else {
            if (*prev_date_str)  /* close the previous date item */
                fprintf (fp, "</ul>\n");
            printlaststats (fp, end_date_num);
            fprintf (fp, "</main>\n");
        }
    } else {
        /* print notice that the archive has no messages. 
           This can happen if all the messages in the archive
           have been annotated as either spam or deleted */
        print_empty_archive_notice(fp, end_date_num);
    }
    
    /* 
     * Print out archive information links at the bottom of the index
     */
    print_index_footer_links(fp, DATE_INDEX, end_date_num, amountmsgs,
			     email ? email->subdir : NULL);

    /* 
     * Print the index page footer.
     */
    printfooter(fp, ihtmlfooterfile, set_label, set_dir, lang[MSG_BY_DATE], datename, TRUE);
    
    fclose(fp);

    /* AUDIT biege: depending on the direc. it better to use fchmod(). */
    if (newfile && chmod(filename, set_filemode) == -1) {
	trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	progerr(errmsg);
    }
    free(filename);

    if (set_showprogress)
	putchar('\n');
}

/*
** Write the attachments index...
*/

void writeattachments(int amountmsgs, struct emailinfo *email)
{
    int newfile;
    char *filename;
    FILE *fp;
    char *attname = index_name[email && email->subdir != NULL][ATTACHMENT_INDEX];
    time_t start_date_num = email && email->subdir ? email->subdir->first_email->date : firstdatenum;
    time_t end_date_num = email && email->subdir ? email->subdir->last_email->date : lastdatenum;
    bool is_first = TRUE; /* used to print the anchor to the first element (WAI) */

    filename = htmlfilename(attname, email, "");

    if (isfile(filename))
	newfile = 0;
    else
	newfile = 1;

    if ((fp = fopen(filename, "w")) == NULL) {	/* AUDIT biege: where? */
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
        progerr(errmsg);
    }

    if (set_showprogress)
	printf("%s \"%s\"...", lang[MSG_WRITING_ATTACHMENT_INDEX], filename);

    /*
     * Print out the index file header 
     */
    print_index_header(fp, set_label, set_dir, lang[MSG_BY_ATTACHMENT], attname, email);

    /* 
     * Print out archive information links at the top of the index
     */
    print_index_header_links(fp, ATTACHMENT_INDEX, start_date_num, end_date_num,
			     amountmsgs, email ? email->subdir : NULL);

    fprintf (fp, "</header>\n");

    /*
     * Print out the actual message index lists. Here's the beef.
     */

    if (amountmsgs > 0) {
        if (set_indextable) {
            fprintf(fp, "<div class=\"center\">\n<table>\n<tr><td><strong>%s</strong></td><td><strong>%s</strong></td><td><strong>%s</strong></td></tr>\n", lang[MSG_CSUBJECT], lang[MSG_CAUTHOR], lang[MSG_CDATE]);
            printattachments(fp, datelist, email, &is_first);
            fprintf(fp, "</table>\n</div>\n");
            printlaststats (fp, end_date_num);	
        }
        else {
            fprintf (fp, "<main class=\"messages-list\">\n");
            if (printattachments(fp, datelist, email, &is_first) == 0) {
                fprintf(fp, "<h2 class=\"empty-archive\">%s</h2>\n", lang[MSG_EMPTY_ARCHIVE]);
            } else {
                printlaststats (fp, end_date_num);
            }
            fprintf(fp, "</main>\n");
        }
    } else {
        /* print notice that the archive has no messages. 
           This can happen if all the messages in the archive
           have been annotated as either spam or deleted */
        print_empty_archive_notice(fp, end_date_num);
    }

    /* 
     * Print out archive information links at the bottom of the index
     */
    print_index_footer_links(fp, ATTACHMENT_INDEX, end_date_num, amountmsgs,
			     email ? email->subdir : NULL);
    
    /* 
     * Print the index page footer.
     */
    printfooter(fp, ihtmlfooterfile, set_label, set_dir, lang[MSG_BY_DATE], attname, TRUE);

    fclose(fp);

    if (newfile && chmod(filename, set_filemode) == -1) {
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	progerr(errmsg);
    }
    free(filename);

    if (set_showprogress)
	putchar('\n');
}

/*
** Write the thread index...
*/

void writethreads(int amountmsgs, struct emailinfo *email)
{
    int newfile;
    char *filename;
    FILE *fp;
    char *thrdname = index_name[email && email->subdir != NULL][THREAD_INDEX];
    time_t start_date_num = email && email->subdir ? email->subdir->first_email->date : firstdatenum;
    time_t end_date_num = email && email->subdir ? email->subdir->last_email->date : lastdatenum;

    struct printed *pp;

    while (printedlist) {	/* cleanup needed ?? */
	pp = printedlist;
	printedlist = printedlist->next;
	free(pp);
    }

    printedlist = NULL;

    filename = htmlfilename(thrdname, email, "");

    if (isfile(filename))
	newfile = 0;
    else
	newfile = 1;

    if ((fp = fopen(filename, "w")) == NULL) {	/* AUDIT biege: where? */
	trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
	progerr(errmsg);
    }

    if (set_showprogress)
	printf("%s \"%s\"...", lang[MSG_WRITING_THREAD_INDEX], filename);

    print_index_header(fp, set_label, set_dir, lang[MSG_BY_THREAD], thrdname, email);

    /* 
     * Print out the index page links 
     */
    print_index_header_links(fp, THREAD_INDEX, start_date_num, end_date_num,
			     amountmsgs, email ? email->subdir : NULL);

    fprintf (fp, "</header>\n");

    if (amountmsgs > 0) {
        if (set_indextable) {
            fprintf(fp, "<div class=\"center\">\n<table>\n<tr><td><strong>%s</strong></td><td><strong>%s</strong></td><td><strong> %s</strong></td></tr>\n", lang[MSG_CSUBJECT], lang[MSG_CAUTHOR], lang[MSG_CDATE]);
            print_all_threads(fp, -1, -1, email);
            fprintf(fp, "</table>\n</div>\n");
            printlaststats (fp, end_date_num);	
        }
        else {
            fprintf (fp, "<main class=\"messages-list\">\n");
            print_all_threads(fp, -1, -1, email);
            printlaststats (fp, end_date_num);
            fprintf (fp, "</main>\n");
        }
    } else {
        /* print notice that the archive has no messages. 
           This can happen if all the messages in the archive
           have been annotated as either spam or deleted */
        print_empty_archive_notice(fp, end_date_num);
    } 

    /* 
     * Print out archive information links at the bottom of the index
     */
    print_index_footer_links(fp, THREAD_INDEX, end_date_num, amountmsgs,
			     email ? email->subdir : NULL);

    printfooter(fp, ihtmlfooterfile, set_label, set_dir, lang[MSG_BY_THREAD], thrdname, TRUE);

    fclose(fp);

    if (newfile && chmod(filename, set_filemode) == -1) {
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	progerr(errmsg);
    }
    free(filename);

    if (set_showprogress)
	putchar('\n');
}

/*
** Print the subject index pointers alphabetically.
*/

void printsubjects(FILE *fp, struct header *hp, char **oldsubject,
		   int year, int month, struct emailinfo *subdir_email)
{
  char *subject=NULL, *name=NULL;
  const char *startline;
  const char *break_str;
  const char *endline;
  static char date_str[DATESTRLEN+40]; /* made static for smaller stack */
  static char *first_attributes = " id=\"first\"";

  if (hp != NULL) {
    printsubjects(fp, hp->left, oldsubject, year, month, subdir_email);
    if ((year == -1 || year_of_datenum(hp->data->date) == year)
	&& (month == -1 || month_of_datenum(hp->data->date) == month)
	&& !hp->data->is_deleted
	&& (!subdir_email || subdir_email->subdir == hp->data->subdir)) {

#ifdef HAVE_ICONV
        subject = convchars(hp->data->unre_subject, "utf-8");
        name = convchars(hp->data->name, "utf-8");
#else
        subject = convchars(hp->data->subject, hp->data->charset);
        name = convchars(hp->data->name, hp->data->charset);
#endif

	if (strcasecmp(hp->data->unre_subject, *oldsubject)) {
	    if (set_indextable) {
		fprintf(fp,
			"<tr><td colspan=\"3\"><strong>%s</strong></td></tr>\n",
			subject);
	    }
	    else {
	      bool is_first;
	        if (*oldsubject && *oldsubject[0] != '\0')  { /* close the previous open list */
		  fprintf(fp, "</ul>\n");
		  is_first = FALSE;
		} 
		else
		  is_first = TRUE;

		fprintf(fp, "<h2%s class=\"heading\">%s</h2>\n", 
			(is_first) ? first_attributes : "", subject);
		fprintf(fp, "<ul>\n");
	    }
	}
	if (set_indextable) {
	    startline = "<tr><td>&nbsp;</td><td nowrap>";
	    break_str = "</td><td nowrap>";
	    strcpy(date_str, getindexdatestr(hp->data->date));
	    endline = "</td></tr>";
	}
	else {
            startline = "<li>";
	    break_str = "";
	    trio_snprintf(date_str, sizeof(date_str), "<span class=\"messages-list-date\">(%s)</span>", getindexdatestr(hp->data->date));
	    endline = "</li>";
	}
	fprintf(fp,
		"%s<a id=\"%s%d\" href=\"%s\">%s</a>%s %s%s\n",
		startline,
                set_fragment_prefix, hp->data->msgnum,
		msg_href(hp->data, subdir_email, FALSE), 
                name, break_str,        
		date_str, endline);
	*oldsubject = hp->data->unre_subject;
        
	free(subject);
	free(name);
    }
    printsubjects(fp, hp->right, oldsubject, year, month, subdir_email);
  }
}

/*
** Prints the subject index.
*/

void writesubjects(int amountmsgs, struct emailinfo *email)
{
    int newfile;
    char *filename;
    FILE *fp;
    char *subjname = index_name[email && email->subdir != NULL][SUBJECT_INDEX];
    time_t start_date_num = email && email->subdir ? email->subdir->first_email->date : firstdatenum;
    time_t end_date_num = email && email->subdir ? email->subdir->last_email->date : lastdatenum;

    filename = htmlfilename(subjname, email, "");

    if (isfile(filename))
	newfile = 0;
    else
	newfile = 1;

    if ((fp = fopen(filename, "w")) == NULL) { /* AUDIT biege: where? */
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
        progerr(errmsg);
    }

    if (set_showprogress)
	printf("%s \"%s\"...", lang[MSG_WRITING_SUBJECT_INDEX], filename);

    print_index_header(fp, set_label, set_dir, lang[MSG_BY_SUBJECT], subjname, email);
    
    /* 
     * Print out the index page links 
     */
    print_index_header_links(fp, SUBJECT_INDEX, start_date_num, end_date_num,
			     amountmsgs, email ? email->subdir : NULL);
    
    fprintf (fp, "</header>\n");

    if (amountmsgs > 0) {
        if (set_indextable) {
            fprintf(fp, "<div class=\"center\">\n<table>\n<tr><td><strong>%s</strong></td><td><strong>%s</strong></td><td><strong> %s</strong></td></tr>\n", lang[MSG_CSUBJECT], lang[MSG_CAUTHOR], lang[MSG_CDATE]);
        }
        else {
            fprintf (fp, "<main class=\"messages-list\">\n");
        }
        {
            char *oldsubject = "";	/* dummy to start with */
            printsubjects(fp, subjectlist, &oldsubject, -1, -1, email);
        }
        if (set_indextable) {
            fprintf(fp, "</table>\n</div>\n");
            printlaststats (fp, end_date_num);                
        }
        else {
            fprintf(fp, "</ul>\n");
            printlaststats (fp, end_date_num);
            fprintf (fp, "</main>\n");
        }
    } else {
        /* print notice that the archive has no messages. 
           This can happen if all the messages in the archive
           have been annotated as either spam or deleted */
        print_empty_archive_notice(fp, end_date_num);
    }

    /* 
     * Print out archive information links at the bottom of the index
     */

    print_index_footer_links(fp, SUBJECT_INDEX, end_date_num, amountmsgs,
				 email ? email->subdir : NULL);

    printfooter(fp, ihtmlfooterfile, set_label, set_dir, lang[MSG_BY_SUBJECT], subjname, TRUE);

    fclose(fp);

    if (newfile && chmod(filename, set_filemode) == -1) {
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	progerr(errmsg);
    }
    free(filename);

    if (set_showprogress)
	putchar('\n');
}

/*
** Prints the author index links sorted alphabetically.
*/

void printauthors(FILE *fp, struct header *hp, char **oldname,
		  int year, int month, struct emailinfo *subdir_email)
{
  char *subj, *tmpname;
  const char *startline;
  const char *break_str;
  const char *endline;
  static char date_str[DATESTRLEN+40]; /* made static for smaller stack */
  static char *first_attributes = " id=\"first\"";

  if (hp != NULL) {
    printauthors(fp, hp->left, oldname, year, month, subdir_email);
    if ((year == -1 || year_of_datenum(hp->data->date) == year)
	&& (month == -1 || month_of_datenum(hp->data->date) == month)
	&& !hp->data->is_deleted
	&& (!subdir_email || subdir_email->subdir == hp->data->subdir)) {

#ifdef HAVE_ICONV
      subj = convchars(hp->data->subject, "utf-8");
      tmpname = convchars(hp->data->name,"utf-8");
#else
      subj = convchars(hp->data->subject, hp->data->charset);
      tmpname = convchars(hp->data->name,hp->data->charset);
#endif
      if (strcasecmp(hp->data->name, *oldname)) {

	if(set_indextable)
	  fprintf(fp,
		  "<tr><td colspan=\"3\"><strong>%s</strong></td></tr>",
		  tmpname);
	else {
	  bool is_first;

	  if (*oldname && *oldname[0] != '\0') { /* close the previous open list */
	    fprintf(fp, "</ul>\n");
	    is_first = FALSE;
	  }
	  else
	    is_first = TRUE;

	  fprintf(fp, "<h2%s class=\"heading\">%s</h2>\n", 
		  (is_first) ? first_attributes : "",
		  tmpname);
	  fprintf(fp, "<ul>\n");
	}
      }
      if(set_indextable) {
	startline = "<tr><td>&nbsp;</td><td>";
	break_str = "</td><td nowrap>";
	strcpy(date_str, getindexdatestr(hp->data->date));
	endline = "</td></tr>";
      }
      else {
        startline = "<li>";
	break_str = "";
	trio_snprintf(date_str, sizeof(date_str), "<span class=\"messages-list-date\">(%s)</span>", getindexdatestr(hp->data->date));
	endline = "</li>";
      }

      fprintf(fp,"%s<a id=\"%s%d\" href=\"%s\">%s</a>%s %s%s\n",
	      startline,
              set_fragment_prefix, hp->data->msgnum,
              msg_href(hp->data, subdir_email, FALSE), subj, break_str,
	      date_str, endline);
      if(subj)
	free(subj);
      if(tmpname)
	free(tmpname);

      *oldname = hp->data->name;	/* avoid copying */
    }
    printauthors(fp, hp->right, oldname, year, month, subdir_email);
  }
}

/*
** Prints the author index file and links sorted alphabetically.
*/

void writeauthors(int amountmsgs, struct emailinfo *email)
{
    int newfile;
    char *filename;
    FILE *fp;
    char *authname = index_name[email && email->subdir != NULL][AUTHOR_INDEX];
    time_t start_date_num = email && email->subdir ? email->subdir->first_email->date : firstdatenum;
    time_t end_date_num = email && email->subdir ? email->subdir->last_email->date : lastdatenum;

    filename = htmlfilename(authname, email, "");

    if (isfile(filename))
	newfile = 0;
    else
	newfile = 1;

    if ((fp = fopen(filename, "w")) == NULL) { /* AUDIT biege: where? */
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
        progerr(errmsg);
    }

    if (set_showprogress)
	printf("%s \"%s\"...", lang[MSG_WRITING_AUTHOR_INDEX], filename);

    print_index_header(fp, set_label, set_dir, lang[MSG_BY_AUTHOR], authname, email);

    /* 
     * Print out the index page links 
     */
    print_index_header_links(fp, AUTHOR_INDEX, start_date_num, end_date_num,
			     amountmsgs, email ? email->subdir : NULL);

    fprintf (fp, "</header>\n");

    if (amountmsgs > 0) {
        if (set_indextable) {
            fprintf(fp, "<div class=\"center\">\n<table>\n<tr><td><strong>%s</strong></td><td><strong>%s</strong></td><td><strong> %s</strong></td></tr>\n", lang[MSG_CAUTHOR], lang[MSG_CSUBJECT], lang[MSG_CDATE]);
        }
        else {
            fprintf(fp, "<main class=\"messages-list\">\n");
        }
        {
            char *prevauthor = "";
            printauthors(fp, authorlist, &prevauthor, -1, -1, email);
        }
        if (set_indextable) {
            fprintf(fp, "</table>\n</div>\n");
            printlaststats (fp, end_date_num);	
        }
        else {
            fprintf(fp, "</ul>\n");
            printlaststats (fp, end_date_num);
            fprintf(fp, "</main>\n");
        }
    } else {
        /* print notice that the archive has no messages. 
           This can happen if all the messages in the archive
           have been annotated as either spam or deleted */
        print_empty_archive_notice(fp, end_date_num);
    }

    /* 
     * Print out archive information links at the bottom 
     * of the index page
     */

    print_index_footer_links(fp, AUTHOR_INDEX, end_date_num, amountmsgs,
				 email ? email->subdir : NULL);

    printfooter(fp, ihtmlfooterfile, set_label, set_dir, lang[MSG_BY_AUTHOR], authname, TRUE);

    fclose(fp);

    if (newfile && chmod(filename, set_filemode) == -1) {
	trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	progerr(errmsg);
    }
    free(filename);

    if (set_showprogress)
	putchar('\n');
}

/*
** Pretty-prints the items for the haof
*/
static void printhaofitems(FILE *fp, struct header *hp, int year, int month, struct emailinfo *subdir_email)
{
  char *subj, *from_name, *from_emailaddr;

  if (hp != NULL) {
    struct emailinfo *em = hp->data;
    printhaofitems(fp, hp->left, year, month, subdir_email);
    if ((year == -1 || year_of_datenum(em->date) == year)
	&& (month == -1 || month_of_datenum(em->date) == month)
        && !em->is_deleted && (!subdir_email || subdir_email->subdir == em->subdir)) {

#ifdef HAVE_ICONV
      subj = convchars(em->subject, "utf-8");
      from_name = convchars(em->name, "utf-8");
      from_emailaddr = convchars(em->emailaddr, "utf-8");
#else
      subj = convchars(em->subject, em->charset);
      from_name = convchars(em->name, em->charset);
      from_emailaddr = convchars(em->emailaddr, em->charset);
#endif

      fprintf(fp, "      <mail>\n" "        <subject>%s</subject>\n" "        <date>%s</date>\n" "        <fromname>%s</fromname>\n" "        <fromemail>%s</fromemail>\n" "        <message-id>%s</message-id>\n" "        <file>\"%s\"</file>\n" "      </mail>\n\n", subj, getdatestr(em->date), from_name, from_emailaddr, em->msgid, msg_relpath(em, subdir_email));

      free(subj);
      free(from_name);
      free(from_emailaddr);
    }
    printhaofitems(fp, hp->right, year, month, subdir_email);
  }
}

/*
** Write the XML based hypermail archive overview file
*/

void writehaof(int amountmsgs, struct emailinfo *email)
{
    int newfile;
    char *filename;
    FILE *fp;

    filename = haofname(email);

    if (isfile(filename))
	newfile = 0;
    else
	newfile = 1;

    if ((fp = fopen(filename, "w")) == NULL) { /* AUDIT biege: where? */
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
        progerr(errmsg);
    }

    if (set_showprogress)
	printf("%s \"%s\"...", lang[MSG_WRITING_HAOF], filename);

#ifdef HAVE_ICONV
	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n");
#else
	fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-15\"?>\n\n");
#endif
	fprintf(fp, "  <!DOCTYPE haof PUBLIC " "\"-//Bernhard Reiter//DTD HOAF 0.2//EN\"\n" "\"http://ffii.org/~breiter/probe/haof-0.2.dtd\">\n\n");
	fprintf(fp, "  <haof version=\"0.2\">\n\n");
	fprintf(fp, "      <archiver name=\"hypermail\" version=\"" VERSION "\" />\n\n");

    print_haof_indices(fp, email ? email->subdir : NULL);
	    
    fprintf(fp, "  <mails>\n");
    printhaofitems(fp, datelist, -1, -1, email);
    fprintf(fp, "  </mails>\n");
    fprintf(fp, "  </haof>\n");

    fclose(fp);

    if (newfile && chmod(filename, set_filemode) == -1) {
	trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename, set_filemode);
	progerr(errmsg);
    }
    free(filename);

    if (set_showprogress)
	putchar('\n');
}




static int count_messages(struct header *hp, int year, int mo, long *first_date, long *last_date)
{
    if (hp != NULL) {
	struct emailinfo *em = hp->data;
	int cnt = count_messages(hp->left, year, mo, first_date, last_date);
	if ((year == -1 || year_of_datenum(em->date) == year)
	    && (mo == -1 || month_of_datenum(em->date) == mo)
	    && !em->is_deleted) {
	    ++cnt;
	    if (em->date < *first_date)
	        *first_date = em->date;
	    if (em->date > *last_date)
	        *last_date = em->date;
	}
	return cnt + count_messages(hp->right, year, mo, first_date, last_date);
    }
    return 0;
}

static void printmonths(FILE *fp, char *summary_filename, int amountmsgs)
{
    int first_year = year_of_datenum(firstdatenum);
    int last_year = year_of_datenum(lastdatenum);
    int y, j, m;
    char *save_name[NO_INDEX];
    char *subject = lang[set_monthly_index ? MSG_MONTHLY_INDEX : MSG_YEARLY_INDEX];

    for (j = 0; j <= AUTHOR_INDEX; ++j)
	save_name[j] = index_name[0][j];
    print_index_header(fp, set_label, set_dir, subject, summary_filename, NULL);
    fprintf(fp, "</header>\n");
    fprintf(fp, "<main class=\"summary-year\">\n");    
    fprintf(fp, "<table>\n");
    fprintf(fp, "<caption>%s</caption>\n", lang[MSG_ACCESS_MAIL_ARCHIVES_BY_CAPTION]);
    fprintf(fp, "<thead>\n");
    fprintf(fp, "<tr>\n");
    fprintf(fp, "<th>%s</th>\n", lang[MSG_PERIOD]);
    fprintf(fp, "<th class=\"cell_period\">%s</th>\n", lang[MSG_ARTICLES]);
    fprintf (fp, "<th colspan=\"4\">%s</th>\n", lang[MSG_SORTED_BY]);
    fprintf(fp, "</tr>\n");
    fprintf(fp, "</thead>\n");
    
    for (y = first_year; y <= last_year; ++y) {
        for (m = (set_monthly_index ? 0 : -1); m < (set_monthly_index ? 12 : 0); ++m) {
	    char month_str[80];
	    char month_str_pub[80];
	    int started_line = 0;
	    int empties = 0;
	    char period_bufs[NO_INDEX][MAXFILELEN];
	    long first_date = lastdatenum;
	    long last_date = firstdatenum;
	    int count;
	    if (!datelist->data)
	        continue;
	    count = count_messages(datelist, y, m, &first_date, &last_date);
	    if (set_monthly_index) {
		sprintf(month_str_pub, "%s %d", months[m], y);
		sprintf(month_str, "%d%.2d", y, m + 1);
	    }
	    else {
		sprintf(month_str_pub, "%d", y);
		sprintf(month_str, "%d", y);
	    }
	    for (j = 0; j <= AUTHOR_INDEX; ++j) {
		sprintf(period_bufs[j], "%sby%s", month_str, save_name[j]);
		index_name[0][j] = period_bufs[j];
	    }
	    for (j = 0; j <= AUTHOR_INDEX; ++j) {
		char *filename;
		char buf1[MAXFILELEN];
		FILE *fp1;
		char *prev_text = "";
		char subject_title[128];
		if (!show_index[0][j])
		    continue;
		trio_snprintf(buf1, sizeof(buf1), "%sby%s", month_str, save_name[j]);
		filename = htmlfilename(buf1, NULL, "");
		fp1 = fopen(filename, "w");
		if (!fp1) {
	    	    trio_snprintf(errmsg, sizeof(errmsg), "can't open %s", filename);
		    progerr(errmsg);
		}
		trio_snprintf(subject_title, sizeof(subject_title), "%s %s", month_str_pub, indextypename[j]);
		print_index_header(fp1, set_label, set_dir, subject_title, filename, NULL);
		/* 
		 * Print out the index page links 
		 */
		print_index_header_links(fp1, j, first_date, last_date, count, NULL);
                
                fprintf (fp1, "</header>\n");
		if (set_indextable) {
		    fprintf(fp1, "<div class=\"center\">\n<table>\n<tr><td><strong>%s</strong></td><td><strong>%s</strong></td><td><strong> %s</strong></td></tr>\n", lang[j == AUTHOR_INDEX ? MSG_CAUTHOR : MSG_CSUBJECT], lang[j == AUTHOR_INDEX ? MSG_CSUBJECT : MSG_CAUTHOR], lang[MSG_CDATE]);
		}
		else {
		   fprintf (fp1, "<main class=\"messages-list\">\n");
		}
                
		switch (j)
                    {
                    case DATE_INDEX:
                    {
                        char prev_date_str[DATESTRLEN + 40];
                        prev_date_str[0] = '\0';
                        printdates(fp1, datelist, y, m, NULL, prev_date_str);
                        if (!set_indextable) {
                            if (*prev_date_str)  /* close the previous date item */
                                fprintf (fp1, "</ul>\n");
                        }
                        break;
                    }
                    case THREAD_INDEX:
                        print_all_threads(fp1, y, m, NULL);
                        break;
                    case SUBJECT_INDEX:
                        printsubjects(fp1, subjectlist, &prev_text, y, m, NULL);
                        if (!set_indextable) {
                            fprintf(fp1, "</ul>\n");
                        }
			break;
		    case AUTHOR_INDEX:
		        printauthors(fp1, authorlist, &prev_text, y, m, NULL);
                        if (!set_indextable) {
                            fprintf(fp1, "</ul>\n");
                        }
			break;
                    }
                
		if (set_indextable) {
		    fprintf(fp1, "</table>\n</div>\n");
		}
		else {
		    fprintf(fp1, "</main>\n");
		}

		/* 
		 * Print out archive information links at the bottom 
		 * of the index page
		 */

		print_index_footer_links(fp1, j, last_date, count, NULL);

		printfooter(fp1, ihtmlfooterfile, set_label, set_dir, subject_title, 
			    save_name[j], TRUE);
		fclose(fp1);
		if (!count) {
		    remove(filename);
		    if (started_line)
		        fprintf(fp, "<td></td>");
		    else
			++empties;
		}
		else {
		    if (!started_line) {
			fprintf(fp, "<tr><td class=\"cell_period\">%s</td><td class=\"cell_message\">%d %s</td>", month_str_pub, count, lang[MSG_ARTICLES]);
			while (empties--)
			    fprintf(fp, "<td></td>");
			started_line = 1;
		    }
		    chmod(filename, set_filemode);
		    fprintf(fp, "<td><a href=\"%sby%s\">%s</a></td>", month_str, save_name[j], indextypename[j]);
		}
		free(filename);
	    }
	    if (started_line)
		fprintf(fp, "</tr>\n");
	}
    }
    fprintf(fp, "</table>\n");
    fprintf(fp, "</main>\n");	
    fprintf (fp, "<footer>\n");	
    printfooter(fp, ihtmlfooterfile, set_label, set_dir, subject, summary_filename, TRUE);
    for (j = 0; j <= AUTHOR_INDEX; ++j)
	index_name[0][j] = save_name[j];
}

void init_index_names(void)
{
    indextypename[DATE_INDEX] = lang[MSG_BY_DATE];
    indextypename[THREAD_INDEX] = lang[MSG_BY_THREAD];
    indextypename[SUBJECT_INDEX] = lang[MSG_BY_SUBJECT];
    indextypename[AUTHOR_INDEX] = lang[MSG_BY_AUTHOR];
    indextypename[ATTACHMENT_INDEX] = lang[MSG_ATTACHMENT];
}

void write_summary_indices(int amount_new)
{
	if (set_monthly_index || set_yearly_index) {
		char *filename;
		FILE *fp;
		filename = htmlfilename("summary", NULL, set_htmlsuffix);
		fp = fopen(filename, "w");	/* AUDIT biege: where? */
		if (!fp) {
                    trio_snprintf(errmsg, sizeof(errmsg), "Couldn't write \"%s\".", filename);
                    progerr(errmsg);
		}
		printmonths(fp, filename, amount_new);
		fclose(fp);
		chmod(filename, set_filemode);
		free(filename);
	}
}

void write_toplevel_indices(int amountmsgs)
{
    int i, j, newfile, k;
    int offset = 0;
    bool first = TRUE;
    struct emailsubdir *sd;
    char *subject = lang[MSG_FOLDERS_INDEX];
    static char verbose_period_name[DATESTRLEN*2 + 1];
    static char abbr_period_name[DATESTRLEN*2 + 1];
    char *end_date;
    char *filename;
    char *saved_set_dateformat;
    char *abbr_dateformat = set_describe_folder != NULL ?
					set_describe_folder : "%d %b %Y";
    char *verbose_dateformat = "%A, %e %B %Y";

    char *tmpstr;

    FILE *fp;

    filename = htmlfilename(index_name[0][FOLDERS_INDEX], NULL, "");
    if (isfile(filename)) 
	newfile = 0;
    else
	newfile = 1;

    if (!show_index[0][FOLDERS_INDEX])
	fp = NULL;
    else if ((fp = fopen(filename, "w")) == NULL) {
        trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\".", lang[MSG_COULD_NOT_WRITE], filename);
	progerr(errmsg);
    }
    if (fp) {
        print_index_header(fp, set_label, set_dir, subject, filename, NULL);
        print_index_header_links(fp, FOLDERS_INDEX, firstdatenum, lastdatenum, amountmsgs, NULL);
        fprintf (fp, "</div>\n");
        fprintf(fp, "<table>\n");

        /* find which element of index_name is the default index */
        offset = 0;
        if (set_defaultindex) {
            tmpstr = setindex(INDEXNAME, INDEXNAME, set_htmlsuffix);
            for (j = 0; j <= ATTACHMENT_INDEX; ++j) {
                if (0 == strcmp(tmpstr, index_name[1][j])) {
                    offset = j;
                    break;
                }
            }
        }

        for (i = 0, j = 0; j <= ATTACHMENT_INDEX; ++j) {
            if (show_index[1][j])
                i++;
        }
        /* not sure if the following -- for computing the colspan is correct will work
           with all configurations. */
        if (i > 0)
            i--;
        fprintf(fp, "<thead>\n  <tr>\n"
                "    <th>%s</th>\n    <th colspan=\"%d\">%s</th>\n"
                "    <th align=\"right\" class=\"count\">%s</th>\n"
                "  </tr>\n</thead>\n"
                "<tbody>\n", lang[MSG_PERIOD], i, lang[MSG_RESORTED], 
                lang[MSG_ARTICLES]);
    }
    sd = folders;
    if (set_reverse_folders)
	while (sd->next_subdir)
	    sd = sd->next_subdir;
    saved_set_dateformat = set_dateformat;
    for (; sd != NULL; sd = set_reverse_folders ? sd->prior_subdir : sd->next_subdir) {
	int started_line = 0;
	if (!datelist->data)
	    continue;
	for (j = 0; j <= ATTACHMENT_INDEX; ++j) {
            /* apply offset so the period column's href points to index.html */
	    k = (j + offset) % (ATTACHMENT_INDEX + 1);
	    if (!show_index[1][k])
		continue;
	    set_dateformat = saved_set_dateformat;
	    switch (k) {
		case DATE_INDEX:
		    writedates(sd->count, sd->first_email);
		    break;
	        case THREAD_INDEX:
		    writethreads(sd->count, sd->first_email);
		    break;
	        case SUBJECT_INDEX:
		    writesubjects(sd->count, sd->first_email);
		    break;
		case AUTHOR_INDEX:
		    writeauthors(sd->count, sd->first_email);
		    break;
		case ATTACHMENT_INDEX:
		    writeattachments(sd->count, sd->first_email);
		    break;
  	        default:
		    break;
	    }
	    if (set_writehaof)
	        writehaof(sd->count, sd->first_email);

	    if (!fp)
	        continue;

	    if (!started_line) {
	      time_t first_date = sd->first_email->fromdate;
	      time_t last_date = sd->last_email->fromdate;
	      if (set_use_sender_date) {
		  first_date = sd->first_email->date;
		  last_date = sd->last_email->date;
	      }
	      set_dateformat = verbose_dateformat;
	      strcpy (verbose_period_name, getdatestr (first_date));
	      set_dateformat = abbr_dateformat;
	      strcpy (abbr_period_name, getdatestr (first_date));
	      end_date = getdatestr (last_date);
	      if (strcmp (abbr_period_name, end_date)) {
		strcat (abbr_period_name, lang[MSG_TO]);
		strcat (abbr_period_name, end_date);
		/* do the same thing for the verbose one */
		set_dateformat = verbose_dateformat;
		end_date = getdatestr (last_date);
		strcat (verbose_period_name, lang[MSG_TO]);
		strcat (verbose_period_name, end_date);
	      }
	      fprintf(fp, "  <tr%s>\n    <td scope=\"row\" class=\"period\" align=\"left\">%s",
		      (first) ? " class=\"first\"" : "",
		      (first) ? "<a id=\"first\"></a>" : "");
	      /* only add a link to the index if it is not empty */
	      if (sd->count > 0)
		fprintf (fp, "<a href=\"%s%s\">",
			 sd->subdir, index_name[1][k]);
	      fprintf (fp, "%s", abbr_period_name);
	      if (sd->count > 0)
		fprintf (fp, "</a>");
	      fprintf (fp, "</td>\n");
	      if (first)
		first = FALSE;
	      started_line = 1;
	    } 
	    else {
	      fprintf(fp, "    <td>");
	      /* only add a link to the index if it is not empty */
	      if (sd->count > 0)
		fprintf (fp, "<a href=\"%s%s\">",
			 sd->subdir, index_name[1][k]);
	      fprintf (fp, "%s", indextypename[k]);
	      if (sd->count > 0)
		fprintf (fp, "</a>");
	      fprintf (fp, "</td>\n");
	    }
	}
	if (started_line && fp)
	    fprintf(fp, "    <td align=\"right\" class=\"count\">%d</td>\n  </tr>\n", sd->count);
    }
    set_dateformat = saved_set_dateformat;

    if (fp) {
      fprintf(fp, "</tbody>\n</table>\n");
      
      /* 
       * Print out archive information links at the bottom of the index
       */
      print_index_footer_links(fp, FOLDERS_INDEX, lastdatenum, amountmsgs, NULL);
      printfooter(fp, ihtmlfooterfile, set_label, set_dir, subject, filename, TRUE);
      fclose(fp);
      
      if (newfile && chmod(filename, set_filemode) == -1) {
          trio_snprintf(errmsg, sizeof(errmsg), "%s \"%s\": %o.", lang[MSG_CANNOT_CHMOD], filename,
		 set_filemode);
          progerr(errmsg);
	}
    }
    free(filename);
}

/*
** This writes out the message index... a file giving the old msgno
** and the hash string that corresponds to it.
*/
void write_messageindex(int startnum, int maxnum)
{
    int num;
    struct emailinfo *email;

    FILE *fp;
    char *filename;
    char *buf;

    struct body *bp;

    if (set_showprogress)
	printf("%s \"%s\"...    ", lang[MSG_WRITING_ARTICLES], set_dir);

    /* write the intial message and number of messages in the index */
	filename = messageindex_name();
	fp = fopen(filename, "w");
	fprintf(fp, "%.04d %.04d\n", startnum, maxnum - 1);

    /* write the reference to the message filenames */
    num = startnum;
    while (num <  maxnum) {
		if ((bp = hashnumlookup(num, &email)) != NULL) {
	  buf = message_name(email);
	  fprintf(fp, "%.04d %s\n", num, buf);
	}
      num++;
    }
    fclose(fp);
    chmod(filename, set_filemode);
    free(filename);
} /* end write_messageindex () */
