/* retawq/cursesbi.c - a small built-in curses library emulation
   This file is part of retawq (<http://retawq.sourceforge.net/>), a network
   client created by Arne Thomassen; retawq is basically released under certain
   versions of the GNU General Public License and WITHOUT ANY WARRANTY.
   Read the file COPYING for license details, README for program information.
   Copyright (C) 2004-2006 Arne Thomassen <arne@arne-thomassen.de>
*/

#include "stuff.h"

#include <time.h>

declare_local_i18n_buffer

static char strbuf[STRBUF_SIZE];

static WINDOW __stdscr;
WINDOW* stdscr = &__stdscr;
int COLS, LINES;

static attr_t currattr = 0, currtermattr = 0, igntermattr = 0;

#define SAI(x, y) ( ((y) * COLS) + (x) ) /* screen array index */
#define A_CHARTEXT (255)


/* Helper functions */

static int refresh_statvar[26], refresh_dynvar[26];
static char refreshbuf[STRBUF_SIZE]; /* to reduce the number of write()s */
static size_t refreshbuf_used = 0;

static void refreshbuf_flush(void)
{ /* if (lfdmbs(1)) */ my_write(fd_stdout, refreshbuf, refreshbuf_used);
  refreshbuf_used = 0;
}

static my_inline void refreshbuf_append_ch(const char ch)
{ if (refreshbuf_used >= STRBUF_SIZE) refreshbuf_flush();
  refreshbuf[refreshbuf_used++] = ch;
}

static void refreshbuf_append_str(const char* str)
{ char ch;
  while ( (ch = *str++) != '\0' ) refreshbuf_append_ch(ch);
}

static void reinit_stdscr_data(void)
/* Call this whenever COLS or LINES changed. */
{ chtype* text;
  unsigned char* dl;
  int prod = COLS * LINES, dlsize = stdscr->dirty_lines_size = (LINES + 7) / 8,
    count;
  stdscr->x = stdscr->y = 0; __dealloc(stdscr->text);
  text = stdscr->text = __memory_allocate(prod * sizeof(chtype), mapOther);
  for (count = 0; count < prod; count++) text[count] = ' ';
  __dealloc(stdscr->attr);
  stdscr->attr = memory_allocate(prod * sizeof(attr_t), mapOther);
  __dealloc(stdscr->dirty_lines);
  stdscr->dirty_lines = dl = __memory_allocate(dlsize, mapOther);
  my_memset(dl, 255, dlsize); stdscr->update_cursor = truE;
}

static void coord_limit(int* _x, int* _y)
{ int x = *_x, y = *_y;
  if (x < 0) x = 0;
  else if (x > COLS - 1) x = COLS - 1;
  if (y < 0) y = 0;
  else if (y > LINES - 1) y = LINES - 1;
  *_x = x; *_y = y;
}


/* Initialization */

static const unsigned char* ttbuf;
static size_t ttsize;

static tBoolean __init ttdim(size_t number_of_bytes)
{ tBoolean retval;
  if (ttsize >= number_of_bytes) { ttsize -= number_of_bytes; retval = truE; }
  else retval = falsE;
  return(retval);
}

static my_inline tSint16 __init __calc_short(const unsigned char* ptr)
{ const unsigned char byte1 = ptr[0], byte2 = ptr[1];
  return(((tSint16) byte1) | (((tSint16) byte2) << 8));
}

static one_caller tBoolean __init calc_short(/*@out@*/ tSint16* dest,
  size_t count)
{ if (!ttdim(2 * count)) return(falsE);
  while (count-- > 0) { *dest++ = __calc_short(ttbuf); ttbuf += 2; }
  return(truE);
}

my_enum1 enum
{ shvColumns = 0, shvLines = 1, shvMaxColors = 2, shvMaxPairs = 3,
  shvNoColorVideo = 4
} my_enum2(unsigned char) tShortValue;
#define NUM_SHORT_VALUES (5)

static const unsigned char short_index[NUM_SHORT_VALUES] __initdata =
{ 0, 2, 13, 14, 15 };

static const_after_init tSint16 short_value[NUM_SHORT_VALUES];

/* begin-autogenerated */
my_enum1 enum
{ strvClearScreen = 0, strvMove = 1, strvEnterBold = 2, strvEnterCa = 3,
  strvEnterReverse = 4, strvEnterUnderline = 5, strvExitAttrs = 6,
  strvExitCa = 7, strvExitUnderline = 8, strvKeyDc = 9, strvKeyDown = 10,
  strvKeyHome = 11, strvKeyIc = 12, strvKeyLeft = 13, strvKeyNpage = 14,
  strvKeyPpage = 15, strvKeyRight = 16, strvKeyUp = 17, strvKeypadLocal = 18,
  strvKeypadXmit = 19, strvKeyEnd = 20, strvOrigColors = 21,
  strvInitializeColor = 22, strvInitializePair = 23, strvSetColorPair = 24
} my_enum2(unsigned char) tStringValue;
#define NUM_STRING_VALUES (25)

static const unsigned short string_index[NUM_STRING_VALUES] __initdata =
{ 5, 10 | 1024, 27, 28, 34, 36, 39, 40, 44, 59, 61, 76, 77, 79, 81, 82, 83,
  87, 88, 89, 164, 298, 299, 300, 301
};

#define NUM_KEY_VALUES (10)
static const struct
{ tStringValue strv;
  tKey key;
} key_value[NUM_KEY_VALUES] __initdata =
{ { strvKeyDc, KEY_DC },
  { strvKeyDown, KEY_DOWN },
  { strvKeyHome, KEY_HOME },
  { strvKeyIc, KEY_IC },
  { strvKeyLeft, KEY_LEFT },
  { strvKeyNpage, KEY_NPAGE },
  { strvKeyPpage, KEY_PPAGE },
  { strvKeyRight, KEY_RIGHT },
  { strvKeyUp, KEY_UP },
  { strvKeyEnd, KEY_END }
};
/* end-autogenerated */

static const char* string_value[NUM_STRING_VALUES];

static const_after_init struct
{ const char* str;
  size_t len;
  tKey key;
} key_string[NUM_KEY_VALUES];
static const_after_init unsigned short num_key_strings = 0;

static tBoolean strv_try(tStringValue v)
{ const char* str = string_value[v];
  const tBoolean is_available = cond2boolean(str != NULL);
  if (is_available) refreshbuf_append_str(str);
  return(is_available);
}

#define TI_BAD(str) do { debugmsg("TI_BAD(" str ")\n"); goto bad; } while (0)

static tBoolean bicurses_timeout_handler(/*@out@*/ int*); /* prototype */

WINDOW* __init initscr(void)
{ const char *termname = getenv("TERM"), *termpath, *strEA;
  const unsigned char* siptr; /* "strings index pointer" */
  char ch, *filename;
  void* filebuf;
  size_t filesize;
  tBoolean need_slash;
  tSint16 header[6], names_size, flags_size, num_shorts, num_strings,
    strings_size;

  if ( (termname == NULL) || ( (ch = *termname) == '\0') )
    fatal_error(0, _("bad value of environment variable TERM"));
  debugmsg("termname=*"); debugmsg(termname); debugmsg("*\n");
  termpath = getenv("TERMINFO");
  if ( (termpath == NULL) || (*termpath == '\0') )
  { termpath = "/usr/share/terminfo/"; need_slash = falsE; }
  else need_slash = cond2boolean(termpath[strlen(termpath) - 1] != chDirsep);
  debugmsg("termpath=*"); debugmsg(termpath); debugmsg("*\n");
  my_spf(strbuf, STRBUF_SIZE, &filename, "%s%s%c/%s", termpath,
    (need_slash ? strSlash : strEmpty), ch, termname);
  debugmsg("filename=*"); debugmsg(filename); debugmsg("*\n");
  if (my_mmap_file_readonly(filename, &filebuf, &filesize) != 2)
  { bad: fatal_error(0, _("bad terminfo file")); }
  my_spf_cleanup(strbuf, filename);

  ttbuf = filebuf; ttsize = filesize;

  /* get the header */
  if (!calc_short(header, 6)) TI_BAD("header");
#if CONFIG_DEBUG
  sprint_safe(strbuf, "terminfo header: %d - %d - %d - %d - %d - %d\n",
    header[0], header[1], header[2], header[3], header[4], header[5]);
  debugmsg(strbuf);
#endif
  if (header[0] != 0432) TI_BAD("magic");
  if ( (  names_size = header[1]) < 0 ) TI_BAD("h1");
  if ( (  flags_size = header[2]) < 0 ) TI_BAD("h2");
  if ( (  num_shorts = header[3]) < 0 ) TI_BAD("h3");
  if ( ( num_strings = header[4]) < 0 ) TI_BAD("h4");
  if ( (strings_size = header[5]) < 0 ) TI_BAD("h5");

  /* skip the terminal names */
  if (!ttdim(names_size)) TI_BAD("names");
  ttbuf += names_size;

  /* skip the flags */
  if (!ttdim(flags_size)) TI_BAD("flags");
  ttbuf += flags_size;
  if (((long int) ttbuf) & 1) { if (!ttdim(1)) { TI_BAD("flags2"); } ttbuf++; }

  /* handle the short values */
  if (!ttdim(2 * num_shorts)) TI_BAD("short");
  my_memclr_arr(short_value);
  { tShortValue i;
    for (i = 0; i < NUM_SHORT_VALUES; i++)
    { const size_t idx = short_index[i];
      tSint16 v;
      if ((tSint16) idx >= num_shorts) break; /* not so many shorts defined */
      v = __calc_short(ttbuf + 2 * idx);
      if (v > 0) short_value[i] = v;
    }
  }
  ttbuf += 2 * num_shorts;

  /* handle the strings */
  if (!ttdim(2 * num_strings)) TI_BAD("stridx");
  siptr = ttbuf; ttbuf += 2 * num_strings;
  my_memclr_arr(string_value);
  { tStringValue i;
    for (i = 0; i < NUM_STRING_VALUES; i++)
    { size_t idx = string_index[i];
      tBoolean is_required;
      if (idx & 1024) { idx &= ~1024; is_required = truE; }
      else is_required = falsE;
      if ((tSint16) idx < num_strings)
      { const tSint16 v = __calc_short(siptr + 2 * idx);
        if ( (v == -1) || (v == -2) ) { if (is_required) goto rvm; }
        else if (v < 0) TI_BAD("stridx2");
        else /* cautiously try to get the string */
        { const unsigned char *ptr, *ptr2;
          char* dest;
          size_t size_left, size;
          if ((ssize_t) ttsize <= v) TI_BAD("stridx3");
          size_left = ttsize - v; ptr = ptr2 = ttbuf + v;
          while (size_left > 0)
          { if (*ptr2++ == '\0') goto string_terminates;
            size_left--;
          }
          TI_BAD("unterminated string");
          string_terminates:
          size = ptr2 - ptr;
          if (size <= 1) /* string is empty */
          { if (is_required) goto rvm;
            else goto next_string;
          }
          else if (size > STRBUF_SIZE / 2) /* can't be serious */
            TI_BAD("string length");
          string_value[i] = dest = __memory_allocate(size, mapPermanent);
          my_memcpy(dest, ptr, size);
#if CONFIG_DEBUG
          sprint_safe(strbuf, "terminfo string: %d - %d - %d - *%s*", i, idx,
            v, dest);
          { unsigned char* x = (unsigned char*) strbuf;
            while (1)
            { const unsigned char uch = *x;
              if (uch == '\0') break;
              else if (uch < 32) *x = '?'; /* esp. for the escape character */
              x++;
            }
          }
          debugmsg(strbuf); debugmsg(strNewline);
#endif
        }
      }
      else if (is_required)
      { rvm: fatal_error(0, _("required value missing in terminfo file")); }
      next_string: {}
    }
  }

  my_munmap(filebuf, filesize);
  strEA = string_value[strvExitAttrs];

  /* check some terminal capabilities */
  { if ( (string_value[strvEnterReverse] == NULL) || (strEA == NULL) )
      igntermattr |= A_REVERSE;
    if ( (string_value[strvEnterBold] == NULL) || (strEA == NULL) )
      igntermattr |= A_BOLD;
    if ( (string_value[strvEnterUnderline] == NULL) ||
         (string_value[strvExitUnderline] == NULL ) )
      igntermattr |= A_UNDERLINE;
  }

  /* try to find out the terminal size */
  { int x, y;
    if ( (env_termsize(&x, &y)) || (calc_termsize(&x, &y)) )
    { COLS = x; LINES = y; }
    else if ( ( (x = short_value[shvColumns]) >= CURSES_MINCOLS ) &&
              ( (y = short_value[shvLines]) >= CURSES_MINLINES ) )
    { COLS = x; LINES = y; }
    else { COLS = 80; LINES = 24; }
  }

  /* look which of the possibly useful key strings are actually available */
  { unsigned short count;
    for (count = 0; count < NUM_KEY_VALUES; count++)
    { const tStringValue strv = key_value[count].strv;
      const char* str = string_value[strv];
      size_t len;
      if ( (str != NULL) && (*str == 27) && ( (len = strlen(str)) < 30) )
      { /* string exists and has decent form and length */
#if CONFIG_DEBUG
        sprint_safe(strbuf, "key string map: %d -> %d\n",strv,num_key_strings);
        debugmsg(strbuf);
#endif
        key_string[num_key_strings].str = str; string_value[strv] = NULL;
        key_string[num_key_strings].len = len;
        key_string[num_key_strings++].key = key_value[count].key;
      }
    }
  }

  my_memclr_arr(refresh_statvar); my_memclr_arr(refresh_dynvar);
  my_memclr_var(__stdscr); reinit_stdscr_data();
  (void) strv_try(strvEnterCa); (void) strv_try(strvKeypadXmit);
  if (strEA != NULL) refreshbuf_append_str(strEA);
  timeout_register(bicurses_timeout_handler);
  return(stdscr);
}


/* curses interface */

int addch(chtype c)
{ const int x = stdscr->x, y = stdscr->y, idx = SAI(x, y);
  stdscr->text[idx] = c & A_CHARTEXT;
  stdscr->attr[idx] = (c & ~A_CHARTEXT) | currattr;
  my_bit_set(stdscr->dirty_lines, y);
  if (x < COLS - 1) { stdscr->x++; stdscr->update_cursor = truE; }
  return(OK);
}

int addstr(const char* _str)
{ const unsigned char* str = (const unsigned char*) _str; /* avoid sign ext. */
  unsigned char ch;
  while ( (ch = *str++) != '\0' ) (void) addch((chtype) ch);
  return(OK);
}

int addnstr(const char* _str, int len)
{ const unsigned char* str = (const unsigned char*) _str; /* avoid sign ext. */
  while (len-- > 0)
  { const unsigned char ch = *str++;
    if (ch == '\0') break;
    (void) addch((chtype) ch);
  }
  return(OK);
}

int attron(attr_t a)
{
#if MIGHT_USE_COLORS
  if (a & __A_COLORMARK) currattr &= ~__A_COLORPAIRMASK;
#endif
  currattr |= a;
  return(0);
}

int attroff(attr_t a)
{
#if MIGHT_USE_COLORS
  if (a & __A_COLORMARK) a |= __A_COLORPAIRMASK; /* caller turns color off */
#endif
  currattr &= ~a;
  return(0);
}

int clear(void)
{ (void) strv_try(strvClearScreen);
  return(OK);
}

int clrtoeol(void)
{ int x = stdscr->x;
  if (x < COLS)
  { const int y = stdscr->y;
    while (x < COLS)
    { const int idx = SAI(x, y);
      stdscr->text[idx] = ' '; stdscr->attr[idx] = 0; x++;
    }
    my_bit_set(stdscr->dirty_lines, y);
  }
  return(OK);
}

int endwin(void)
{ (void) strv_try(strvKeypadLocal); (void) strv_try(strvExitAttrs);
#if 0 /* MIGHT_USE_COLORS */
  if (....) { (void) strv_try(strvOrigColors); (void) strv_try(strvOrigPair); }
#endif
  if (!strv_try(strvExitCa)) (void) strv_try(strvClearScreen); /* try all... */
  if (refreshbuf_used > 0) refreshbuf_flush(); /* (we'll quit soon) */
  return(OK);
}

chtype inch(void)
{ const int idx = SAI(stdscr->x, stdscr->y);
  return(stdscr->text[idx] | stdscr->attr[idx]);
}

int move(int y, int x)
{ coord_limit(&x, &y);
  stdscr->x = x; stdscr->y = y; stdscr->update_cursor = truE;
  return(OK);
}

int mvaddch(int y, int x, chtype ch)
{ (void) move(y, x); (void) addch(ch);
  return(OK);
}

int mvaddnstr(int y, int x, const char* str, int len)
{ (void) move(y, x); (void) addnstr(str, len);
  return(OK);
}

int resizeterm(int l, int c)
{ COLS = c; LINES = l;
  reinit_stdscr_data();
  return(OK);
}

#if MIGHT_USE_COLORS
int start_color(void)
{ int retval;
#if 0
  if (....)
  { (void) strv_try(strvOrigColors); (void) strv_try(strvOrigPair);
    retval = OK;
  }
  else
#endif
  { retval = ERR; }
  return(retval);
}
#endif


/* getch() */

#define INPUTBUFLEN (100)
typedef unsigned char tInputbufIndex;
static tInputbufIndex inputbuf_count = 0, fragmentbuf_count = 0;
static tKey inputbuf[INPUTBUFLEN];
static char fragmentbuf[INPUTBUFLEN];

static tBoolean time_limit_is_valid = falsE;

#if HAVE_GETTIMEOFDAY

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

static struct timeval time_limit;

static void time_limit_set(void)
{ time_t t;
  if (gettimeofday(&time_limit, NULL) == 0)
  { time_limit.tv_sec++; time_limit_is_valid = truE; }
  else if ( (t = my_time()) > 0 )
  { my_memclr_var(time_limit);
    time_limit.tv_sec = t + 2; /* ("+2" to wait at least one _full_ second) */
    time_limit_is_valid = truE;
  }
  else time_limit_is_valid = falsE;
}

static long time_left(void)
{ long retval = 0, v;
  if (time_limit_is_valid)
  { struct timeval tv;
    time_t t;
    if (gettimeofday(&tv, NULL) == 0) { /* fine */ }
    else if ( (t = my_time()) > 0 ) { my_memclr_var(tv); tv.tv_sec = t; }
    else goto out;
    v = time_limit.tv_sec - tv.tv_sec;
    if (v < 0) goto out;
    else if (v > 10) v = 10; /* "should not happen" */
    retval = (v * 1000) + ( (time_limit.tv_usec - tv.tv_usec) / 1000 );
  }
  out:
  return(retval);
}

#else /* #if HAVE_GETTIMEOFDAY */

static time_t time_limit;

static void time_limit_set(void)
{ time_limit = my_time();
  if (time_limit > 0) { time_limit += 2; time_limit_is_valid = truE; }
  else time_limit_is_valid = falsE;
}

static long time_left(void)
{ long retval = 0, v;
  if (time_limit_is_valid)
  { v = my_time();
    if (v <= 0) goto out;
    v = time_limit - v;
    if (v < 0) goto out;
    else if (v > 10) v = 10; /* "should not happen" */
    retval = v * 1000;
  }
  out:
  return(retval);
}

#endif /* #if HAVE_GETTIMEOFDAY */

static __my_inline tBoolean time_is_over(void)
{ return(cond2boolean(time_left() <= 0));
}

static tBoolean bicurses_timeout_handler(/*@out@*/ int* _msec)
{ tBoolean retval;
  if (inputbuf_count > 0) { *_msec = 0; retval = truE; }
  else if (fragmentbuf_count > 0)
  { long msec = time_left();
    if (msec < 0) msec = 0;
    msec += 2;
      /* CHECKME: that's a hack to sleep slightly longer; without it, pressing
         the Escape key (and nothing further) doesn't work... */
    *_msec = (int) msec; retval = truE;
  }
  else retval = falsE;
  return(retval);
}

static __my_inline void inputbuf_append(const char ch)
{ inputbuf[inputbuf_count++] = (tKey) ((unsigned char) ch);
}

static void fragmentbuf_flush(void)
{ tInputbufIndex count;
#if CONFIG_DEBUG
  sprint_safe(strbuf, "fragmentbuf_flush(): %d\n", fragmentbuf_count);
  debugmsg(strbuf);
#endif
  for (count = 0; count < fragmentbuf_count; count++)
    inputbuf_append(fragmentbuf[count]);
  fragmentbuf_count = 0; time_limit_is_valid = falsE;
}

static one_caller void handle_input(const char ch)
{ if (fragmentbuf_count > 0)
  { fragmentbuf[fragmentbuf_count++] = ch;
    if (time_is_over())
    { /* character came in too late, can't belong to any old escape sequence */
      tBoolean is_escape;
      do_flush:
      is_escape = cond2boolean(ch == 27);
      if (is_escape) fragmentbuf_count--; /* don't flush a new escape */
      fragmentbuf_flush();
      if (is_escape) goto append_escape;
    }
    else
    { unsigned short count;
      tBoolean found_container = falsE;
      const tInputbufIndex fraglen = fragmentbuf_count;
      for (count = 0; count < num_key_strings; count++)
      { const size_t len = key_string[count].len;
        if (len < fraglen) continue; /* can't match */
        if (!my_memdiff(key_string[count].str, fragmentbuf, fraglen))
        { if (len == fraglen) /* exact match */
          { inputbuf[inputbuf_count++] = key_string[count].key;
            fragmentbuf_count = 0;
#if CONFIG_DEBUG
            sprint_safe(strbuf, "key string match: %d\n", count);
            debugmsg(strbuf);
#endif
            return;
          }
          else found_container = truE;
        }
      }
      if (!found_container)
      { /* The current contents of the fragment buffer will never match any of
           our key strings, no matter how many further bytes we get. Thus: */
        goto do_flush;
      }
    }
  }
  else if (ch == 27)
  { append_escape: fragmentbuf[fragmentbuf_count++] = ch; time_limit_set(); }
  else inputbuf_append(ch);
}

int my_builtin_getch(tBoolean may_read)
{ tKey key;
  if (inputbuf_count > 0)
  { tInputbufIndex count;
    return_key_from_buffer:
    key = inputbuf[0];
    inputbuf_count--;
    for (count = 0; count < inputbuf_count; count++) /* IMPROVEME! */
      inputbuf[count] = inputbuf[count + 1];
  }
  else if ( (fragmentbuf_count > 0) && (time_is_over()) )
  { fragmentbuf_flush(); goto return_key_from_buffer; }
  else if (may_read)
  { char ch;
    if (my_read(fd_keyboard_input, &ch, 1) != 1) /* IMPROVEME? */
      fatal_error(0, "bicurses getch() failed");
    handle_input(ch);
    if (inputbuf_count > 0) goto return_key_from_buffer;
    else key = ERR; /* nothing there yet, need more input */
  }
  else key = ERR;
  return(key);
}

int getch(void)
{ return(my_builtin_getch(truE));
}


/* refresh() */

static int refresh_param[10];
#define REFRESH_STACKSIZE (60)
static int refresh_stack[REFRESH_STACKSIZE];
static unsigned char refresh_stackused;

static tBoolean refresh_push(const int val)
{ if (refresh_stackused >= REFRESH_STACKSIZE) /* stack already full */
  { debugmsg("refresh: stack overflow\n"); return(falsE); }
  refresh_stack[refresh_stackused++] = val;
  return(truE);
}

static tBoolean refresh_pop(/*@out@*/ int* _val)
{ if (refresh_stackused <= 0) /* stack empty */
  { debugmsg("refresh: can't pop from empty stack\n"); return(falsE); }
  refresh_stackused--; *_val = refresh_stack[refresh_stackused];
  return(truE);
}

#define TFFAIL1(str) goto out
#define TFFAIL2(str) \
  do { debugmsg("tf: " str "\n"); retval = 2; goto out; } while (0)

static one_caller unsigned char try_formatted(const char** _format)
{ unsigned char retval = 1; /* 0=fine, 1=not competent, 2=error */
  static const char flaglist[] = "-+# ", speclist[] = "dsoxX";
  const char* format = (*_format) - 1, *flagpos, *specpos;
  unsigned char flags = 0, count;
  tBoolean found_colon;
  int width, prec, val;

  if (*format != ':') found_colon = falsE;
  else { format++; if (*format == '\0') TFFAIL2("zero1"); found_colon = truE; }
  while (1)
  { flagpos = my_strchr(flaglist, *format);
    if (flagpos == NULL) break;
    flags |= (1 << (flagpos - flaglist)); format++;
  }
  if (flags == 0) { if (found_colon) { TFFAIL2("fc"); /* bad format */ } }
  else if (*format == '\0') TFFAIL2("zero2"); /* bad format */

  width = prec = 0;
  if (my_isdigit(*format)) /* width given */
  { my_atoi(format, &width, &format, 99);
    if ( (width <= 0) || (width >= 50) ) TFFAIL2("w"); /* can't be serious */
    if (*format == '\0') TFFAIL2("zero3"); /* bad format */
    if (*format == '.') /* precision given */
    { format++;
      if (my_isdigit(*format))
      { my_atoi(format, &prec, &format, 99);
        if ( (prec < 0) || (prec >= 50) ) TFFAIL2("p"); /* can't be serious */
      }
      if (*format == '\0') TFFAIL2("zero4"); /* bad format */
    }
  }

  specpos = my_strchr(speclist, *format);
  if (specpos == NULL) TFFAIL1("spec");
  if (!refresh_pop(&val)) TFFAIL2("pop");

  { char fbuf[1024], *ftemp = fbuf;
    *ftemp++ = '%';
    if (flags != 0)
    { for (count = 0; count < 4 /* strlen(flaglist) */; count++)
      { if (flags & (1 << count)) *ftemp++ = flaglist[count]; }
    }
    if (width > 0)
    { ftemp += sprint_safe(ftemp, strPercd, width);
      if (prec > 0)
      { *ftemp++ = '.'; ftemp += sprint_safe(ftemp, strPercd, prec); }
    }
    *ftemp++ = ( (*specpos == 's') ? 'd' : *specpos ); *ftemp = '\0';
    sprint_safe(strbuf, fbuf, val);
  }

  format++; *_format = format; retval = 0;
  out:
  return(retval);
}

#undef TFFAIL1
#undef TFFAIL2

#define RTFAIL(str) do { debugmsg("tparm: " str "\n"); goto failed; } while (0)

#define rbuf_append(ch) \
  do \
  { if (rbuf_used >= sizeof(rbuf)) RTFAIL("app"); \
    rbuf[rbuf_used++] = (ch); \
  } while (0)

static void refresh_tparm(const char* format, int num_params)
/* general handler for parameterized strings */
{ int val, val1, val2, depth;
  const char* temp;
  char ch, rbuf[1024];
  unsigned short rbuf_used = 0;
  refresh_stackused = 0;

  /* "termcap compatibility hack" */
  val = num_params;
  while (val > 0)
  { val--;
    if (!refresh_push(refresh_param[val])) break; /* "can't happen" */
  }

  /* backgam^Wgo */
  loop:
  ch = *format++;
  if (ch == '\0') goto done;
  else if (ch != '%') { do_app: rbuf_append(ch); goto loop; }
  ch = *format++;
  switch (ch)
  { case '\0': RTFAIL("zero"); /*@notreached@*/ break; /* bad format */
    case 'p':
      ch = *format++;
      if ( (ch < '1') || (ch > '9') ) RTFAIL("p1");
      val = ch - '1';
      if (val >= num_params) RTFAIL("p2");
      if (!refresh_push(refresh_param[val])) RTFAIL("p3");
      break;
    case 'P':
      ch = *format++;
      if (!refresh_pop(&val)) RTFAIL("P1");
      if (my_islower(ch)) refresh_dynvar[ch - 'a'] = val;
      else if (my_isupper(ch)) refresh_statvar[ch - 'A'] = val;
      else RTFAIL("P2");
      break;
    case 'g':
      ch = *format++;
      if (my_islower(ch)) val = refresh_dynvar[ch - 'a'];
      else if (my_isupper(ch)) val = refresh_statvar[ch - 'A'];
      else RTFAIL("g1");
      if (!refresh_push(val)) RTFAIL("g2");
      break;
    case 'c':
      if (!refresh_pop(&val)) RTFAIL("c");
      if (val == 0) val = 0200; /* CHECKME! */
      ch = (char) val; goto do_app;
      /*@notreached@*/ break;
    case '\'':
      if ( ( (ch = *format++) == '\0' ) || (*format++ != '\'') ) RTFAIL("'");
      goto do_app; /*@notreached@*/ break;
    case 's': case 'l':
      if (!refresh_pop(&val)) RTFAIL("sl1");
      sprint_safe(strbuf, strPercd, val);
      if (ch == 'l') { if (!refresh_push(strlen(strbuf))) RTFAIL("sl2"); }
      else { rs: temp=strbuf; while ( (ch=*temp++) != '\0' ) rbuf_append(ch); }
      break;
    case 'i':
      if (num_params < 2) RTFAIL("i");
      refresh_param[0]++; refresh_param[1]++; break;
    case '+': case '-': case '*': case '/': case 'm': case '&': case '|':
    case '^': case '=': case '>': case '<': case 'A': case 'O':
      if ( (!refresh_pop(&val2)) || (!refresh_pop(&val1)) ) RTFAIL("a1");
      if ( (val2 == 0) && ( (ch == '/') || (ch == 'm') ) ) RTFAIL("a2");
      switch (ch)
      { case '+': val = val1 + val2; break;
        case '-': val = val1 - val2; break;
        case '*': val = val1 * val2; break;
        case '/': val = val1 / val2; break;
        case 'm': val = val1 % val2; break;
        case '&': val = val1 & val2; break;
        case '|': val = val1 | val2; break;
        case '^': val = val1 ^ val2; break;
        case '=': val = cond2bool(val1 == val2); break;
        case '>': val = cond2bool(val1 > val2); break;
        case '<': val = cond2bool(val1 < val2); break;
        case 'A': val = cond2bool(val1 && val2); break;
        case 'O': val = cond2bool(val1 || val2); break;
      }
      if (!refresh_push(val)) RTFAIL("a3");
      break;
    case '!': case '~':
      if (!refresh_pop(&val)) RTFAIL("!1");
      val = ( (ch == '!') ? (!val) : (~val) );
      if (!refresh_push(val)) RTFAIL("!2");
      break;
    case '{':
      if (!my_isdigit(*format)) RTFAIL("num1");
      my_atoi(format, &val, &format, MY_ATOI_INT_MAX);
      if (*format++ != '}') RTFAIL("num2");
      if (!refresh_push(val)) RTFAIL("num3");
      break;
    case '%': goto do_app; /*@notreached@*/ break;
    case '?': break; /* the start of a conditional construct; nothing to do */
    case 't': /* the "then" part of a conditional construct */
      if (!refresh_pop(&val)) RTFAIL("t1");
      if (!val) /* condition not true, skip the "then" part */
      { depth = 0;
        while (1)
        { ch = *format++;
          if (ch == '\0') goto done;
          else if (ch == '%')
          { ch = *format++;
            if (ch == '\0') RTFAIL("t2"); /* bad format */
            else if (ch == 'e') { if (depth <= 0) break; /* reached goal */ }
            else if (ch == ';')
            { if (depth > 0) depth--;
              else break; /* reached goal */
            }
            else if (ch == '?') depth++;
          }
        }
      }
      break;
    case 'e': /* the "else" part of a conditional construct */
      /* If we get here, we usually handled the "then" part of a conditional
         construct, so we'll have to skip the "else" part. */
      depth = 0;
      while (1)
      { ch = *format++;
        if (ch == '\0') goto done;
        else if (ch == '%')
        { ch = *format++;
          if (ch == '\0') RTFAIL("e"); /* bad format */
          else if (ch == ';')
          { if (depth > 0) depth--;
            else break; /* reached goal */
          }
          else if (ch == '?') depth++;
        }
      }
      break;
    case ';': break; /* the end of a conditional construct; nothing to do */
    default:
      switch (try_formatted(&format))
      { case 0: goto rs; /*@notreached@*/ break;
        case 1: goto do_app; /*@notreached@*/ break;
        default: RTFAIL("tf"); /*@notreached@*/ break;
      }
      /*@notreached@*/ break;
  }
  goto loop;
  done: rbuf_append('\0'); refreshbuf_append_str(rbuf);
  failed: {}
}

static void refresh_tparm2(const char* format, int val1, int val2)
{ refresh_param[0] = val1; refresh_param[1] = val2;
  refresh_tparm(format, 2);
}

static __my_inline void refresh_move(int x, int y)
{ refresh_tparm2(string_value[strvMove], y, x);
}

int refresh(void)
{ unsigned char* dirty_lines = stdscr->dirty_lines;
  const chtype* text = stdscr->text;
  const attr_t* attr = stdscr->attr;
  int c, l;
  for (l = 0; l < LINES; l++)
  { if (!my_bit_test(dirty_lines, l)) continue; /* this line is up-to-date */
    refresh_move(0, l); stdscr->update_cursor = truE;
    for (c = 0; c < ( (l < LINES - 1) ? (COLS) : (COLS - 1) ); c++)
    { const size_t idx = SAI(c, l);
      const attr_t a = attr[idx], desta = a & (~igntermattr);
      if (currtermattr == desta) goto app_ch; /* need not update attributes */
      if ( ( (currtermattr & A_REVERSE) && (!(desta & A_REVERSE)) ) ||
           ( (currtermattr & A_BOLD) && (!(desta & A_BOLD)) ) )
      { /* why-oh-why is there no strvExitReverse/strvExitBold... */
        refreshbuf_append_str(string_value[strvExitAttrs]);
        currtermattr = 0;
      }
      if ( (desta & A_REVERSE) && (!(currtermattr & A_REVERSE)) )
        refreshbuf_append_str(string_value[strvEnterReverse]);
      if ( (desta & A_BOLD) && (!(currtermattr & A_BOLD)) )
        refreshbuf_append_str(string_value[strvEnterBold]);
      if (currtermattr & A_UNDERLINE)
      { if (!(desta & A_UNDERLINE))
          refreshbuf_append_str(string_value[strvExitUnderline]);
      }
      else if (desta & A_UNDERLINE)
        refreshbuf_append_str(string_value[strvEnterUnderline]);
      currtermattr = desta;
      app_ch: refreshbuf_append_ch(text[idx]);
    }
  }
  if (stdscr->update_cursor)
  { refresh_move(stdscr->x, stdscr->y); stdscr->update_cursor = falsE; }
  my_memclr(dirty_lines, stdscr->dirty_lines_size);
  if (refreshbuf_used > 0) refreshbuf_flush();
  return(OK);
}
