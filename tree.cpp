/****************************************************************************

  TREE - Graphically displays the directory structure of a drive or path

  Written to work with FreeDOS (and other DOS variants)
  Win32(c) console and DOS with LFN support.

****************************************************************************/

#define VERSION "1.03"

/****************************************************************************

  Written by: Kenneth J. Davis
  Date:       August, 2000
  Updated:    September, 2000; October, 2000; November, 2000; January, 2001;
              May, 2004
  Contact:    jeremyd@computer.org


Copyright (c): Public Domain [United States Definition]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR AUTHORS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

****************************************************************************/

/**
 * Define the appropriate target here or within your compiler 
 */
/* #define WIN32 */    /** Win32 console version **/
/* #define DOS */      /** DOS version           **/
/* #define UNIX */

/** 
 * Used to determine whether catgets (LGPL under Win32 & DOS) is used or not.
 * Undefine, ie comment out, to use hard coded strings only.
 */
/* #define USE_CATGETS */


/* Include files */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <ctype.h>
#include <limits.h>

#include "stack.h"


/* Platform (OS) specific definitions */

#ifdef WIN32       /* windows specific     */

#ifndef _WIN32_WINNT         /* NT 4.0 or higher features */
#define _WIN32_WINNT 0x0400
#endif

#include <windows.h>
#include <winbase.h>

/* converts from Ansi to OEM (IBM extended )    */
#define charToDisplayChar(x) CharToOemA((x), (x))
/* For wide character conversion, charToDisplayChar
   must be a function that provides its own buffer
   to CharToOemW and copies the results back into x
   as CharToOemA is inplace safe, but CharToOemW is not.
*/

/* These are defined in w32fDOS.h for DOS and enable /
   disable use of DOS extended int21h API for LFNs,
   on Windows we use it to force using short name when
   both a long and short name are available 
   (note, LFNs may still be shown when no SFN exists)
*/
#define LFN_ENABLE 1
#define LFN_DISABLE 0
int LFN_Enable_Flag = LFN_ENABLE;

/* DOS compiler treats L"" as a char * */
#define UDOT L"."
#define UDOTDOT L".."

#else                   /* DOS specific         */
/* Win32 File compability stuff */
#include "w32fDOS.h"
#include "wincon.h"

/* currently no mapping required */
#define charToDisplayChar(x)

/* DOS compiler treats L"" as a char * */
const WORD UDOT[]    = { 0x2E, 0x00 };        //   L"."
const WORD UDOTDOT[] = { 0x2E, 0x2E, 0x00 };  //   L".."

#endif                  


/* Define getdrive so it returns current drive, 0=A,1=B,...           */
#if defined _MSC_VER || defined __MSC /* MS Visual C/C++ 5 */
#define getdrive() (_getdrive() - 1)
#else /* #ifdef __BORLANDC__ || __TURBOC__ */
#define getdrive() getdisk()
#endif

#include <conio.h>  /* for getch()   */


/* include support for message files */
#ifdef USE_CATGETS
#include "catgets.h"
#endif /* USE_CATGETS */

/* End Platform (OS) specific sections */


/* The default extended forms of the lines used. */
#define VERTBAR_STR  "\xB3   "                 /* |    */
#define TBAR_HORZBAR_STR "\xC3\xC4\xC4\xC4"    /* +--- */
#define CBAR_HORZBAR_STR "\xC0\xC4\xC4\xC4"    /* \--- */

/* Unicode forms of the lines used. */
const char *UMARKER = "\xEF\xBB\xBF";   /* 0xFEFF, Indicate UTF-8 Unicode */
#define UVERTBAR_STR  "\xE2\x94\x82   "                                         /* |    */
#define UTBAR_HORZBAR_STR "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"    /* +--- */
#define UCBAR_HORZBAR_STR "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"    /* \--- */
/*
const unichar UVERTBAR_STR[]      = { 0x2502, 0x20, 0x20, 0x20 };       
const unichar UTBAR_HORZBAR_STR[] = { 0x251C, 0x2500, 0x2500, 0x2500 };
const unichar UCBAR_HORZBAR_STR[] = { 0x2514, 0x2500, 0x2500, 0x2500 };
*/


/* Global flags */
#define SHOWFILESON    1  /* Display names of files in directories       */
#define SHOWFILESOFF   0  /* Don't display names of files in directories */

#define UNICODECHARS   2  /* Use Unicode (UTF8) characters               */
#define ASCIICHARS     1  /* Use ASCII [7bit] characters                 */
#define EXTENDEDCHARS  0  /* Use extended ASCII [8bit] characters        */

#define NOPAUSE        0  /* Default, don't pause after screenfull       */
#define PAUSE          1  /* Wait for keypress after each page           */


/* Global variables */
short showFiles = SHOWFILESOFF;
short charSet = EXTENDEDCHARS;
short pause = NOPAUSE;

short dspAll = 0;  /* if nonzero includes HIDDEN & SYSTEM files in output */
short dspSize = 0; /* if nonzero displays filesizes                       */
short dspAttr = 0; /* if nonzero displays file attributes [DACESHRBP]     */
short dspSumDirs = 0; /* show count of subdirectories  (per dir and total)*/


/* maintains total count, for > 4billion dirs, use a __int64 */
unsigned long totalSubDirCnt = 0;


/* text window size, used to determine when to pause,
   Note: rows is total rows available - 2
   1 is for pause message and then one to prevent auto scroll up 
*/
short cols=80, rows=23;   /* determined these on startup (when possible)  */


#ifdef USE_CATGETS
  char *catsFile = "tree";   /* filename of our message catalog           */
#endif /* USE_CATGETS */


/* Global constants */
#define SERIALLEN 16      /* Defines max size of volume & serial number   */
#define VOLLEN 128

#define MAXBUF 1024       /* Must be larger than max file path length     */
char path[MAXBUF];        /* Path to begin search from, default=current   */

#define MAXPADLEN (MAXBUF*2) /* Must be large enough to hold the maximum padding */
/* (MAXBUF/2)*4 == (max path len / min 2chars dirs "?\") * 4chars per padding    */

/* The maximum size any line of text output can be, including room for '\0'*/
#define MAXLINE 160        /* Increased to fit two lines for translations  */


/* The hard coded strings used by the following show functions.            */

/* common to many functions [Set 1] */
char newLine[MAXLINE] = "\n";

/* showUsage [Set 2] - Each %c will be replaced with proper switch/option */
char treeDescription[MAXLINE] = "Graphically displays the directory structure of a drive or path.\n";
char treeUsage[MAXLINE] =       "TREE [drive:][path] [%c%c] [%c%c]\n";
char treeFOption[MAXLINE] =     "   %c%c   Display the names of the files in each directory.\n";
char treeAOption[MAXLINE] =     "   %c%c   Use ASCII instead of extended characters.\n";

/* showInvalidUsage [Set 3] */
char invalidOption[MAXLINE] = "Invalid switch - %s\n";  /* Must include the %s for option given. */
char useTreeHelp[MAXLINE] =   "Use TREE %c? for usage information.\n"; /* %c replaced with switch */

/* showVersionInfo [Set 4] */
/* also uses treeDescription */
char treeGoal[MAXLINE] =      "Written to work with FreeDOS\n";
char treePlatforms[MAXLINE] = "Win32(c) console and DOS with LFN support.\n";
char version[MAXLINE] =       "Version %s\n"; /* Must include the %s for version string. */
char writtenBy[MAXLINE] =     "Written by: Kenneth J. Davis\n";
char writtenDate[MAXLINE] =   "Date:       2000, 2001, 2004\n";
char contact[MAXLINE] =       "Contact:    jeremyd@computer.org\n";
char copyright[MAXLINE] =     "Copyright (c): Public Domain [United States Definition]\n";
#ifdef USE_CATGETS
char catsCopyright[MAXLINE] = "Uses Jim Hall's <jhall@freedos.org> Cats Library\n  version 3.8 Copyright (C) 1999,2000 Jim Hall\n";
#endif

/* showInvalidDrive [Set 5] */
char invalidDrive[MAXLINE] = "Invalid drive specification\n";

/* showInvalidPath [Set 6] */
char invalidPath[MAXLINE] = "Invalid path - %s\n"; /* Must include %s for the invalid path given. */

/* Misc Error messages [Set 7] */
/* showBufferOverrun */
/* %u required to show what the buffer's current size is. */
char bufferToSmall[MAXLINE] = "Error: File path specified exceeds maximum buffer = %u bytes\n";
/* showOutOfMemory */
/* %s required to display what directory we were processing when ran out of memory. */
char outOfMemory[MAXLINE] = "Out of memory on subdirectory: %s\n";

/* main [Set 1] */
char pathListingNoLabel[MAXLINE] = "Directory PATH listing\n";
char pathListingWithLabel[MAXLINE] = "Directory PATH listing for Volume %s\n"; /* %s for label */
char serialNumber[MAXLINE] = "Volume serial number is %s\n"; /* Must include %s for serial #   */
char noSubDirs[MAXLINE] = "No subdirectories exist\n\n";
char pauseMsg[MAXLINE]  = " --- Press any key to continue ---\n";

/* Option Processing - parseArguments [Set 8]      */
char optionchar1 = '/';  /* Primary character used to determine option follows  */
char optionchar2 = '-';  /* Secondary character used to determine option follows  */
const char OptShowFiles[2] = { 'F', 'f' };  /* Show files */
const char OptUseASCII[2]  = { 'A', 'a' };  /* Use ASCII only */
const char OptUnicode[2]   = { 'U', 'u' };  /* Use Unicode (16bit) output */
const char OptVersion[2]   = { 'V', 'v' };  /* Version information */
const char OptSFNs[2]      = { 'S', 's' };  /* Shortnames only (disable LFN support) */
const char OptPause[2]     = { 'P', 'p' };  /* Pause after each page (screenfull) */
const char OptDisplay[2]   = { 'D', 'd' };  /* modify Display settings */
const char OptSort[2]      = { 'O', 'o' };  /* sort Output */


/* Procedures */

/* Convert src from given codepage to UTF-16, 
 * returns nonzero on success, 0 on any error
 * cp is the codepage of source string, should be either CP_ACP (ansi)
 * or CP_OEM (DOS, e.g. cp437).
 */
#define convertCPtoUTF16(cp, src, dst, dstsize) \
  MultiByteToWideChar(cp, 0/*MB_PRECOMPOSED|MB_USEGLYPHCHARS*/, src, -1, dst, dstsize)

/* Convert from UTF-16 to UTF-8 */
#if 0   // Can use on Win 98+, NT4+, or Win95 with MSLU
        // as Win95 does not support CP_UTF8 conversion
#define convertUTF16toUTF8(src, dst, dstsize) \
        WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstsize, NULL, NULL)

#else
/* Transforms a UTF-16 string to a UTF-8 one, does not support surrogate pairs,
 * see http://en.wikipedia.org/wiki/UTF-8 for conversion chart
 * returns 0 on error, nonzero if conversion completed.
 */
int convertUTF16toUTF8(const WORD *src, char *dst, unsigned dstsize)
{
  if (!dstsize) return 0;
  dstsize--;  // reserve room for terminating '\0'

  const WORD *s = src;
  unsigned char *d = (unsigned char *)dst;
  for (register WORD ch = *s ; ch && dstsize; ch = *s)
  {
    // determine how many bytes this UTF-16 char is in UTF-8 format
    // Note: for values >= 0x10000 (MAXWORD+1)
    // a surrogate is needed and UTF-8 requires 4 bytes
    register unsigned short cnt = (ch < 0x80)? 1 : (ch < 0x800)? 2 : 3;

    // ensure enough room in dst buffer for it, break early if not
    if (dstsize < cnt) break;
    dstsize -= cnt;

    switch(cnt) // write out each byte needed
    {
      case 1:
      {
        *d = (unsigned char)ch;  // 0xxx xxxx == (ch & 0x7F)
        break;
      }
      case 2:
      { // 110x xxxx 10xx xxxx
        *d = (unsigned char)(0xC0 | ((ch >> 6) & 0x1F)); // top 5 bits
        *(d+1) = (unsigned char)(0x80 | (ch & 0x3F));        // lower 6 bits
        break;
      }
      case 3:
      { // 1110 xxxx 10xx xxxx 10xx xxxx
        *d = (unsigned char)(0xE0 | ((ch >> 12) & 0x0F)); // top 4 bits
        *(d+1) = (unsigned char)(0x80 | ((ch >> 6)  & 0x3F)); // mid 6 bits
        *(d+2) = (unsigned char)(0x80 | (ch & 0x3F));         // lower 6 bits
        break;
      }
    }

    // increment source and destination pointers
    s++;
    d+=cnt;
  }
  *d = '\0';  // force result string to be '\0' terminated

  if (*s && dstsize) // not all values converted
    return 0;
  else
    return 1;
}
#endif


/*
 * Converts src string (assumed windows ANSI or OEM cp) to UTF8 string.
 * src and dst may be same, but dst may be truncated if exceeds maxlen
 * cp is the codepage of source string, should be either CP_ACP (ansi)
 * or CP_OEM (DOS, e.g. cp437).
 * returns zero if src or dst is NULL or error in conversion
 * otherwise returns nonzero for success
 * WARNING: mapping may be incorrect under DOS as it ignores cp.
 */
BOOL charToUTF8(unsigned int cp, const char *src, char *dst, int maxlen)
{
  static char buffer[MAXBUF];
  if (src == NULL || dst == NULL || maxlen < 1) return 0;

  /* convert from ANSI/OEM cp to UTF-16 then to UTF-8 */
  WORD ubuf[MAXBUF];

  if (!convertCPtoUTF16(cp, src, ubuf, MAXBUF) ||
      !convertUTF16toUTF8(ubuf, buffer, MAXBUF))
    return 0;

  memcpy(dst, buffer, maxlen);

  return 1;
}


/* sets rows & cols to size of actual console window
 * force NOPAUSE if appears output redirected to a file or
 * piped to another program
 * Uses hard coded defaults and leaves pause flag unchanged
 * if unable to obtain information.
 */
void getConsoleSize(void)
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE)
  {
    switch (GetFileType(h))
    {
      case FILE_TYPE_PIPE:  /* e.g. piped to more, tree | more */
      case FILE_TYPE_DISK:  /* e.g. redirected to a file, tree > filelist.txt */
      {
         /* Output to a file or program, so no screen to fill (no max cols or rows) */
         pause = NOPAUSE;   /* so ignore request to pause */
         break;
      }
      case FILE_TYPE_CHAR:  /* e.g. the console */
      case FILE_TYPE_UNKNOWN:  /* else at least attempt to get proper limits */
      /*case #define FILE_TYPE_REMOTE:*/
      default:
      {
        if (GetConsoleScreenBufferInfo(h, &csbi))
        {
          /* rows = window size - 2, where -2 neccessary to keep screen from scrolling */
          rows = csbi.srWindow.Bottom - csbi.srWindow.Top - 1;
          cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        }
        /* else use hard coded defaults of 80x23 assuming 80x25 sized screen */
        break;
      }
    }
  }
}

/* when pause == NOPAUSE then identical to printf,
   otherwise counts lines printed and pauses as needed.
   Should be used for all messages printed that do not
   immediately exit afterwards (else printf may be used).
   May display N too many lines before pause if line is
   printed that exceeds cols [N=linelen%cols] and lacks
   any newlines (but this should not occur in tree).
*/
#include <stdarg.h>  /* va_list, va_start, va_end */
int pprintf(const char *msg, ...)
{
  static int lineCnt = rows;
  static int lineCol = 0;

  va_list argptr;
  int cnt;
  char buffer[MAXBUF];

  va_start(argptr, msg);
  cnt = vsprintf(buffer, msg, argptr);
  va_end(argptr);

  if (pause == PAUSE)
  {
    char * l = buffer;
    /* cycle through counting newlines and lines > cols */
    for (register char *t = strchr(l, '\n'); t != NULL; t = strchr(l, '\n'))
    {
      t++;             /* point to character after newline */
      char c = *t;     /* store current value */
      *t = '\0';       /* mark as end of string */

      /* print all but last line of a string that wraps across rows */
      /* adjusting for partial lines printed without the newlines   */
      while (strlen(l)+lineCol > cols)
      {
        char c = l[cols-lineCol];
        l[cols-lineCol] = '\0';
        printf("%s", l);
        l[cols-lineCol] = c;
        l += cols-lineCol;

        lineCnt--;  lineCol = 0;
        if (!lineCnt) { lineCnt= rows;  fflush(NULL);  fprintf(stderr, "%s", pauseMsg);  getch(); }
      }

      printf("%s", l); /* print out this line */
      *t = c;          /* restore value */
      l = t;           /* mark beginning of next line */

      lineCnt--;  lineCol = 0;
      if (!lineCnt) { lineCnt= rows;  fflush(NULL);  fprintf(stderr, "%s", pauseMsg);  getch(); }
    }
    printf("%s", l);   /* print rest of string that lacks newline */
    lineCol = strlen(l);
  }
  else  /* NOPAUSE */
    printf("%s", buffer);

  return cnt;
}


/* Displays to user valid options then exits program indicating no error */
void showUsage(void)
{
  printf("%s%s%s%s", treeDescription, newLine, treeUsage, newLine);
  printf("%s%s%s", treeFOption, treeAOption, newLine);
  exit(1);
}


/* Displays error message then exits indicating error */
void showInvalidUsage(char * badOption)
{
  printf(invalidOption, badOption);
  printf("%s%s", useTreeHelp, newLine);
  exit(1);
}


/* Displays author, copyright, etc info, then exits indicating no error. */
void showVersionInfo(void)
{
  printf("%s%s%s%s%s", treeDescription, newLine, treeGoal, treePlatforms, newLine);
  printf(version, VERSION);
  printf("%s%s%s%s%s", writtenBy, writtenDate, contact, newLine, newLine);
  printf("%s%s", copyright, newLine);
#ifdef USE_CATGETS
  printf("%s%s", catsCopyright, newLine);
#endif
  exit(1);
}


/* Displays error messge for invalid drives and exits */
void showInvalidDrive(void)
{
  printf(invalidDrive);
  exit(1);
}


/* Takes a fullpath, splits into drive (C:, or \\server\share) and path */
void splitpath(char *fullpath, char *drive, char *path);

/**
 * Takes a given path, strips any \ or / that may appear on the end.
 * Returns a pointer to its static buffer containing path
 * without trailing slash and any necessary display conversions.
 */
char *fixPathForDisplay(char *path);

/* Displays error message for invalid path; Does NOT exit */
void showInvalidPath(char *path)
{
  char partialPath[MAXBUF], dummy[MAXBUF];

  pprintf("%s\n", path);
  splitpath(path, dummy, partialPath);
  pprintf(invalidPath, fixPathForDisplay(partialPath));
}

/* Displays error message for out of memory; Does NOT exit */
void showOutOfMemory(char *path)
{
  pprintf(outOfMemory, path);
}

/* Displays buffer exceeded message and exits */
void showBufferOverrun(WORD maxSize)
{
  printf(bufferToSmall, maxSize);
  exit(1);
}


/**
 * Takes a fullpath, splits into drive (C:, or \\server\share) and path
 * It assumes a colon as the 2nd character means drive specified,
 * a double slash \\ (\\, //, \/, or /\) specifies network share.
 * If neither drive nor network share, then assumes whole fullpath
 * is path, and sets drive to "".
 * If drive specified, then set drive to it and colon, eg "C:", with
 * the rest of fullpath being set in path.
 * If network share, the slash slash followed by the server name,
 * another slash and either the rest of fullpath or up to, but not
 * including, the next slash are placed in drive, eg "\\KJD\myshare";
 * the rest of the fullpath including the slash are placed in
 * path, eg "\mysubdir"; where fullpath is "\\KJD\myshare\mysubdir".
 * None of these may be NULL, and drive and path must be large
 * enough to hold fullpath.
 */
void splitpath(char *fullpath, char *drive, char *path)
{
  register char *src = fullpath;
  register char oldchar;

  /* If either network share or path only starting at root directory */
  if ( (*src == '\\') || (*src == '/') )
  {
    src++;

    if ( (*src == '\\') || (*src == '/') ) /* network share */
    {
      src++;

      /* skip past server name */
      while ( (*src != '\\') && (*src != '/') && (*src != '\0') )
        src++;

      /* skip past slash (\ or /) separating  server from share */
      if (*src != '\0') src++;

      /* skip past share name */
      while ( (*src != '\\') && (*src != '/') && (*src != '\0') )
        src++;

      /* src points to start of path, either a slash or '\0' */
      oldchar = *src;
      *src = '\0';

      /* copy server name to drive */
      strcpy(drive, fullpath);

      /* restore character used to mark end of server name */
      *src = oldchar;

      /* copy path */
      strcpy(path, src);
    }
    else /* path only starting at root directory */
    {
      /* no drive, so set path to same as fullpath */
      strcpy(drive, "");
      strcpy(path, fullpath);
    }
  }
  else
  {
    if (*src != '\0') src++;

    /* Either drive and path or path only */
    if (*src == ':')
    {
      /* copy drive specified */
      *drive = *fullpath;  drive++;
      *drive = ':';        drive++;
      *drive = '\0';

      /* copy path */
      src++;
      strcpy(path, src);
    }
    else
    {
      /* no drive, so set path to same as fullpath */
      strcpy(drive, "");
      strcpy(path, fullpath);
    }
  }
}


/* Converts given path to full path */
void getProperPath(char *fullpath)
{
  char drive[MAXBUF];
  char path[MAXBUF];

  splitpath(fullpath, drive, path);

  /* if no drive specified use current */
  if (drive[0] == '\0')
  {
    sprintf(fullpath, "%c:%s", 'A'+ getdrive(), path);
  }
  else if (path[0] == '\0') /* else if drive but no path specified */
  {
    if ((drive[0] == '\\') || (drive[0] == '/'))
    {
      /* if no path specified and network share, use root   */
      sprintf(fullpath, "%s%s", drive, "\\");
    }
    else
    {
      /* if no path specified and drive letter, use current path */
      sprintf(fullpath, "%s%s", drive, ".");
    }
  }
  /* else leave alone, it has both a drive and path specified */
}


/* Parses the command line and sets global variables. */
void parseArguments(int argc, char *argv[])
{
  register int i;     /* temp loop variable */

  /* if no drive specified on command line, use current */
  sprintf(path, "%c:.", 'A'+ getdrive());

  for (i = 1; i < argc; i++)
  {
    /* Check if user is giving an option or drive/path */
    if ((argv[i][0] == optionchar1) || (argv[i][0] == optionchar2) )
    {
      /* check multi character options 1st */
      if ((argv[i][1] == OptDisplay[0]) || (argv[i][1] == OptDisplay[1]))
      {
        switch (argv[i][2] & 0xDF)
        {
          case 'A' :       /*  /DA  display attributes */
            dspAttr = 1;
            break;
          case 'F' :       /*  /DF  display filesizes  */
            dspSize = 1;
            break;
          case 'H' :       /*  /DH  display hidden & system files (normally not shown) */
            dspAll = 1;
            break;
          case 'R' :       /*  /DR  display results at end */
            dspSumDirs = 1;
            break;
          default:
            showInvalidUsage(argv[i]);
        }
      }
      else if ((argv[i][1] == OptSort[0]) || (argv[i][1] == OptSort[1]))
      {
#if 1  // not yet supported
        showInvalidUsage(argv[i]);
#else
        int reverse = (argv[i][3] == '-')?1:0;  /* invert sort if suffixed with - */
        switch (argv[i][2] & 0xDF)
        {
          case 'F' :       /*  /Of  sort by Filesize   */
            break;
          case 'N' :       /*  /On  sort by fileName   */
            break;
          case 'E' :       /*  /Oe  sort by Extension  */
            break;
          default:
            showInvalidUsage(argv[i]);
        }
#endif
      }
      else /* a 1 character option (or invalid) */
      {
        if (argv[i][2] != '\0')
          showInvalidUsage(argv[i]);

        /* Must check both uppercase and lowercase                        */
        if ((argv[i][1] == OptShowFiles[0]) || (argv[i][1] == OptShowFiles[1]))
          showFiles = SHOWFILESON; /* set file display flag appropriately */
        else if ((argv[i][1] == OptUseASCII[0]) || (argv[i][1] == OptUseASCII[1]))
          charSet = ASCIICHARS;    /* set charset flag appropriately      */
        else if (argv[i][1] == '?')
          showUsage();             /* show usage info and exit            */
        else if ((argv[i][1] == OptVersion[0]) || (argv[i][1] == OptVersion[1]))
          showVersionInfo();       /* show version info and exit          */
        else if ((argv[i][1] == OptSFNs[0]) || (argv[i][1] == OptSFNs[1]))
          LFN_Enable_Flag = LFN_DISABLE;         /* force shortnames only */
        else if ((argv[i][1] == OptPause[0]) || (argv[i][1] == OptPause[1]))
          pause = PAUSE;     /* wait for keypress after each page (pause) */
        else if ((argv[i][1] == OptUnicode[0]) || (argv[i][1] == OptUnicode[1]))
          charSet = UNICODECHARS;   /* indicate output Unicode text */
        else /* Invalid or unknown option */
          showInvalidUsage(argv[i]);
      }
    }
    else /* should be a drive/path */
    {
      if (strlen(argv[i]) > MAXBUF)
        showBufferOverrun(MAXBUF);

      /* copy path over, making all caps to look prettier, can be strcpy */
      register char *dptr = path;
      for (register char *cptr = argv[i]; *cptr != '\0'; cptr++, dptr++)
        *dptr = toupper(*cptr);
      *dptr = '\0';

      /* Converts given path to full path */
      getProperPath(path);
    }
  }
}


/**
 * Fills in the serial and volume variables with the serial #
 * and volume found using path.
 * If there is an error getting the volume & serial#, then an 
 * error message is displayed and the program exits.
 * Volume and/or serial # returned may be blank if the path specified
 * does not contain them, or an error retrieving 
 * (ie UNC paths under DOS), but path is valid.
 */
void GetVolumeAndSerial(char *volume, char *serial, char *path)
{
  char rootPath[MAXBUF];
  char dummy[MAXBUF];
  union serialNumber {
    DWORD serialFull;
    struct {
      WORD a;
      WORD b;
    } serialParts;
  } serialNum;

  /* get drive letter or share server\name */
  splitpath(path, rootPath, dummy);
  strcat(rootPath, "\\");

  if (GetVolumeInformation(rootPath, volume, VOLLEN,
      &serialNum.serialFull, NULL, NULL, NULL, 0) == 0)
	showInvalidDrive();

  if (serialNum.serialFull == 0)
    serial[0] = '\0';
  else
    sprintf(serial, "%04X:%04X",
      serialNum.serialParts.b, serialNum.serialParts.a);
}


/* FindFile stuff to support optional NT & Unicode API variants */
typedef union WIN32_FIND_DATA_BOTH
{
 WIN32_FIND_DATAW ud;
 WIN32_FIND_DATAA ad;
} WIN32_FIND_DATA_BOTH;

#ifndef STDCALL
#define STDCALL __stdcall
#endif

typedef HANDLE ( STDCALL * fFindFirstFileExA)(const char *, FINDEX_INFO_LEVELS, void *, FINDEX_SEARCH_OPS, void *, DWORD);
typedef HANDLE ( STDCALL * fFindFirstFileExW)(const WORD *, FINDEX_INFO_LEVELS, void *, FINDEX_SEARCH_OPS, void *, DWORD);
typedef BOOL ( STDCALL * fFindNextFileW)(HANDLE, WIN32_FIND_DATAW *);

/* FindFirstFileExA is only available on NT systems, so on Win9x & DOS use plain FindFirstFile */
HANDLE STDCALL myFindFirstFileExA(const char *fname, FINDEX_INFO_LEVELS, void * ffd, FINDEX_SEARCH_OPS, void *, DWORD)
{
  return FindFirstFileA(fname, (WIN32_FIND_DATAA *)ffd);
}

fFindFirstFileExA pFindFirstFileExA = &myFindFirstFileExA;
fFindFirstFileExW pFindFirstFileExW = NULL;  // &FindFirstFileExW

/**
 * Stores directory information obtained from FindFirst/Next that
 * we may wish to make use of when displaying directory entry.
 * e.g. attribute, dates, etc.
 */
typedef struct DIRDATA
{
  DWORD subdirCnt;          /* how many subdirectories we have */
  DWORD fileCnt;            /* how many [normal] files we have */
  DWORD dwDirAttributes;    /* Directory attributes            */
} DIRDATA;

/**
 * Contains the information stored in a Stack necessary to allow
 * non-recursive function to display directory tree.
 */
typedef struct SUBDIRINFO
{
  struct SUBDIRINFO * parent; /* points to parent subdirectory                */
  char *currentpath;    /* Stores the full path this structure represents     */
  char *subdir;         /* points to last subdir within currentpath           */
  char *dsubdir;        /* Stores a display ready directory name              */
  long subdircnt;       /* Initially a count of how many subdirs in this dir  */
  HANDLE findnexthnd;   /* The handle returned by findfirst, used in findnext */
  struct DIRDATA ddata; /* Maintain directory information, eg attributes      */
} SUBDIRINFO;


/**
 * Returns 0 if no subdirectories, count if has subdirs.
 * Path must end in slash \ or /
 * On error (invalid path) displays message and returns -1L.
 * Stores additional directory data in ddata if non-NULL
 * and path is valid.
 */
long hasSubdirectories(char *path, DIRDATA *ddata = NULL)
{
  static WIN32_FIND_DATA findData;
  HANDLE hnd;
  static char buffer[MAXBUF];
  int hasSubdirs = 0;

  /* get the handle to start with (using wildcard spec) */
  strcpy(buffer, path);
  strcat(buffer, "*");

  /* Use FindFirstFileEx when available (falls back to FindFirstFile).
   * Allows us to limit returned results to just directories
   * if supported by underlying filesystem.
   */
  hnd = pFindFirstFileExA(buffer, FindExInfoStandard, &findData, FindExSearchLimitToDirectories, NULL, 0);
  if (hnd == INVALID_HANDLE_VALUE)
  {
    showInvalidPath(path); /* Display error message */
    return -1L;
  }


  /*  cycle through entries counting directories found until no more entries */
  do {
    if (((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) &&
	((findData.dwFileAttributes &
	 (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0 || dspAll) )
    {
      if ( (strcmp(findData.cFileName, ".") != 0) && /* ignore initial [current] path */
           (strcmp(findData.cFileName, "..") != 0) ) /* and ignore parent path */
        hasSubdirs++;      /* subdir of initial path found, so increment counter */
    }
  } while(FindNextFile(hnd, &findData) != 0);

  /* prevent resource leaks, close the handle. */
  FindClose(hnd);

  if (ddata != NULL)  // don't bother if user doesn't want them
  {
    /* The root directory of a volume (including non root paths
       corresponding to mount points) may not have a current (.) and
       parent (..) entry.  So we can't get attributes for initial
       path in above loop from the FindFile call as it may not show up 
       (no . entry).  So instead we explicitly get them here.
    */
    if ((ddata->dwDirAttributes = GetFileAttributes(path)) == (DWORD)-1)
    {
      //printf("ERROR: unable to get file attr, %i\n", GetLastError());
      ddata->dwDirAttributes = 0;
    }

    /* a curiosity, for showing sum of directories process */
    ddata->subdirCnt = hasSubdirs;
  }
  totalSubDirCnt += hasSubdirs;

  return hasSubdirs;
}


/**
 * Allocates memory and stores the necessary stuff to allow us to
 * come back to this subdirectory after handling its subdirectories.
 * parentpath must end in \ or / or be NULL, however
 * parent should only be NULL for initialpath
 * if subdir does not end in slash, one is added to stored subdir
 * dsubdir is subdir already modified so ready to display to user
 */
SUBDIRINFO *newSubdirInfo(SUBDIRINFO *parent, char *subdir, char *dsubdir)
{
  register int parentLen, subdirLen;

  /* Get length of parent directory */
  if (parent == NULL)
    parentLen = 0;
  else
    parentLen = strlen(parent->currentpath);

  /* Get length of subdir, add 1 if does not end in slash */
  subdirLen = strlen(subdir);
  if ((subdirLen < 1) || ( (*(subdir+subdirLen-1) != '\\') && (*(subdir+subdirLen-1) != '/') ) )
    subdirLen++;

  SUBDIRINFO *temp = (SUBDIRINFO *)malloc(sizeof(SUBDIRINFO));
  if (temp == NULL) 
  {
    showOutOfMemory(subdir);
    return NULL;
  }
  if ( ((temp->currentpath = (char *)malloc(parentLen+subdirLen+1)) == NULL) ||
       ((temp->dsubdir = (char *)malloc(strlen(dsubdir)+1)) == NULL) )
  {
    showOutOfMemory(subdir);
    free(temp);
    if (temp->currentpath != NULL) free(temp->currentpath);
    return NULL;
  }
  temp->parent = parent;
  if (parent == NULL)
    strcpy(temp->currentpath, "");
  else
    strcpy(temp->currentpath, parent->currentpath);
  strcat(temp->currentpath, subdir);
  /* if subdir[subdirLen-1] == '\0' then we must append a slash */
  if (*(subdir+subdirLen-1) == '\0')
    strcat(temp->currentpath, "\\");
  temp->subdir = temp->currentpath+parentLen;
  strcpy(temp->dsubdir, dsubdir);
  if ((temp->subdircnt = hasSubdirectories(temp->currentpath, &(temp->ddata))) == -1L)
  {
    free (temp->currentpath);
    free (temp->dsubdir);
    free(temp);
    return NULL;
  }
  temp->findnexthnd = INVALID_HANDLE_VALUE;

  return temp;
}

/**
 * Extends the padding with the necessary 4 characters.
 * Returns the pointer to the padding.
 * padding should be large enough to hold the additional 
 * characters and '\0', moreSubdirsFollow specifies if
 * this is the last subdirectory in a given directory
 * or if more follow (hence if a | is needed).
 * padding must not be NULL
 * Warning: if charSet == UNICODECHARS, then padding
 *          will be a fixed 6 bytes (instead of 4)
 *          as the leading | is encoded in 3 bytes.
 */
char * addPadding(char *padding, int moreSubdirsFollow)
{
    if (moreSubdirsFollow)
    {
      /* 1st char is | or a vertical bar */
      if (charSet == EXTENDEDCHARS)
        strcat(padding, VERTBAR_STR);
      else if (charSet == UNICODECHARS)
        strcat(padding, UVERTBAR_STR);
      else
        strcat(padding, "|   ");
    }
    else
      strcat(padding, "    ");

    return padding;
}

/**
 * Removes the last padding added (last 4 characters added,
 * or last 6 characters added if charSet == UNICODECHARS).
 * Does nothing if less than 4|6 characters in string.
 * padding must not be NULL
 * Returns the pointer to padding.
 */
char * removePadding(char *padding)
{
  register size_t len = strlen(padding);

  if (charSet == UNICODECHARS && padding[len-4] != ' ')
  {
    if (len < 6) return padding;

    *(padding + len - 6) = '\0';
  }
  else
  {
    if (len < 4) return padding;

    *(padding + len - 4) = '\0';
  }

  return padding;
}

/**
 * Takes a given path, strips any \ or / that may appear on the end.
 * Returns a pointer to its static buffer containing path
 * without trailing slash and any necessary display conversions.
 */
char *fixPathForDisplay(char *path)
{
  static char buffer[MAXBUF];
  register int pathlen;

  strcpy(buffer, path);
  pathlen = strlen(buffer);
  if (pathlen > 1)
  {
    pathlen--;
    if ((buffer[pathlen] == '\\') || (buffer[pathlen] == '/'))
      buffer[pathlen] = '\0'; // strip off trailing slash on end
  }

  if (charSet == UNICODECHARS)
    charToUTF8((LFN_Enable_Flag == LFN_DISABLE)?CP_OEMCP:CP_ACP, buffer, buffer, MAXBUF);
  else
    charToDisplayChar(buffer);

  return buffer;
}

/**
 * Displays the current path, with necessary padding before it.
 * A \ or / on end of currentpath is not shown.
 * moreSubdirsFollow should be nonzero if this is not the last
 * subdirectory to be displayed in current directory, else 0.
 * Also displays additional information, such as attributes or
 * sum of size of included files.
 * currentpath is an ASCIIZ string of path to display
 *             assumed to be a displayable path (ie. OEM or UTF-8)
 * padding is an ASCIIZ string to display prior to entry.
 * moreSubdirsFollow is -1 for initial path else >= 0.
 */
void showCurrentPath(char *currentpath, char *padding, int moreSubdirsFollow, DIRDATA *ddata)
{
  if (padding != NULL)
    pprintf("%s", padding);

  /* print lead padding except for initial directory */
  if (moreSubdirsFollow >= 0)
  {
    if (charSet == EXTENDEDCHARS)
    {
      if (moreSubdirsFollow)
        pprintf("%s", TBAR_HORZBAR_STR);
      else
        pprintf("%s", CBAR_HORZBAR_STR);
    }
    else if (charSet == UNICODECHARS)
    {
      if (moreSubdirsFollow)
        pprintf("%s", UTBAR_HORZBAR_STR);
      else
        pprintf("%s", UCBAR_HORZBAR_STR);
    }
    else
    {
      if (moreSubdirsFollow)
        pprintf("+---");
      else
        pprintf("\\---");
    }
  }

  /* optional display data */
  if (dspAttr)  /* attributes */
    pprintf("[%c%c%c%c%c%c%c%c] ",
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_DIRECTORY)?'D':' ',  /* keep this one? its always true */
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_ARCHIVE)?'A':' ',
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_COMPRESSED)?'C':' ',
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_ENCRYPTED)?'E':' ',
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_SYSTEM)?'S':' ',
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_HIDDEN)?'H':' ',
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_READONLY)?'R':' ',
      (ddata->dwDirAttributes & FILE_ATTRIBUTE_REPARSE_POINT)?'M':' '  /* often a mount point */
    );

  /* display directory name */
  pprintf("%s\n", currentpath);
}


/** 
 * Displays summary information about directory.
 * Expects to be called after displayFiles (optionally called)
 */
void displaySummary(char *path, char *padding, int hasMoreSubdirs, DIRDATA *ddata)
{
  addPadding(padding, hasMoreSubdirs);

  if (dspSumDirs)
  {
    if (showFiles == SHOWFILESON)
    {
      /* print File summary with lead padding, add filesize to it */
      pprintf("%s%lu files\n", padding, ddata->fileCnt);
    }

    /* print Directory summary with lead padding */
    pprintf("%s%lu subdirectories\n", padding, ddata->subdirCnt);

    /* show [nearly] blank line after summary */
    pprintf("%s\n", padding);
  }

  removePadding(padding);
}

/** 
 * Displays files in directory specified by path.
 * Path must end in slash \ or /
 * Returns -1 on error,
 *          0 if no files, but no errors either,
 *      or  1 if files displayed, no errors.
 */
int displayFiles(char *path, char *padding, int hasMoreSubdirs, DIRDATA *ddata)
{
  char buffer[MAXBUF];
  WIN32_FIND_DATA entry; /* current directory entry info    */
  HANDLE dir;         /* Current directory entry working with      */
  unsigned long filesShown = 0;

  /* get handle for files in current directory (using wildcard spec) */
  strcpy(buffer, path);
  strcat(buffer, "*");
  dir = FindFirstFile(buffer, &entry);
  if (dir == INVALID_HANDLE_VALUE)
    return -1;

  addPadding(padding, hasMoreSubdirs);

  /* cycle through directory printing out files. */
  do 
  {
    /* print padding followed by filename */
    if ( ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) &&
         ( ((entry.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN |
         FILE_ATTRIBUTE_SYSTEM)) == 0)  || dspAll) )
    {
      /* print lead padding */
      pprintf("%s", padding);

      /* optional display data */
      if (dspAttr)  /* file attributes */
        pprintf("[%c%c%c%c%c%c%c%c] ",
          (entry.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE)?'0':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)?'A':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED)?'C':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED)?'E':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)?'S':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)?'H':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_READONLY)?'R':' ',
          (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)?'M':' '
        );

      if (dspSize)  /* file size */
      {
        if (entry.nFileSizeHigh)
        {
#ifdef WIN32  /* convert to a 64bit value, then round to nearest KB */
          __int64 fsize = entry.nFileSizeHigh * ((__int64)ULONG_MAX + 1i64);
          fsize = (fsize + entry.nFileSizeLow + 512i64) / 1024i64;
          pprintf("%8I64uKB ", fsize);
#else
          pprintf("******** ");  /* error exceed max value we can display, > 4GB */
#endif
        }
        else
        {
          if (entry.nFileSizeLow < 1048576)  /* if less than a MB, display in bytes */
            pprintf("%10lu ", entry.nFileSizeLow);
          else                               /* otherwise display in KB */
            pprintf("%8luKB ", entry.nFileSizeLow/1024UL);
        }
      }

      /* process filename, convert to utf or OEM codepage, etc */
      if ((LFN_Enable_Flag == LFN_DISABLE) && (entry.cAlternateFileName[0] != '\0') )
      {
        if (charSet == UNICODECHARS)
          charToUTF8(CP_OEMCP, entry.cAlternateFileName, entry.cFileName, _MAX_PATH);
        else
          strcpy(entry.cFileName, entry.cAlternateFileName);
      }
      else
      {
        if (charSet == UNICODECHARS)
          charToUTF8(CP_ACP, entry.cFileName, entry.cFileName, _MAX_PATH);
        else
          charToDisplayChar(entry.cFileName);
      }

      /* print filename */
      pprintf("%s\n", entry.cFileName);

      filesShown++;
    }
  } while(FindNextFile(dir, &entry) != 0);

  if (filesShown)
  {
    pprintf("%s\n", padding);
  }

  /* cleanup directory search */
  FindClose(dir);
  /* dir = NULL; */

  removePadding(padding);

  /* store for summary display */
  if (ddata != NULL) ddata->fileCnt = filesShown;

  return (filesShown)? 1 : 0;
}


/**
 * Common portion of findFirstSubdir and findNextSubdir
 * Checks current FindFile results to determine if a valid directory
 * was found, and if so copies appropriate data into subdir and dsubdir.
 * It will repeat until a valid subdirectory is found or no more
 * are found, at which point it closes the FindFile search handle and
 * return INVALID_HANDLE_VALUE.  If successful, returns FindFile handle.
 */
HANDLE cycleFindResults(HANDLE findnexthnd, WIN32_FIND_DATA_BOTH &entry, char *subdir, char *dsubdir)
{
  if ( (charSet == UNICODECHARS) && (pFindFirstFileExW != NULL) )
  {
    /* cycle through directory until 1st non . or .. directory is found. */
    do
    {
      /* skip files & hidden or system directories */
      if ((((entry.ud.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) ||
           ((entry.ud.dwFileAttributes &
            (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0  && !dspAll) ) ||
          ((wcscmp(entry.ud.cFileName, UDOT    /* L"." */) == 0) ||
           (wcscmp(entry.ud.cFileName, UDOTDOT /* L".." */) == 0)) )
      {
        if (FindNextFileW(findnexthnd, &entry.ud) == 0)
        {
          FindClose(findnexthnd);      // prevent resource leaks
          return INVALID_HANDLE_VALUE; // no subdirs found
        }
      }
      else
      {
        /* set display name */
        if ((LFN_Enable_Flag == LFN_DISABLE) && (entry.ud.cAlternateFileName[0] != '\0') )
          convertUTF16toUTF8(entry.ud.cAlternateFileName, dsubdir, MAXBUF);
        else 
          convertUTF16toUTF8(entry.ud.cFileName, dsubdir, MAXBUF);

        /* set canical name to use for further FindFile calls */
        /* use short file name if exists as lfn may contain unicode values converted
         * to default character (eg. ?) and so not a valid path.
         * Note: if using unicode API and strings, this is not necessary.
         */
        if (entry.ud.cAlternateFileName[0] != '\0')
          WideCharToMultiByte(CP_ACP, 0, entry.ud.cAlternateFileName, -1, subdir, MAXBUF, NULL, NULL);
        else
          WideCharToMultiByte(CP_ACP, 0, entry.ud.cFileName, -1, subdir, MAXBUF, NULL, NULL);
        strcat(subdir, "\\");
      }
    } while (!*subdir); // while (subdir is still blank)
  }
  else
  {
    /* cycle through directory until 1st non . or .. directory is found. */
    do
    {
      /* skip files & hidden or system directories */
      if ((((entry.ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) ||
           ((entry.ad.dwFileAttributes &
            (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0  && !dspAll) ) ||
          ((strcmp(entry.ad.cFileName, ".") == 0) ||
           (strcmp(entry.ad.cFileName, "..") == 0)) )
      {
        if (FindNextFile(findnexthnd, &entry.ad) == 0)
        {
          FindClose(findnexthnd);      // prevent resource leaks
          return INVALID_HANDLE_VALUE; // no subdirs found
        }
      }
      else
      {
        /* set display name */
        if ((LFN_Enable_Flag == LFN_DISABLE) && (entry.ad.cAlternateFileName[0] != '\0') )
        {
          if (charSet == UNICODECHARS)
            charToUTF8(CP_OEMCP, entry.ad.cAlternateFileName, dsubdir, MAXBUF);
          else
            strcpy(dsubdir, entry.ad.cAlternateFileName);
        }
        else 
        {
          if (charSet == UNICODECHARS)
            charToUTF8(CP_ACP, entry.ad.cFileName, dsubdir, MAXBUF);
          else
          {
            strcpy(dsubdir, entry.ad.cFileName);
            charToDisplayChar(dsubdir);
          }
        }

        /* set canical name to use for further FindFile calls */
        /* use short file name if exists as lfn may contain unicode values converted
         * to default character (eg. ?) and so not a valid path.
         * Note: if using unicode API and strings, this is not necessary.
         */
        if (entry.ad.cAlternateFileName[0] != '\0')
          strcpy(subdir, entry.ad.cAlternateFileName);
        else
          strcpy(subdir, entry.ad.cFileName);
        strcat(subdir, "\\");
      }
    } while (!*subdir); // while (subdir is still blank)
  }

  return findnexthnd;
}


/* FindFile buffer used by findFirstSubdir and findNextSubdir only */
static WIN32_FIND_DATA_BOTH findSubdir_entry; /* current directory entry info    */

/**
 * Given the current path, find the 1st subdirectory.
 * The subdirectory found is stored in subdir.
 * subdir is cleared on error or no subdirectories.
 * Returns the findfirst search HANDLE, which should be passed to
 * findclose when directory has finished processing, and can be
 * passed to findnextsubdir to find subsequent subdirectories.
 * Returns INVALID_HANDLE_VALUE on error.
 * currentpath must end in \
 */
HANDLE findFirstSubdir(char *currentpath, char *subdir, char *dsubdir)
{
  static char buffer[MAXBUF];
  static WORD ubuf[MAXBUF];
  HANDLE dir;         /* Current directory entry working with      */

  /* get handle for files in current directory (using wildcard spec) */
  strcpy(buffer, currentpath);
  strcat(buffer, "*");

  if ( (charSet == UNICODECHARS) && (pFindFirstFileExW != NULL) )
  {
    if (!convertCPtoUTF16(CP_ACP, buffer, ubuf, MAXBUF))
    {
      printf("ERR: codepage to utf-16\n");
      return INVALID_HANDLE_VALUE;
    }
    dir = pFindFirstFileExW(ubuf, FindExInfoStandard, &findSubdir_entry.ud, FindExSearchLimitToDirectories, NULL, 0);
  }
  else
    dir = pFindFirstFileExA(buffer, FindExInfoStandard, &findSubdir_entry.ad, FindExSearchLimitToDirectories, NULL, 0);

  if (dir == INVALID_HANDLE_VALUE)
  {
    showInvalidPath(currentpath);
    return INVALID_HANDLE_VALUE;
  }

  /* clear result path */
  strcpy(subdir, "");

  return cycleFindResults(dir, findSubdir_entry, subdir, dsubdir);
}

/**
 * Given a search HANDLE, will find the next subdirectory, 
 * setting subdir to the found directory name.
 * dsubdir is the name to display (lfn or sfn as appropriate)
 * currentpath must end in \
 * If a subdirectory is found, returns 0, otherwise returns 1
 * (either error or no more files).
 */
int findNextSubdir(HANDLE findnexthnd, char *subdir, char *dsubdir)
{
  /* clear result path */
  strcpy(subdir, "");

  if ( (charSet == UNICODECHARS) && (pFindFirstFileExW != NULL) )
  {
    if (FindNextFileW(findnexthnd, &findSubdir_entry.ud) == 0)
      return 1; // no subdirs found
  }
  else
  {
    if (FindNextFile(findnexthnd, &findSubdir_entry.ad) == 0)
      return 1; // no subdirs found
  }

  if (cycleFindResults(findnexthnd, findSubdir_entry, subdir, dsubdir) == INVALID_HANDLE_VALUE)
    return 1;
  else
    return 0;
}

/**
 * Given an initial path, displays the directory tree with
 * a non-recursive function using a Stack.
 * initialpath must be large enough to hold an added slash \ or /
 * if it does not already end in one.
 * Returns the count of subdirs in initialpath.
 */
long traverseTree(char *initialpath)
{
  long subdirsInInitialpath;
  char padding[MAXPADLEN] = "";
  char subdir[MAXBUF];
  char dsubdir[MAXBUF];
  register SUBDIRINFO *sdi;

  STACK s;
  stackDefaults(&s);
  stackInit(&s);

  if ( (sdi = newSubdirInfo(NULL, initialpath, initialpath)) == NULL)
    return 0L;
  stackPushItem(&s, sdi);

  /* Store count of subdirs in initial path so can display message if none. */
  subdirsInInitialpath = sdi->subdircnt;

  do
  {
    sdi = (SUBDIRINFO *)stackPopItem(&s);

    if (sdi->findnexthnd == INVALID_HANDLE_VALUE)  // findfirst not called yet
    {
      // 1st time this subdirectory processed, so display its name & possibly files
      if (sdi->parent == NULL) // if initial path
      {
        // display initial path
        showCurrentPath(/*sdi->dsubdir*/initialpath, NULL, -1, &(sdi->ddata));
      }
      else // normal processing (display path, add necessary padding)
      {
        showCurrentPath(sdi->dsubdir, padding, (sdi->parent->subdircnt > 0L)?1 : 0, &(sdi->ddata));
        addPadding(padding, (sdi->parent->subdircnt > 0L)?1 : 0);
      }

      if (showFiles == SHOWFILESON)  displayFiles(sdi->currentpath, padding, (sdi->subdircnt > 0L)?1 : 0, &(sdi->ddata));
      displaySummary(sdi->currentpath, padding, (sdi->subdircnt > 0L)?1 : 0, &(sdi->ddata));
    }

    if (sdi->subdircnt > 0) /* if (there are more subdirectories to process) */
    {
      int flgErr;
      if (sdi->findnexthnd == INVALID_HANDLE_VALUE)
      {
        sdi->findnexthnd = findFirstSubdir(sdi->currentpath, subdir, dsubdir);
        flgErr = (sdi->findnexthnd == INVALID_HANDLE_VALUE);
      }
      else
      {
        flgErr = findNextSubdir(sdi->findnexthnd, subdir, dsubdir);
      }

      if (flgErr) // don't add invalid paths to stack
      {
        printf("INTERNAL ERROR: subdir count changed, expecting %li more!\n", sdi->subdircnt+1L);

        sdi->subdircnt = 0; /* force subdir counter to 0, none left */
        stackPushItem(&s, sdi);
      }
      else
      {
        sdi->subdircnt = sdi->subdircnt - 1L; /* decrement subdirs left count */
        stackPushItem(&s, sdi);

        /* store necessary information, validate subdir, and if no error store it. */
        if ((sdi = newSubdirInfo(sdi, subdir, dsubdir)) != NULL)
          stackPushItem(&s, sdi);
      }
    }
    else /* this directory finished processing, so free resources */
    {
      /* Remove the padding for this directory, all but initial path. */
      if (sdi->parent != NULL)
        removePadding(padding);

      /* Prevent resource leaks, by ending findsearch and freeing memory. */
      FindClose(sdi->findnexthnd);
      if (sdi != NULL)
      {
        if (sdi->currentpath != NULL)
          free(sdi->currentpath);
        free(sdi);
      }
    }
  } while (stackTotalItems(&s)); /* while (stack is not empty) */

  stackTerm(&s);

  return subdirsInInitialpath;
}


/**
 * Process strings, converting \\, \n, \r, and \t to actual chars.
 * This method is used to allow the message catalog to use \\, \n, \r, and \t
 * returns a pointer to its internal buffer, so strcpy soon after use.
 * Can only handle lines up to MAXLINE chars.
 * This is required because most messages are passed as
 * string arguments to printf, and not actually parsed by it.
 */
char *processLine(char *line)
{
  static char buffer[MAXLINE+MAXLINE];
  register char *src = line, *dst = buffer;

  if (line == NULL) return NULL;

  /* cycle through copying characters, except when a \ is encountered. */
  for ( ; *src != '\0'; src++, dst++)
  {
    if (*src == '\\')
    {
      src++;
      switch (*src)
      {
	  case '\0': /* a slash ends a line, ignore the slash. */
		  src--; /* next time through will see the '\0'    */
		  break;
	  case '\\': /* a single slash */
		  *dst = '\\';
		  break;
	  case 'n': /* a newline */
		  *dst = '\n';
		  break;
	  case 'r': /* a carriage return */
		  *dst = '\r';
		  break;
	  case 't': /* a horizontal tab */
		  *dst = '\t';
		  break;
	  default: /* just copy over the letter */
		  *dst = *src;
		  break;
      }
    }
    else
      *dst = *src;
  }

  /* ensure '\0' terminated */
  *dst = '\0';

  return buffer;
}


void FixOptionText(void)
{
  char buffer[MAXLINE];  /* sprintf can have problems with src==dest */

  /* Handle %c for options within messages using Set 8 */
  strcpy(buffer, treeUsage);
  sprintf(treeUsage, buffer, optionchar1, OptShowFiles[0], optionchar1, OptUseASCII[0]);
  strcpy(buffer, treeFOption);
  sprintf(treeFOption, buffer, optionchar1, OptShowFiles[0]);
  strcpy(buffer, treeAOption);
  sprintf(treeAOption, buffer, optionchar1, OptUseASCII[0]);
  strcpy(buffer, useTreeHelp);
  sprintf(useTreeHelp, buffer, optionchar1);
}


/* Loads all messages from the message catalog.
 * If USE_CATGETS is undefined or failure finding catalog then
 * hard coded strings are used
 */
void loadAllMessages(void)
{
  #ifdef USE_CATGETS
    nl_catd cat;              /* store id of our message catalog global       */
    char buffer[MAXLINE];
    char *bufPtr;

    /* Open the message catalog, keep hard coded values on error. */
    if ((cat = catopen (catsFile, MCLoadAll)) == -1) 
    {
      FixOptionText(); /* Changes %c in certain lines with default option characters. */
      return;
    }

    /* common to many functions [Set 1] */
    bufPtr = catgets (cat, 1, 1, newLine);
    if (bufPtr != newLine) strcpy(newLine, processLine(bufPtr));

    /* main [Set 1] */
    bufPtr = catgets (cat, 1, 2, pathListingNoLabel);
    if (bufPtr != pathListingNoLabel) strcpy(pathListingNoLabel, processLine(bufPtr));
    bufPtr = catgets (cat, 1, 3, pathListingWithLabel);
    if (bufPtr != pathListingWithLabel) strcpy(pathListingWithLabel, processLine(bufPtr));
    bufPtr = catgets (cat, 1, 4, serialNumber);
    if (bufPtr != serialNumber) strcpy(serialNumber, processLine(bufPtr));
    bufPtr = catgets (cat, 1, 5, noSubDirs);
    if (bufPtr != noSubDirs) strcpy(noSubDirs, processLine(bufPtr));
    bufPtr = catgets (cat, 1, 6, pauseMsg);
    if (bufPtr != pauseMsg) strcpy(pauseMsg, processLine(bufPtr));

    /* showUsage [Set 2] */
    bufPtr = catgets (cat, 2, 1, treeDescription);
    if (bufPtr != treeDescription) strcpy(treeDescription, processLine(bufPtr));
    bufPtr = catgets (cat, 2, 2, treeUsage);
    if (bufPtr != treeUsage) strcpy(treeUsage, processLine(bufPtr));
    bufPtr = catgets (cat, 2, 3, treeFOption);
    if (bufPtr != treeFOption) strcpy(treeFOption, processLine(bufPtr));
    bufPtr = catgets (cat, 2, 4, treeAOption);
    if (bufPtr != treeAOption) strcpy(treeAOption, processLine(bufPtr));

    /* showInvalidUsage [Set 3] */
    bufPtr = catgets (cat, 3, 1, invalidOption);
    if (bufPtr != invalidOption) strcpy(invalidOption, processLine(bufPtr));
    bufPtr = catgets (cat, 3, 2, useTreeHelp);
    if (bufPtr != useTreeHelp) strcpy(useTreeHelp, processLine(bufPtr));

    /* showVersionInfo [Set 4] */
    /* also uses treeDescription from Set 2 */
    bufPtr = catgets (cat, 4, 1, treeGoal);
    if (bufPtr != treeGoal) strcpy(treeGoal, processLine(bufPtr));
    bufPtr = catgets (cat, 4, 2, treePlatforms);
    if (bufPtr != treePlatforms) strcpy(treePlatforms, processLine(bufPtr));
    bufPtr = catgets (cat, 4, 3, version);
    if (bufPtr != version) strcpy(version, processLine(bufPtr));
    bufPtr = catgets (cat, 4, 4, writtenBy);
    if (bufPtr != writtenBy) strcpy(writtenBy, processLine(bufPtr));
    bufPtr = catgets (cat, 4, 5, writtenDate);
    if (bufPtr != writtenDate) strcpy(writtenDate, processLine(bufPtr));
    bufPtr = catgets (cat, 4, 6, contact);
    if (bufPtr != contact) strcpy(contact, processLine(bufPtr));
    bufPtr = catgets (cat, 4, 7, copyright);
    if (bufPtr != copyright) strcpy(copyright, processLine(bufPtr));
//ifdef USE_CATGETS
    bufPtr = catgets (cat, 4, 8, catsCopyright);
    if (bufPtr != catsCopyright) strcpy(catsCopyright, processLine(bufPtr));
//endif

    /* showInvalidDrive [Set 5] */
    bufPtr = catgets (cat, 5, 1, invalidDrive);
    if (bufPtr != invalidDrive) strcpy(invalidDrive, processLine(bufPtr));

    /* showInvalidPath [Set 6] */
    bufPtr = catgets (cat, 6, 1, invalidPath);
    if (bufPtr != invalidPath) strcpy(invalidPath, processLine(bufPtr));

    /* Misc Error messages [Set 7] */
    /* showBufferOverrun */
    /* %u required to show what the buffer's current size is. */
    bufPtr = catgets (cat, 7, 1, bufferToSmall);
    if (bufPtr != bufferToSmall) strcpy(bufferToSmall, processLine(bufPtr));
    /* showOutOfMemory */
    /* %s required to display what directory we were processing when ran out of memory. */
    bufPtr = catgets (cat, 7, 2, outOfMemory);
    if (bufPtr != outOfMemory) strcpy(outOfMemory, processLine(bufPtr));

    /* parseArguments - options [Set 8] */
    /* Note all of these are single characters (only 1st character used) */
    bufPtr = catgets (cat, 8, 1, NULL);
    if (bufPtr != NULL) optionchar1 = bufPtr[0];
    bufPtr = catgets (cat, 8, 2, NULL);
    if (bufPtr != NULL) optionchar2 = bufPtr[0];
    bufPtr = catgets (cat, 8, 3, NULL);

    /* close the message catalog */
    catclose (cat);
  #endif

  /* Changes %c in certain lines with proper option characters. */
  FixOptionText();
}


/* Initialize function pointers for Win32 API functions not always available */
void initFuncPtrs(void)
{
#ifdef WIN32
  /* Attempt to get Unicode version of Win32 APIs 
   * Because they are in Kernel32, we assume it's always loaded
   * and so don't need to use LoadLibrary/FreeLibrary to maintain a reference count.
   */
  HMODULE hKERNEL32 = GetModuleHandle("KERNEL32");
  if (hKERNEL32 == NULL) printf("ERROR: unable to get KERNEL32 handle, %i\n", GetLastError());

  pFindFirstFileExA = (fFindFirstFileExA)GetProcAddress(hKERNEL32, "FindFirstFileExA");
  if (pFindFirstFileExA == NULL)  printf("WARNING: unable to get FindFirstFileExA, %i\n", GetLastError());
  if (pFindFirstFileExA == NULL) pFindFirstFileExA = myFindFirstFileExA;

  if (charSet == UNICODECHARS)
  {
    pFindFirstFileExW = (fFindFirstFileExW)GetProcAddress(hKERNEL32, "FindFirstFileExW");
    if (pFindFirstFileExW == NULL)  printf("WARNING: unable to get FindFirstFileExW, %i\n", GetLastError());
  }
#endif
}

int main(int argc, char *argv[])
{
  char serial[SERIALLEN]; /* volume serial #  0000:0000 */
  char volume[VOLLEN];    /* volume name (label), possibly none */

  /* Load all text from message catalog (or uses hard coded text) */
  loadAllMessages();

  /* Parse any command line arguments, obtain path */
  parseArguments(argc, argv);

  /* Initialize screen size, may reset pause to NOPAUSE if redirected */
  getConsoleSize();

  /* Initialize function pointers for Win32 API functions not always available */
  initFuncPtrs();

  /* Unicode mode only, Output BOM for UTF-8 signature */
  if (charSet == UNICODECHARS)  pprintf("%s", UMARKER);


  /* Get Volume & Serial Number */
  GetVolumeAndSerial(volume, serial, path);
  if (strlen(volume) == 0)
    pprintf(pathListingNoLabel);
  else
    pprintf(pathListingWithLabel, volume);
  if (serial[0] != '\0')  /* Don't print anything if no serial# found */
    pprintf(serialNumber, serial);

  /* now traverse & print tree, returns nonzero if has subdirectories */
  if (traverseTree(path) == 0)
    pprintf(noSubDirs);
  else if (dspSumDirs) /* show count of directories processed */
    pprintf("\n    %lu total directories\n", totalSubDirCnt+1);

  return 0;
}
