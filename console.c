/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// console.c

#if !defined(WIN32) || defined(__MINGW32__)
# include <unistd.h>
#endif
#include <time.h>
#include "quakedef.h"

int con_linewidth;

float con_cursorspeed = 4;

#define		CON_TEXTSIZE	131072

// total lines in console scrollback
int con_totallines;
// lines up from bottom to display
int con_backscroll;
// where next message will be printed
int con_current;
// offset in current line for next print
int con_x;
char *con_text = 0;

//seconds
cvar_t con_notifytime = {CVAR_SAVE, "con_notifytime","3"};
cvar_t con_notify = {CVAR_SAVE, "con_notify","4"};

#define MAX_NOTIFYLINES 32
// cl.time time the line was generated for transparent notify lines
float con_times[MAX_NOTIFYLINES];

int con_vislines;

#define MAXCMDLINE	256
extern char key_lines[32][MAXCMDLINE];
extern int edit_line;
extern int key_linepos;
extern int key_insert;


qboolean con_initialized;

mempool_t *console_mempool;


/*
==============================================================================

LOGGING

==============================================================================
*/

cvar_t log_file = {0, "log_file",""};
cvar_t log_sync = {0, "log_sync","0"};
char crt_log_file [MAX_OSPATH] = "";
qfile_t* logfile = NULL;

qbyte* logqueue = NULL;
size_t logq_ind = 0;
size_t logq_size = 0;

void Log_ConPrint (const char *msg);

/*
====================
Log_Timestamp
====================
*/
const char* Log_Timestamp (const char *desc)
{
	static char timestamp [128];
	time_t crt_time;
	const struct tm *crt_tm;
	char timestring [64];

	// Build the time stamp (ex: "Wed Jun 30 21:49:08 1993");
	time (&crt_time);
	crt_tm = localtime (&crt_time);
	strftime (timestring, sizeof (timestring), "%a %b %d %H:%M:%S %Y", crt_tm);

	if (desc != NULL)
		snprintf (timestamp, sizeof (timestamp), "====== %s (%s) ======\n", desc, timestring);
	else
		snprintf (timestamp, sizeof (timestamp), "====== %s ======\n", timestring);

	return timestamp;
}


/*
====================
Log_Init
====================
*/
void Log_Init (void)
{
	// Allocate a log queue
	logq_size = 512;
	logqueue = Mem_Alloc (tempmempool, logq_size);
	logq_ind = 0;

	Cvar_RegisterVariable (&log_file);
	Cvar_RegisterVariable (&log_sync);

	// support for the classic Quake option
// COMMANDLINEOPTION: Console: -condebug logs console messages to qconsole.log with sync on (so it keeps every message up to a crash), see also log_file and log_sync
	if (COM_CheckParm ("-condebug") != 0)
	{
		Cvar_SetQuick (&log_file, "qconsole.log");
		Cvar_SetValueQuick (&log_sync, 1);
		unlink (va("%s/qconsole.log", fs_gamedir));
	}
}


/*
====================
Log_Open
====================
*/
void Log_Open (void)
{
	if (logfile != NULL || log_file.string[0] == '\0')
		return;

	logfile = FS_Open (log_file.string, "at", false);
	if (logfile != NULL)
	{
		strlcpy (crt_log_file, log_file.string, sizeof (crt_log_file));
		FS_Print (logfile, Log_Timestamp ("Log started"));
	}
}


/*
====================
Log_Close
====================
*/
void Log_Close (void)
{
	if (logfile == NULL)
		return;

	FS_Print (logfile, Log_Timestamp ("Log stopped"));
	FS_Print (logfile, "\n");
	FS_Close (logfile);

	logfile = NULL;
	crt_log_file[0] = '\0';
}


/*
====================
Log_Start
====================
*/
void Log_Start (void)
{
	Log_Open ();

	// Dump the contents of the log queue into the log file and free it
	if (logqueue != NULL)
	{
		if (logfile != NULL && logq_ind != 0)
			FS_Write (logfile, logqueue, logq_ind);
		Mem_Free (logqueue);
		logqueue = NULL;
		logq_ind = 0;
		logq_size = 0;
	}
}


/*
================
Log_ConPrint
================
*/
void Log_ConPrint (const char *msg)
{
	static qboolean inprogress = false;
	// don't allow feedback loops with memory error reports
	if (inprogress)
		return;
	inprogress = true;
	// Until the host is completely initialized, we maintain a log queue
	// to store the messages, since the log can't be started before
	if (logqueue != NULL)
	{
		size_t remain = logq_size - logq_ind;
		size_t len = strlen (msg);

		// If we need to enlarge the log queue
		if (len > remain)
		{
			unsigned int factor = ((logq_ind + len) / logq_size) + 1;
			qbyte* newqueue;

			logq_size *= factor;
			newqueue = Mem_Alloc (tempmempool, logq_size);
			memcpy (newqueue, logqueue, logq_ind);
			Mem_Free (logqueue);
			logqueue = newqueue;
			remain = logq_size - logq_ind;
		}
		memcpy (&logqueue[logq_ind], msg, len);
		logq_ind += len;

		inprogress = false;
		return;
	}

	// Check if log_file has changed
	if (strcmp (crt_log_file, log_file.string) != 0)
	{
		Log_Close ();
		Log_Open ();
	}

	// If a log file is available
	if (logfile != NULL)
	{
		FS_Print (logfile, msg);
		if (log_sync.integer)
			FS_Flush (logfile);
	}
	inprogress = false;
}


/*
================
Log_Print
================
*/
void Log_Print (const char *logfilename, const char *msg)
{
	qfile_t *file;
	file = FS_Open(logfilename, "at", true);
	if (file)
	{
		FS_Print(file, msg);
		FS_Close(file);
	}
}

/*
================
Log_Printf
================
*/
void Log_Printf (const char *logfilename, const char *fmt, ...)
{
	qfile_t *file;

	file = FS_Open (logfilename, "at", true);
	if (file != NULL)
	{
		va_list argptr;

		va_start (argptr, fmt);
		FS_VPrintf (file, fmt, argptr);
		va_end (argptr);

		FS_Close (file);
	}
}


/*
==============================================================================

CONSOLE

==============================================================================
*/

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void)
{
	// toggle the 'user wants console' bit
	key_consoleactive ^= KEY_CONSOLEACTIVE_USER;
	memset (con_times, 0, sizeof(con_times));
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void)
{
	if (con_text)
		memset (con_text, ' ', CON_TEXTSIZE);
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify (void)
{
	int i;

	for (i=0 ; i<MAX_NOTIFYLINES ; i++)
		con_times[i] = 0;
}


/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void)
{
	key_dest = key_message;
	chat_team = false;
}


/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void)
{
	key_dest = key_message;
	chat_team = true;
}


/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int i, j, width, oldwidth, oldtotallines, numlines, numchars;
	char tbuf[CON_TEXTSIZE];

	width = (vid.conwidth >> 3);

	if (width == con_linewidth)
		return;

	if (width < 1)			// video hasn't been initialized yet
	{
		width = 80;
		con_linewidth = width;
		con_totallines = CON_TEXTSIZE / con_linewidth;
		memset (con_text, ' ', CON_TEXTSIZE);
	}
	else
	{
		oldwidth = con_linewidth;
		con_linewidth = width;
		oldtotallines = con_totallines;
		con_totallines = CON_TEXTSIZE / con_linewidth;
		numlines = oldtotallines;

		if (con_totallines < numlines)
			numlines = con_totallines;

		numchars = oldwidth;

		if (con_linewidth < numchars)
			numchars = con_linewidth;

		memcpy (tbuf, con_text, CON_TEXTSIZE);
		memset (con_text, ' ', CON_TEXTSIZE);

		for (i=0 ; i<numlines ; i++)
		{
			for (j=0 ; j<numchars ; j++)
			{
				con_text[(con_totallines - 1 - i) * con_linewidth + j] =
						tbuf[((con_current - i + oldtotallines) %
							  oldtotallines) * oldwidth + j];
			}
		}

		Con_ClearNotify ();
	}

	con_backscroll = 0;
	con_current = con_totallines - 1;
}

/*
================
Con_Init
================
*/
void Con_Init (void)
{
	console_mempool = Mem_AllocPool("console", 0, NULL);
	con_text = Mem_Alloc(console_mempool, CON_TEXTSIZE);
	memset (con_text, ' ', CON_TEXTSIZE);
	con_linewidth = -1;
	Con_CheckResize ();

	Con_Print("Console initialized.\n");

	// register our cvars
	Cvar_RegisterVariable (&con_notifytime);
	Cvar_RegisterVariable (&con_notify);

	// register our commands
	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	con_initialized = true;
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (void)
{
	if (con_backscroll)
		con_backscroll++;

	con_x = 0;
	con_current++;
	memset (&con_text[(con_current%con_totallines)*con_linewidth], ' ', con_linewidth);
}

/*
================
Con_PrintToHistory

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be displayed
If no console is visible, the notify window will pop up.
================
*/
void Con_PrintToHistory(const char *txt)
{
	int y, c, l, mask;
	static int cr;

	if (txt[0] == 1)
	{
		mask = 128;		// go to colored text
		S_LocalSound ("sound/misc/talk.wav");
	// play talk wav
		txt++;
	}
	else if (txt[0] == 2)
	{
		mask = 128;		// go to colored text
		txt++;
	}
	else
		mask = 0;


	while ( (c = *txt) )
	{
	// count word length
		for (l=0 ; l< con_linewidth ; l++)
			if ( txt[l] <= ' ')
				break;

	// word wrap
		if (l != con_linewidth && (con_x + l > con_linewidth) )
			con_x = 0;

		txt++;

		if (cr)
		{
			con_current--;
			cr = false;
		}


		if (!con_x)
		{
			Con_Linefeed ();
		// mark time for transparent overlay
			if (con_current >= 0)
			{
				if (con_notify.integer < 0)
					Cvar_SetValueQuick(&con_notify, 0);
				if (con_notify.integer > MAX_NOTIFYLINES)
					Cvar_SetValueQuick(&con_notify, MAX_NOTIFYLINES);
				if (con_notify.integer > 0)
					con_times[con_current % con_notify.integer] = cl.time;
			}
		}

		switch (c)
		{
		case '\n':
			con_x = 0;
			break;

		case '\r':
			con_x = 0;
			cr = 1;
			break;

		default:	// display character and advance
			y = con_current % con_totallines;
			con_text[y*con_linewidth+con_x] = c | mask;
			con_x++;
			if (con_x >= con_linewidth)
				con_x = 0;
			break;
		}

	}
}

/* The translation table between the graphical font and plain ASCII  --KB */
static char qfont_table[256] = {
	'\0', '#',  '#',  '#',  '#',  '.',  '#',  '#',
	'#',  9,    10,   '#',  ' ',  13,   '.',  '.',
	'[',  ']',  '0',  '1',  '2',  '3',  '4',  '5',
	'6',  '7',  '8',  '9',  '.',  '<',  '=',  '>',
	' ',  '!',  '"',  '#',  '$',  '%',  '&',  '\'',
	'(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
	'8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
	'@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',
	'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
	'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
	'X',  'Y',  'Z',  '[',  '\\', ']',  '^',  '_',
	'`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',
	'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
	'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
	'x',  'y',  'z',  '{',  '|',  '}',  '~',  '<',

	'<',  '=',  '>',  '#',  '#',  '.',  '#',  '#',
	'#',  '#',  ' ',  '#',  ' ',  '>',  '.',  '.',
	'[',  ']',  '0',  '1',  '2',  '3',  '4',  '5',
	'6',  '7',  '8',  '9',  '.',  '<',  '=',  '>',
	' ',  '!',  '"',  '#',  '$',  '%',  '&',  '\'',
	'(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
	'8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
	'@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',
	'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
	'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
	'X',  'Y',  'Z',  '[',  '\\', ']',  '^',  '_',
	'`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',
	'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
	'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
	'x',  'y',  'z',  '{',  '|',  '}',  '~',  '<'
};

/*
================
Con_Print

Prints to all appropriate console targets, and adds timestamps
================
*/
extern cvar_t timestamps;
extern cvar_t timeformat;
extern qboolean sys_nostdout;
void Con_Print(const char *msg)
{
	static int index = 0;
	static char line[16384];

	for (;*msg;msg++)
	{
		if (index == 0)
		{
			// if this is the beginning of a new line, print timestamp
			char *timestamp = timestamps.integer ? Sys_TimeString(timeformat.string) : "";
			// special color codes for chat messages must always come first
			// for Con_PrintToHistory to work properly
			if (*msg <= 2)
				line[index++] = *msg++;
			// store timestamp
			for (;*timestamp;index++, timestamp++)
				if (index < sizeof(line) - 2)
					line[index] = *timestamp;
		}
		// append the character
		line[index++] = *msg;
		// if this is a newline character, we have a complete line to print
		if (*msg == '\n' || index >= 16000)
		{
			// terminate the line
			line[index] = 0;
			// send to log file
			Log_ConPrint(line);
			// send to scrollable buffer
			if (con_initialized && cls.state != ca_dedicated)
				Con_PrintToHistory(line);
			// send to terminal or dedicated server window
			if (!sys_nostdout)
			{
				unsigned char *p;
				for (p = (unsigned char *) line;*p; p++)
					*p = qfont_table[*p];
				Sys_PrintToTerminal(line);
			}
			// empty the line buffer
			index = 0;
		}
	}
}


// LordHavoc: increased from 4096 to 16384
#define	MAXPRINTMSG	16384

/*
================
Con_Printf

Prints to all appropriate console targets
================
*/
void Con_Printf(const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start(argptr,fmt);
	vsprintf(msg,fmt,argptr);
	va_end(argptr);

	Con_Print(msg);
}

/*
================
Con_DPrint

A Con_Print that only shows up if the "developer" cvar is set
================
*/
void Con_DPrint(const char *msg)
{
	if (!developer.integer)
		return;			// don't confuse non-developers with techie stuff...
	Con_Print(msg);
}

/*
================
Con_DPrintf

A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void Con_DPrintf(const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	if (!developer.integer)
		return;			// don't confuse non-developers with techie stuff...

	va_start(argptr,fmt);
	vsprintf(msg,fmt,argptr);
	va_end(argptr);

	Con_Print(msg);
}


/*
================
Con_SafePrint

Okay to call even when the screen can't be updated
==================
*/
void Con_SafePrint(const char *msg)
{
	Con_Print(msg);
}

/*
==================
Con_SafePrintf

Okay to call even when the screen can't be updated
==================
*/
void Con_SafePrintf(const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start(argptr,fmt);
	vsprintf(msg,fmt,argptr);
	va_end(argptr);

	Con_Print(msg);
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge

Modified by EvilTypeGuy eviltypeguy@qeradiant.com
================
*/
void Con_DrawInput (void)
{
	int		y;
	int		i;
	char editlinecopy[257], *text;

	if (!key_consoleactive)
		return;		// don't draw anything

	text = strcpy(editlinecopy, key_lines[edit_line]);

	// Advanced Console Editing by Radix radix@planetquake.com
	// Added/Modified by EvilTypeGuy eviltypeguy@qeradiant.com
	// use strlen of edit_line instead of key_linepos to allow editing
	// of early characters w/o erasing

	y = strlen(text);

// fill out remainder with spaces
	for (i = y; i < 256; i++)
		text[i] = ' ';

	// add the cursor frame
	if ((int)(realtime*con_cursorspeed) & 1)		// cursor is visible
		text[key_linepos] = 11 + 130 * key_insert;	// either solid or triangle facing right

//	text[key_linepos + 1] = 0;

	// prestep if horizontally scrolling
	if (key_linepos >= con_linewidth)
		text += 1 + key_linepos - con_linewidth;

	// draw it
	DrawQ_String(0, con_vislines - 16, text, con_linewidth, 8, 8, 1, 1, 1, 1, 0);

	// remove cursor
//	key_lines[edit_line][key_linepos] = 0;
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int		x, v;
	char	*text;
	int		i;
	float	time;
	extern char chat_buffer[];
	char	temptext[256];

	if (con_notify.integer < 0)
		Cvar_SetValueQuick(&con_notify, 0);
	if (con_notify.integer > MAX_NOTIFYLINES)
		Cvar_SetValueQuick(&con_notify, MAX_NOTIFYLINES);
	if (gamemode == GAME_TRANSFUSION)
		v = 8;
	else
		v = 0;
	for (i= con_current-con_notify.integer+1 ; i<=con_current ; i++)
	{
		if (i < 0)
			continue;
		time = con_times[i % con_notify.integer];
		if (time == 0)
			continue;
		time = cl.time - time;
		if (time > con_notifytime.value)
			continue;
		text = con_text + (i % con_totallines)*con_linewidth;

		DrawQ_String(0, v, text, con_linewidth, 8, 8, 1, 1, 1, 1, 0);

		v += 8;
	}


	if (key_dest == key_message)
	{
		x = 0;

		// LordHavoc: speedup, and other improvements
		if (chat_team)
			sprintf(temptext, "say_team:%s%c", chat_buffer, (int) 10+((int)(realtime*con_cursorspeed)&1));
		else
			sprintf(temptext, "say:%s%c", chat_buffer, (int) 10+((int)(realtime*con_cursorspeed)&1));
		while (strlen(temptext) >= (size_t) con_linewidth)
		{
			DrawQ_String (0, v, temptext, con_linewidth, 8, 8, 1, 1, 1, 1, 0);
			strcpy(temptext, &temptext[con_linewidth]);
			v += 8;
		}
		if (strlen(temptext) > 0)
		{
			DrawQ_String (0, v, temptext, 0, 8, 8, 1, 1, 1, 1, 0);
			v += 8;
		}
	}
}

/*
================
Con_DrawConsole

Draws the console with the solid background
The typing input line at the bottom should only be drawn if typing is allowed
================
*/
extern char engineversion[40];
void Con_DrawConsole (int lines)
{
	int i, y, rows, j;
	char *text;

	if (lines <= 0)
		return;

// draw the background
	if (scr_conbrightness.value >= 0.01f)
		DrawQ_Pic(0, lines - vid.conheight, "gfx/conback", vid.conwidth, vid.conheight, scr_conbrightness.value, scr_conbrightness.value, scr_conbrightness.value, scr_conalpha.value, 0);
	else
		DrawQ_Fill(0, lines - vid.conheight, vid.conwidth, vid.conheight, 0, 0, 0, scr_conalpha.value, 0);
	DrawQ_String(vid.conwidth - strlen(engineversion) * 8 - 8, lines - 8, engineversion, 0, 8, 8, 1, 0, 0, 1, 0);

// draw the text
	con_vislines = lines;

	rows = (lines-16)>>3;		// rows of text to draw
	y = lines - 16 - (rows<<3);	// may start slightly negative

	for (i = con_current - rows + 1;i <= con_current;i++, y += 8)
	{
		j = max(i - con_backscroll, 0);
		text = con_text + (j % con_totallines)*con_linewidth;

		DrawQ_String(0, y, text, con_linewidth, 8, 8, 1, 1, 1, 1, 0);
	}

// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();
}

/*
	Con_DisplayList

	New function for tab-completion system
	Added by EvilTypeGuy
	MEGA Thanks to Taniwha

*/
void Con_DisplayList(const char **list)
{
	int i = 0, pos = 0, len = 0, maxlen = 0, width = (con_linewidth - 4);
	const char **walk = list;

	while (*walk) {
		len = strlen(*walk);
		if (len > maxlen)
			maxlen = len;
		walk++;
	}
	maxlen += 1;

	while (*list) {
		len = strlen(*list);
		if (pos + maxlen >= width) {
			Con_Print("\n");
			pos = 0;
		}

		Con_Print(*list);
		for (i = 0; i < (maxlen - len); i++)
			Con_Print(" ");

		pos += maxlen;
		list++;
	}

	if (pos)
		Con_Print("\n\n");
}

/*
	Con_CompleteCommandLine

	New function for tab-completion system
	Added by EvilTypeGuy
	Thanks to Fett erich@heintz.com
	Thanks to taniwha

*/
void Con_CompleteCommandLine (void)
{
	const char *cmd = "", *s;
	const char **list[3] = {0, 0, 0};
	int c, v, a, i, cmd_len;

	s = key_lines[edit_line] + 1;
	// Count number of possible matches
	c = Cmd_CompleteCountPossible(s);
	v = Cvar_CompleteCountPossible(s);
	a = Cmd_CompleteAliasCountPossible(s);

	if (!(c + v + a))	// No possible matches
		return;

	if (c + v + a == 1) {
		if (c)
			list[0] = Cmd_CompleteBuildList(s);
		else if (v)
			list[0] = Cvar_CompleteBuildList(s);
		else
			list[0] = Cmd_CompleteAliasBuildList(s);
		cmd = *list[0];
		cmd_len = strlen (cmd);
	} else {
		if (c)
			cmd = *(list[0] = Cmd_CompleteBuildList(s));
		if (v)
			cmd = *(list[1] = Cvar_CompleteBuildList(s));
		if (a)
			cmd = *(list[2] = Cmd_CompleteAliasBuildList(s));

		cmd_len = strlen (s);
		do {
			for (i = 0; i < 3; i++) {
				char ch = cmd[cmd_len];
				const char **l = list[i];
				if (l) {
					while (*l && (*l)[cmd_len] == ch)
						l++;
					if (*l)
						break;
				}
			}
			if (i == 3)
				cmd_len++;
		} while (i == 3);
		// 'quakebar'
		Con_Print("\n\35");
		for (i = 0; i < con_linewidth - 4; i++)
			Con_Print("\36");
		Con_Print("\37\n");

		// Print Possible Commands
		if (c) {
			Con_Printf("%i possible command%s\n", c, (c > 1) ? "s: " : ":");
			Con_DisplayList(list[0]);
		}

		if (v) {
			Con_Printf("%i possible variable%s\n", v, (v > 1) ? "s: " : ":");
			Con_DisplayList(list[1]);
		}

		if (a) {
			Con_Printf("%i possible aliases%s\n", a, (a > 1) ? "s: " : ":");
			Con_DisplayList(list[2]);
		}
	}

	if (cmd) {
		strncpy(key_lines[edit_line] + 1, cmd, cmd_len);
		key_linepos = cmd_len + 1;
		if (c + v + a == 1) {
			key_lines[edit_line][key_linepos] = ' ';
			key_linepos++;
		}
		key_lines[edit_line][key_linepos] = 0;
	}
	for (i = 0; i < 3; i++)
		if (list[i])
			Mem_Free((void *)list[i]);
}

