/*
  tbmacs - an emacs-like editor powered by termbox2
  suckless, single source file, no C++, no curses.

  fixes applied 2026:
  - get_buffer_index_from_cursor: logical lines only (no screen-wrap tracking)
  - insert_tab: memmove moved outside loop
  - find_file: resets cursor and scroll on file open
  - kill_to_end_of_line: removed spurious backspace_join_line / go_to_bol calls
  - minibuffer C-x C-f: fixed strcat/leak, length kept in sync
  - minibuffer kill-line: dispatched via pending flag so buffer/length are in scope
  - status_msg: static buffer, no more malloc/leak
  - removed duplicate tb_clear() in redraw_screen
  - removed screen-wrap cursor bump in character insert handler
  - length / used_length consolidated to single global: length
*/

#define TB_IMPL
#define _XOPEN_SOURCE 700

#include "termbox2.h"
#include "refresh.h"
#ifdef MENU_BAR_MODE
#include "menubar.h"
#else
/* Command actions remain available in a lean build; only the menu UI and its
 * state are omitted.  Keep these values aligned with menubar.h. */
typedef enum {
  MENU_ACTION_NONE,
  MENU_ACTION_NEW_BUFFER, MENU_ACTION_OPEN_FILE, MENU_ACTION_OPEN_DIR,
  MENU_ACTION_SAVE, MENU_ACTION_SAVE_AS, MENU_ACTION_KILL_BUFFER,
  MENU_ACTION_QUIT,
  MENU_ACTION_UNDO, MENU_ACTION_COPY, MENU_ACTION_CUT, MENU_ACTION_PASTE,
  MENU_ACTION_SELECT_ALL, MENU_ACTION_KILL_LINE, MENU_ACTION_DELETE_CHAR,
  MENU_ACTION_SEARCH_FORWARD, MENU_ACTION_SEARCH_BACKWARD,
  MENU_ACTION_QUERY_REPLACE,
  MENU_ACTION_TEXT_MODE, MENU_ACTION_C_MODE, MENU_ACTION_PYTHON_MODE,
  MENU_ACTION_LUA_MODE, MENU_ACTION_SH_MODE, MENU_ACTION_MAKEFILE_MODE,
  MENU_ACTION_ROFF_MODE, MENU_ACTION_MARKDOWN_MODE, MENU_ACTION_ORG_MODE,
  MENU_ACTION_JAVASCRIPT_MODE, MENU_ACTION_RUST_MODE, MENU_ACTION_PASCAL_MODE,
  MENU_ACTION_ASSEMBLY_MODE, MENU_ACTION_JAVA_MODE, MENU_ACTION_GO_MODE,
  MENU_ACTION_CSHARP_MODE, MENU_ACTION_SQL_MODE, MENU_ACTION_JSON_MODE,
  MENU_ACTION_HTML_MODE, MENU_ACTION_CSS_MODE, MENU_ACTION_LISP_MODE,
  MENU_ACTION_LIST_BUFFERS, MENU_ACTION_NEXT_BUFFER,
  MENU_ACTION_SOFT_WRAP, MENU_ACTION_SINGLE_QUOTES, MENU_ACTION_NANO_MODE,
  MENU_ACTION_HELP
} menu_action_t;
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

/*#define BUFFER_SIZE    65536*/
#define BUFFER_SIZE (1024 * 256)
#define D_FOREGROUND   7   /* 256-color white   */
#define D_BACKGROUND   0   /* 256-color black   */
#define CURSOR_BG      2   /* 256-color green   */
#define STATUS_FG      (7 | TB_BOLD)
#define STATUS_BG      237 /* 256-color dark grey; 4=blue fallback */
#define TAB_SPACE_NUM  3
#define MINIBUFFER_MAX 128
#define KILL_RING_MAX  4096
#define WRAP_COL 200  /* max line length before truncation */
#define FILL_COL 78
/* #define CONFIRM_QUIT */   /* if defined: prompt before quitting unsaved buffers */
#define MAX_OPEN_BUFFERS 32

/* -- major modes ----------------------------------------------------------- */

typedef struct {
  const char **keywords;
  int          keyword_count;
  uintattr_t   keyword_color;
} major_mode_t;

/* -------keywords for mods ------------------------------*/
static const char *c_keywords[] = {
  "auto", "break", "case", "char", "const", "continue", "default",
  "do", "double", "else", "enum", "extern", "float", "for", "goto",
  "if", "inline", "int", "long", "register", "restrict", "return",
  "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
  "union", "unsigned", "void", "volatile", "while",
  /* common preprocessor */
  "#define", "#include", "#ifdef", "#ifndef", "#endif", "#pragma"
};

static const char *lua_keywords[] = {
  "and", "break", "do", "else", "elseif", "end",
  "false", "for", "function", "goto", "if", "in",
  "local", "nil", "not", "or", "repeat", "return",
  "then", "true", "until", "while"
};

static const char *python_keywords[] = {
  "False", "None", "True", "and", "as", "assert", "async", "await",
  "break", "class", "continue", "def", "del", "elif", "else", "except",
  "finally", "for", "from", "global", "if", "import", "in", "is",
  "lambda", "nonlocal", "not", "or", "pass", "raise", "return",
  "try", "while", "with", "yield"
};

static const char *lisp_keywords[] = {
  "and", "begin", "case", "cond", "define", "defmacro", "defun", "do",
  "else", "if", "lambda", "let", "let*", "letrec", "nil", "or", "progn",
  "quote", "set!", "setq", "t"
};

static const char *sh_keywords[] = {
  /* control flow */
  "if", "then", "else", "elif", "fi", "for", "while", "until",
  "do", "done", "case", "esac", "in", "select",
  /* functions and scope */
  "function", "return", "local", "readonly",
  /* execution */
  "exec", "eval", "exit", "trap", "wait",
  /* environment */
  "export", "unset", "shift", "set",
  /* builtins you actually use */
  "echo", "printf", "read", "source", "alias", "unalias",
  "cd", "pwd", "pushd", "popd",
  "true", "false", "test", "break", "continue",
  /* file ops */
  "mkdir", "rm", "mv", "cp", "touch", "chmod", "chown",
  /* common but not technically builtins -- highlight anyway */
  "grep", "sed", "awk", "cut", "sort", "find", "xargs", "cat"
};


static const char *makefile_keywords[] = {
  "ifeq", "ifneq", "ifdef", "ifndef",
  "else", "endif",
  "include", "sinclude",
  "define", "endef",
  "override", "export", "unexport",
  "vpath",
};

static const major_mode_t makefile_mode = {
  makefile_keywords,
  sizeof(makefile_keywords) / sizeof(makefile_keywords[0]),
  2  /* green */
};

static const char *roff_keywords[] = {0}; /* macros handled by line prefix */

static const major_mode_t roff_mode = {
  roff_keywords,
  0,
  6  /* cyan */
};

/* markdown mode - minimal: just highlight structure characters */
static const char *markdown_keywords[] = {
  "#", "##", "###", "####", "#####", "######",  /* headers */
  "-", "*", "+",  /* lists */
  ">",  /* blockquotes */
  "`", "```",  /* code */
  "[", "]", "!", "(", ")"  /* links and images */
};

static const major_mode_t markdown_mode = {
  markdown_keywords,
  sizeof(markdown_keywords) / sizeof(markdown_keywords[0]),
  5  /* magenta */
};

/* org mode - minimal: highlight structure */
static const char *org_keywords[] = {
  "*", "**", "***", "****", "*****", "******",  /* headers */
  "+", "-",  /* lists */
  "TODO", "DONE", "SCHEDULED", "DEADLINE",  /* keywords */
  "CLOSED", "CANCELLED",
  "[ ]", "[x]", "[X]",  /* checkboxes */
  ":", ":END:",  /* properties */
  "#+", "#+BEGIN", "#+END"  /* directives */
};

static const major_mode_t org_mode = {
  org_keywords,
  sizeof(org_keywords) / sizeof(org_keywords[0]),
  3  /* yellow - org is golden/yellow */
};

static const major_mode_t c_mode = {
  c_keywords,
  sizeof(c_keywords) / sizeof(c_keywords[0]),
  6  /* cyan */
};

static const major_mode_t lua_mode = {
  lua_keywords,
  sizeof(lua_keywords) / sizeof(lua_keywords[0]),
  6  /* cyan */
};

static const major_mode_t python_mode = {
  python_keywords,
  sizeof(python_keywords) / sizeof(python_keywords[0]),
  3  /* yellow -- pythons are yellow. fight me. */
};

static const major_mode_t lisp_mode = {
  lisp_keywords,
  sizeof(lisp_keywords) / sizeof(lisp_keywords[0]),
  2  /* green */
};

static const major_mode_t sh_mode = {
  sh_keywords,
  sizeof(sh_keywords) / sizeof(sh_keywords[0]),
  1  /* red -- shell scripts are dangerous */
};

#include "extended_modes.h"

/* NULL mode = plain text, no highlighting */
static const major_mode_t *current_mode = NULL;
static int highlight_single_quotes = 0;

static int mode_has_c_comments(const major_mode_t *m) {
  return m == &c_mode || m == &javascript_mode || m == &rust_mode ||
         m == &java_mode || m == &go_mode || m == &csharp_mode ||
         m == &css_mode || m == &sql_mode;
}

static int mode_has_slash_comments(const major_mode_t *m) {
  return m == &c_mode || m == &javascript_mode || m == &rust_mode ||
         m == &pascal_mode || m == &java_mode || m == &go_mode ||
         m == &csharp_mode || m == &css_mode;
}

static int mode_keywords_ignore_case(const major_mode_t *m) {
  return m == &pascal_mode || m == &assembly_mode || m == &sql_mode ||
         m == &html_mode;
}

#define IS_STRING_QUOTE(ch) \
  ((ch) == '"' || (highlight_single_quotes && (ch) == '\''))

void detect_major_mode(const char *filename) {
  const char *base = strrchr(filename, '/');
  base = base ? base + 1 : filename;
  if (!strcmp(base, "Makefile") || !strcmp(base, "GNUmakefile") ||
      !strcmp(base, "makefile")) {
    current_mode = &makefile_mode;
    return;
  }

  const char *dot = strrchr(filename, '.');
  if (!dot) { current_mode = NULL; return; }

  if (!strcmp(dot, ".mk") || !strcmp(dot, ".make")) {
    current_mode = &makefile_mode;
  }
  else if (!strcmp(dot, ".c") || !strcmp(dot, ".h")) {
    current_mode = &c_mode;
  }
  else if (!strcmp(dot, ".lua")) {
    current_mode = &lua_mode;
  }
  else if (!strcmp(dot, ".py")) {
    current_mode = &python_mode;
  }
  else if (!strcmp(dot, ".lisp") || !strcmp(dot, ".lsp") ||
           !strcmp(dot, ".scm") || !strcmp(dot, ".ss") ||
           !strcmp(dot, ".rkt") || !strcmp(dot, ".cl") ||
           !strcmp(dot, ".el")) {
    current_mode = &lisp_mode;
  }
  else if (!strcmp(dot, ".sh") || !strcmp(dot, ".bash") ||
	   !strcmp(dot, ".zsh") || !strcmp(dot, ".ksh")) {
    current_mode = &sh_mode;
  }
  else if (!strcmp(dot, ".1") || !strcmp(dot, ".2") || !strcmp(dot, ".3") ||
	   !strcmp(dot, ".4") || !strcmp(dot, ".5") || !strcmp(dot, ".6") ||
	   !strcmp(dot, ".7") || !strcmp(dot, ".8") || !strcmp(dot, ".9") ||
	   !strcmp(dot, ".roff") || !strcmp(dot, ".groff") ||
	   !strcmp(dot, ".ms") || !strcmp(dot, ".me") || !strcmp(dot, ".mm") ||
	   !strcmp(dot, ".mom") || !strcmp(dot, ".man") || !strcmp(dot, ".mdoc")) {
    current_mode = &roff_mode;
  }
  else if (!strcmp(dot, ".md") || !strcmp(dot, ".markdown")) {
    current_mode = &markdown_mode;
  }
  else if (!strcmp(dot, ".org")) {
    current_mode = &org_mode;
  }
  else if (!strcmp(dot, ".js") || !strcmp(dot, ".mjs") ||
           !strcmp(dot, ".cjs") || !strcmp(dot, ".jsx") ||
           !strcmp(dot, ".ts") || !strcmp(dot, ".tsx"))
    current_mode = &javascript_mode;
  else if (!strcmp(dot, ".rs")) current_mode = &rust_mode;
  else if (!strcmp(dot, ".pas") || !strcmp(dot, ".pp") ||
           !strcmp(dot, ".dpr") || !strcmp(dot, ".lpr"))
    current_mode = &pascal_mode;
  else if (!strcmp(dot, ".asm") || !strcmp(dot, ".s") || !strcmp(dot, ".S"))
    current_mode = &assembly_mode;
  else if (!strcmp(dot, ".java")) current_mode = &java_mode;
  else if (!strcmp(dot, ".go")) current_mode = &go_mode;
  else if (!strcmp(dot, ".cs")) current_mode = &csharp_mode;
  else if (!strcmp(dot, ".sql")) current_mode = &sql_mode;
  else if (!strcmp(dot, ".json") || !strcmp(dot, ".jsonl")) current_mode = &json_mode;
  else if (!strcmp(dot, ".html") || !strcmp(dot, ".htm") ||
           !strcmp(dot, ".xml") || !strcmp(dot, ".svg")) current_mode = &html_mode;
  else if (!strcmp(dot, ".css") || !strcmp(dot, ".scss") ||
           !strcmp(dot, ".sass") || !strcmp(dot, ".less")) current_mode = &css_mode;
  /* else if (!strcmp(dot, ".py")) current_mode = &python_mode; */
  else {
    current_mode = NULL;
  }
}

/* -- globals --------------------------------------------------------------- */

typedef struct {
  char name[256];
  long size;
  int  lines;
} file_info_t;

file_info_t fileref;

int    cursor_x      = 0;
int    cursor_y      = 0;
int    scroll_offset = 0;
int    scroll_x = 0;

/* Soft wrap (M-x soft-wrap-mode).  Display only: the buffer, `length` and the
 * cursor model are untouched -- cursor_y still counts logical newlines.  A
 * logical line simply paints across as many screen rows as it needs, broken
 * after the last space that fits.  No continuation glyph is drawn.
 * scroll_row is how many wrapped rows of the topmost logical line are hidden
 * above the window; it lets a single line longer than the screen scroll.  It
 * is meaningless (and always 0) when soft_wrap is off. */
int    soft_wrap  = 0;
int    scroll_row = 0;

int paren_match_pos  = -1; /* buffer index of matching paren, -1 = none */
int paren_cursor_pos = -1; /* buffer index of paren under cursor */

int    minibuffer_active      = 0;
int    search_forward_active  = 0;
char   minibuffer_input[MINIBUFFER_MAX] = {0};
int    minibuffer_length = 0;
char   in_string = 0; /* tracks whether we are inside ' or " */
char   in_atag = 0;   /* tracks if we are in <> */
int    in_comment = 0; /* 0=none, 1=in block comment */

size_t length = 0;   /* single authoritative buffer length */

int mark_active = 0;
size_t mark_pos = 0;  /* buffer index, not cursor_x/y */

static char   kill_ring[KILL_RING_MAX];
static size_t kill_ring_len = 0;

static char  status_buf[256] = {0}; /* written only via set_status() */
static char *status_msg      = NULL; /* NULL = no message; points into status_buf */
static int   status_ttl      = 0;   /* keypresses until status_msg is cleared */

static int  modified      = 0; /* current buffer has unsaved changes */
static int  confirm_quit  = 0; /* waiting y/n for quit-without-save prompt */
static int  minibuffer_cursor = 0; /* insert point within minibuffer_input */

int saved_cursor_x = 0;
int saved_cursor_y = 0;

/* -- search state ---------------------------------------------------------- */
static char   search_term[MINIBUFFER_MAX] = {0};
static int    search_match_pos = -1;
static size_t search_match_len = 0;
static int    search_backward_active = 0;

/* -- replace state --------------------------------------------------------- */
static int  replace_state     = 0; /* 0=off 1=enter search 2=enter replace 3=confirm */
static char replace_search[MINIBUFFER_MAX] = {0};
static char replace_with[MINIBUFFER_MAX]   = {0};
static int  replace_match_pos = -1;

/* -- undo ring ------------------------------------------------------------- */
#define UNDO_DEPTH 64

typedef struct {
  char  *data;
  size_t len;
  int    cx, cy;
} undo_state_t;

static undo_state_t undo_ring[UNDO_DEPTH];
static int          undo_top   = -1;
static int          undo_count = 0;

/* -- multi-buffer ---------------------------------------------------------- */

typedef struct {
  char               *data;
  size_t              length;
  file_info_t         fileref;
  int                 cursor_x, cursor_y;
  int                 scroll_offset, scroll_x, scroll_row;
  int                 mark_active;
  size_t              mark_pos;
  const major_mode_t *mode;
  undo_state_t        undo[UNDO_DEPTH]; /* each .data owned by this slot */
  int                 undo_top;
  int                 undo_count;
  int                 modified;
} buf_slot_t;

static buf_slot_t buf_slots[MAX_OPEN_BUFFERS];
static int        current_buf = 0;
static int        num_bufs    = 1;

/* -- status helper --------------------------------------------------------- */

static void set_status(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(status_buf, sizeof(status_buf), fmt, ap);
  va_end(ap);
  status_msg = status_buf;
  status_ttl = 5;
}

/* -- forward declarations -------------------------------------------------- */

void   draw_status_bar(const char *, const char *, size_t);
void   draw_minibuffer(void);
void   redraw_screen(const char *, size_t, const char *);
void   force_redraw(const char *, size_t, const char *);
size_t get_buffer_index_from_cursor(const char *, size_t);
void   quit_with_save(const char *, const char *, size_t);
void   quit_no_save(const char *);
void   kill_to_end_of_line(char *, size_t *);
int    save_to_file(const char *, const char *, size_t);
int    get_line_length(const char *, size_t, int);
void   go_to_bol(const char *, size_t);
void   go_to_eol(const char *, size_t);
void   clamp_cursor(const char *, size_t);
int    find_file(char *, char *, char *);
static void dired_enter(const char *);
static void draw_dired(void);
/* Both defined in dired.h, which is included further down the file; this
 * tentative definition lets redraw_screen() ask whether the browser owns the
 * screen without moving the include. */
static int  dired_active;
void   insert_tab(char *, size_t, size_t *);
int    is_paren(char);
int countlines_buf(const char *, size_t);
char   get_buffer_char_at_cursor(const char *, size_t);

const char *brace_type_of_char(char c);
int         find_matchingparen(const char *, int, const char *);
void        update_paren_match(const char *, size_t);
void detect_major_mode(const char *filename);
void fill_paragraph(char *buffer, size_t *len);
static void set_cursor_from_index(const char *, size_t);
void move_forward_word(const char *, size_t);
void move_backward_word(const char *, size_t);
void delete_forward_word(char *, size_t *);
void delete_backward_word(char *, size_t *);
void delete_char(char *, size_t *);
void yank(char *, size_t, size_t *);
int  find_next_match(const char *, size_t, const char *, size_t);
int  find_prev_match(const char *, size_t, const char *, size_t);
void follow_link_at_cursor(const char *, size_t, char **, char *);
void hungry_delete_backward(char *, size_t *);
void undo_push(const char *, size_t);
int  undo_pop(char *, size_t *);
void kill_region(char *, size_t *);
void copy_region(const char *, size_t);
static int  buf_alloc(int);
static void buf_save_to_slot(int);
static void buf_load_from_slot(int, char **, char *);
static void buf_kill(int, char **, char *);
static int  open_in_new_buf(char *, char **, char *);
static void open_help_buffer(char **, char *);

#include "autocomplete.h"



/* -- helpers --------------------------------------------------------------- */

void fill_paragraph(char *buffer, size_t *len) {
  size_t idx = get_buffer_index_from_cursor(buffer, *len);

  /* find start of paragraph -- walk back to blank line or BOF */
  size_t para_start = idx;
  while (para_start > 0) {
    /* check if previous line is blank */
    size_t sol = para_start - 1;
    while (sol > 0 && buffer[sol - 1] != '\n') sol--;
    int blank = 1;
    for (size_t k = sol; k < para_start - 1; k++) {
      if (buffer[k] != ' ' && buffer[k] != '\t') { blank = 0; break; }
    }
    if (blank) break;
    para_start = sol;
  }

  /* find end of paragraph -- walk forward to blank line or EOF */
  size_t para_end = idx;
  while (para_end < *len) {
    /* find end of this line */
    size_t eol = para_end;
    while (eol < *len && buffer[eol] != '\n') eol++;
    if (eol >= *len) { para_end = eol; break; }
    /* check if next line is blank */
    size_t next_sol = eol + 1;
    size_t next_eol = next_sol;
    while (next_eol < *len && buffer[next_eol] != '\n') next_eol++;
    int blank = 1;
    for (size_t k = next_sol; k < next_eol; k++) {
      if (buffer[k] != ' ' && buffer[k] != '\t') { blank = 0; break; }
    }
    if (blank) { para_end = eol; break; }
    para_end = next_eol;
  }

  if (para_start >= para_end) return;
  size_t para_len = para_end - para_start;

  /* extract paragraph into temp buffer, collapse internal newlines to spaces */
  char *tmp = malloc(para_len + 1);
  if (!tmp) return;
  size_t tmp_len = 0;
  for (size_t i = para_start; i < para_end; i++) {
    if (buffer[i] == '\n') {
      /* collapse newline to space, skip multiple spaces */
      if (tmp_len > 0 && tmp[tmp_len - 1] != ' ')
        tmp[tmp_len++] = ' ';
    } else {
      tmp[tmp_len++] = buffer[i];
    }
  }
  /* trim trailing space */
  while (tmp_len > 0 && tmp[tmp_len - 1] == ' ') tmp_len--;
  tmp[tmp_len] = '\0';

  /* re-wrap at FILL_COL -- insert newlines at word boundaries */
  char *wrapped = malloc(tmp_len * 2 + 1);
  if (!wrapped) { free(tmp); return; }
  size_t w     = 0;
  size_t col   = 0;
  size_t i     = 0;

  while (i < tmp_len) {
    /* find end of next word */
    size_t word_start = i;
    while (i < tmp_len && tmp[i] != ' ') i++;
    size_t word_len = i - word_start;

    /* skip spaces between words */
    while (i < tmp_len && tmp[i] == ' ') i++;

    if (col == 0) {
      /* first word on line -- always fits */
      memcpy(wrapped + w, tmp + word_start, word_len);
      w   += word_len;
      col += word_len;
    } else if (col + 1 + word_len <= FILL_COL) {
      /* word fits on current line */
      wrapped[w++] = ' ';
      memcpy(wrapped + w, tmp + word_start, word_len);
      w   += word_len;
      col += 1 + word_len;
    } else {
      /* word doesn't fit -- wrap */
      wrapped[w++] = '\n';
      memcpy(wrapped + w, tmp + word_start, word_len);
      w   += word_len;
      col  = word_len;
    }
  }
  wrapped[w] = '\0';

  /* splice wrapped text back into buffer */
  size_t new_para_len = w;
  long   delta        = (long)new_para_len - (long)para_len;

  if (*len + delta >= BUFFER_SIZE) { free(tmp); free(wrapped); return; }

  memmove(buffer + para_start + new_para_len,
          buffer + para_end,
          *len - para_end);
  memcpy(buffer + para_start, wrapped, new_para_len);
  *len += delta;
  buffer[*len] = '\0';

  fileref.lines = countlines_buf(buffer, *len);
  clamp_cursor(buffer, *len);

  free(tmp);
  free(wrapped);
}

char *trim_spaces(char *str) {
  while (isspace((unsigned char)*str)) str++;
  if (*str == 0) return str;
  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;
  *(end + 1) = '\0';
  return str;
}

int countlines_buf(const char *buf, size_t len) {
  /* count logical lines directly from buffer — no file I/O needed */
  int lines = 1;
  for (size_t i = 0; i < len; i++)
    if (buf[i] == '\n') lines++;
  return lines;
}

/* -- file operations ------------------------------------------------------- */

int find_file(char *newfilename, char *buff, char *currentfilename) {
  const char *prefix     = "find-file";
  size_t      prefix_len = strlen(prefix);

  if (strncmp(newfilename, prefix, prefix_len) == 0)
    newfilename += prefix_len;
  newfilename = trim_spaces(newfilename);

  strcpy(currentfilename, newfilename);
  strcpy(fileref.name, newfilename);

  { struct stat _ds; if (stat(newfilename, &_ds) == 0 && S_ISDIR(_ds.st_mode)) {
      dired_enter(newfilename); return -1; } }

  memset(buff, 0, BUFFER_SIZE);

  FILE *f = fopen(newfilename, "r");
  if (!f) return -1;

  size_t n = fread(buff, 1, BUFFER_SIZE - 1, f);
  buff[n] = '\0';
  fclose(f);

  /* BUG-10 fix: warn if file was larger than the buffer */
  if (n >= BUFFER_SIZE - 1)
    set_status("WARNING: file truncated at %zu bytes!", n);

  /* reset cursor and scroll so we don't land out-of-bounds */
  length        = n;
  cursor_x      = 0;
  cursor_y      = 0;
  scroll_offset = 0;
  scroll_x      = 0;
  scroll_row    = 0;
  modified      = 0;
  fileref.lines = countlines_buf(buff, n);
  detect_major_mode(currentfilename);

  tb_clear();
  return 0;
}

/* Returns 0 on success, -1 if the file could not be written.  A caller that
 * reports "Saved" without looking loses the one chance to tell the user their
 * work is still only in memory -- a read-only file or a full disk failed
 * silently before. */
int save_to_file(const char *filename, const char *buffer, size_t len) {
  FILE *fp = fopen(filename, "w");
  if (!fp) return -1;

  size_t n = fwrite(buffer, sizeof(char), len, fp);
  if (fclose(fp) != 0 || n != len) return -1;

  modified = 0;
  return 0;
}

/* -- cursor model: logical lines only ------------------------------------- */

/*
 * Walk the buffer counting newlines for y and characters for x.
 * Screen width is NOT considered — wrapping is a display concern only.
 * This must agree with get_line_length() which also only counts to \n.
 */
size_t get_buffer_index_from_cursor(const char *buffer, size_t len) {
  int    x = 0, y = 0;
  size_t i = 0;

  while (i < len) {
    if (y == cursor_y && x == cursor_x)
      return i;

    if (buffer[i] == '\n') {
      /* if cursor_x is past EOL on this logical line, clamp to the newline */
      if (y == cursor_y)
        return i;
      x = 0;
      y++;
    } else {
      x++;
    }
    i++;
  }
  return len; /* cursor at or past end of buffer */
}

int get_line_length(const char *buffer, size_t len, int target_y) {
  int x = 0, y = 0;
  for (size_t i = 0; i < len; ++i) {
    if (y == target_y) {
      if (buffer[i] == '\n') return x;
      x++;
    }
    if (buffer[i] == '\n') { y++; x = 0; }
  }
  return (y == target_y) ? x : 0;
}

char get_buffer_char_at_cursor(const char *buffer, size_t len) {
  size_t idx = get_buffer_index_from_cursor(buffer, len);
  if (idx >= len) return '\0';
  return buffer[idx];
}

/* -- soft wrap geometry ----------------------------------------------------
 * Every wrap calculation in the editor goes through wrap_break(), so the
 * renderer and the cursor/scroll arithmetic can never disagree about where a
 * line folds.
 */

/* Buffer index where logical line `y` starts. */
size_t line_start_index(const char *buffer, size_t len, int y) {
  size_t i = 0;
  int    cur = 0;
  while (cur < y && i < len) {
    if (buffer[i] == '\n') cur++;
    i++;
  }
  return i;
}

/* Within a logical line of `ll` chars starting at buffer index `ls`: given a
 * screen row that begins at column `off`, return the column where that row
 * ends and the next one begins.  The row keeps the space it breaks after; a
 * word longer than the screen is broken hard at the right edge. */
int wrap_break(const char *buffer, size_t ls, int ll, int off, int width) {
  if (width < 1)         return ll;
  if (ll - off <= width) return ll;                  /* rest of the line fits */
  for (int k = off + width; k > off; k--) {          /* scan back for a space */
    char c = buffer[ls + k - 1];
    if (c == ' ' || c == '\t') return k;
  }
  return off + width;                                /* unbreakable word */
}

/* Screen rows a logical line of `ll` chars at index `ls` folds into.
 * A line whose last row is exactly `width` columns wide gets one extra, blank
 * row: with no continuation glyph there is otherwise nowhere on screen to put
 * the cursor when it sits at end-of-line. */
int wrap_rows_of(const char *buffer, size_t ls, int ll, int width) {
  if (width < 1) return 1;
  int rows = 0, off = 0;
  for (;;) {
    int next = wrap_break(buffer, ls, ll, off, width);
    rows++;
    if (next >= ll) {
      if (next - off == width) rows++;               /* trailing blank row */
      break;
    }
    off = next;
  }
  return rows;
}

/* Screen rows logical line `y` occupies.  1 when soft wrap is off. */
int wrap_rows_for_line(const char *buffer, size_t len, int y, int width) {
  if (!soft_wrap) return 1;
  return wrap_rows_of(buffer, line_start_index(buffer, len, y),
                      get_line_length(buffer, len, y), width);
}

/* Screen rows occupied by logical lines [from, to).  Walks the buffer once so
 * a whole screenful costs one pass, not one line_start_index() per line. */
int wrap_rows_between(const char *buffer, size_t len, int from, int to, int width) {
  if (to <= from) return 0;
  if (!soft_wrap) return to - from;

  size_t i    = line_start_index(buffer, len, from);
  int    rows = 0;
  for (int y = from; y < to; y++) {
    size_t ls = i;
    while (i < len && buffer[i] != '\n') i++;
    rows += wrap_rows_of(buffer, ls, (int)(i - ls), width);
    if (i >= len) break;                     /* last line has no newline */
    i++;                                     /* step over the newline */
  }
  return rows;
}

/* Row (relative to the first row of its own logical line) and column at which
 * the cursor is drawn under soft wrap. */
void wrap_cursor_rowcol(const char *buffer, size_t len, int width,
                        int *row, int *col) {
  *row = 0;
  *col = cursor_x;
  if (!soft_wrap || width < 1) return;

  size_t ls  = line_start_index(buffer, len, cursor_y);
  int    ll  = get_line_length(buffer, len, cursor_y);
  int    off = 0;
  while (off < ll) {
    int next = wrap_break(buffer, ls, ll, off, width);
    if (cursor_x < next || next >= ll) break;   /* on this row, or it's the last */
    off = next;
    (*row)++;
  }
  *col = cursor_x - off;
  if (*col == width) { (*row)++; *col = 0; }    /* the trailing blank row */
}

/* Column at which wrapped row `r` of logical line `y` begins.  Used to park
 * the cursor on the row a page-scroll just brought to the top of the window. */
int wrap_row_start_col(const char *buffer, size_t len, int y, int r, int width) {
  if (!soft_wrap || width < 1) return 0;
  size_t ls  = line_start_index(buffer, len, y);
  int    ll  = get_line_length(buffer, len, y);
  int    off = 0;
  for (int k = 0; k < r; k++) {
    int next = wrap_break(buffer, ls, ll, off, width);
    off = next;                     /* the trailing blank row starts at `ll` */
    if (next >= ll) break;
  }
  return off;
}

/* Move the top of the window forward/backward by `n` screen rows. */
void wrap_scroll_forward(const char *buffer, size_t len, int n, int width) {
  int last = countlines_buf(buffer, len) - 1;
  while (n > 0) {
    int rows  = wrap_rows_for_line(buffer, len, scroll_offset, width);
    int avail = rows - scroll_row - 1;     /* rows of this line below the top */
    if (n <= avail) { scroll_row += n; return; }
    if (scroll_offset >= last) {
      scroll_row += avail;                 /* last line: stop at its last row */
      return;
    }
    n -= avail + 1;
    scroll_offset++;
    scroll_row = 0;
  }
}

void wrap_scroll_backward(const char *buffer, size_t len, int n, int width) {
  while (n > 0) {
    if (scroll_row > 0) { scroll_row--; n--; continue; }
    if (scroll_offset == 0) return;
    scroll_offset--;
    scroll_row = wrap_rows_for_line(buffer, len, scroll_offset, width) - 1;
    n--;
  }
}

void clamp_cursor(const char *buffer, size_t len) {
  /* BUG-2 fix: clamp cursor_y to valid line range first */
  int max_lines = countlines_buf(buffer, len);
  if (max_lines < 1) max_lines = 1;
  if (cursor_y < 0)            cursor_y = 0;
  if (cursor_y >= max_lines)   cursor_y = max_lines - 1;

  int max_x = get_line_length(buffer, len, cursor_y);
  if (max_x < 0) max_x = 0;
  if (cursor_x > max_x) cursor_x = max_x;
  if (cursor_x < 0)     cursor_x = 0;

  int max_y = tb_height() - 2;
  int sw    = tb_width();

  if (soft_wrap) {
    /* Vertical auto-scroll counted in screen rows, not logical lines: one
     * wrapped line can fill several rows, so "cursor_y - scroll_offset" says
     * nothing about whether the cursor is on screen. */
    int crow, ccol;
    wrap_cursor_rowcol(buffer, len, sw, &crow, &ccol);

    if (cursor_y < scroll_offset || cursor_y - scroll_offset > max_y) {
      scroll_offset = cursor_y;   /* far outside the window: jump to it */
      scroll_row    = 0;
    }
    if (cursor_y == scroll_offset && crow < scroll_row) scroll_row = crow;

    int rows = wrap_rows_between(buffer, len, scroll_offset, cursor_y, sw)
             - scroll_row + crow + 1;
    int top_rows = -1;
    while (rows > max_y) {
      if (top_rows < 0)
        top_rows = wrap_rows_for_line(buffer, len, scroll_offset, sw);
      if (scroll_row + 1 < top_rows) scroll_row++;
      else { scroll_offset++; scroll_row = 0; top_rows = -1; }
      rows--;
    }
    scroll_x = 0;               /* nothing is ever off the right edge */
  } else {
    if (cursor_y < scroll_offset)
      scroll_offset = cursor_y;
    else if (cursor_y >= scroll_offset + max_y)
      scroll_offset = cursor_y - max_y + 1;

    /* UX-5: horizontal auto-scroll to keep cursor visible */
    if (cursor_x < scroll_x) scroll_x = cursor_x;
    else if (cursor_x >= scroll_x + sw) scroll_x = cursor_x - sw + 1;
    if (scroll_x < 0) scroll_x = 0;
    scroll_row = 0;
  }
  if (scroll_offset < 0) scroll_offset = 0;
  if (scroll_row < 0)    scroll_row = 0;
}

/* -- movement -------------------------------------------------------------- */

void go_to_bol(const char *buffer, size_t len) { cursor_x = 0; }

void go_to_eol(const char *buffer, size_t len) {
  cursor_x = get_line_length(buffer, len, cursor_y);
}

void go_to_beginning_of_buffer(const char *buffer) {
  cursor_x = cursor_y = scroll_offset = scroll_row = 0;
}

void go_to_end_of_buffer(const char *buffer) {
  cursor_y = countlines_buf(buffer, length) - 1;
  cursor_x = get_line_length(buffer, length, cursor_y); /* UX-7: land at EOL */
  scroll_offset = cursor_y - (tb_height() - 3);
  if (scroll_offset < 0) scroll_offset = 0;
  scroll_row    = 0;
}

void go_to_line(const char *buffer, int line_number) {
  int total = countlines_buf(buffer, length); /* BUG-8 fix: live count */
  if (line_number < 1)      line_number = 1;
  if (line_number > total)  line_number = total;
  cursor_y      = line_number - 1; /* 0-based internally */
  scroll_offset = cursor_y > 0 ? cursor_y - 1 : 0;
  scroll_row    = 0;
}

/* -- editing --------------------------------------------------------------- */

/*
 * FIX: memmove is now called ONCE before the fill loop.
 * Old code called it inside the loop, shifting TAB_SPACE_NUM times too many.
 */
void insert_tab(char *buffer, size_t buffer_size, size_t *used_length) {
  if (*used_length + TAB_SPACE_NUM >= buffer_size) return;
  size_t insert_pos = get_buffer_index_from_cursor(buffer, *used_length);
  memmove(buffer + insert_pos + TAB_SPACE_NUM,
          buffer + insert_pos,
          *used_length - insert_pos);
  for (int i = 0; i < TAB_SPACE_NUM; i++)
    buffer[insert_pos + i] = ' ';
  cursor_x      += TAB_SPACE_NUM;
  *used_length  += TAB_SPACE_NUM;
  buffer[*used_length] = '\0';
}

/*
 * FIX: removed spurious backspace_join_line() and go_to_bol() calls.
 * Emacs C-k on a non-empty line kills to EOL, leaving the newline.
 * C-k on an empty line (cursor already at EOL) deletes the newline itself.
 */
void kill_to_end_of_line(char *buffer, size_t *len) {
  size_t start = get_buffer_index_from_cursor(buffer, *len);
  size_t end   = start;

  while (end < *len && buffer[end] != '\n') end++;

  if (start == end) {
    /* at EOL — eat the newline like Emacs does on second C-k */
    if (end < *len && buffer[end] == '\n') {
      kill_ring[0] = '\n'; kill_ring[1] = '\0'; kill_ring_len = 1;
      memmove(buffer + end, buffer + end + 1, *len - end - 1);
      (*len)--;
      buffer[*len] = '\0';
      fileref.lines--;
    }
  } else {
    /* save killed text to kill ring */
    size_t kill_len = end - start;
    if (kill_len < KILL_RING_MAX) {
      memcpy(kill_ring, buffer + start, kill_len);
      kill_ring_len = kill_len;
      kill_ring[kill_ring_len] = '\0';
    }
    /* kill from cursor to end of line, keep the newline */
    memmove(buffer + start, buffer + end, *len - end);
    *len -= kill_len;
    buffer[*len] = '\0';
  }
  /* cursor stays at same column (already at EOL after kill) */
}

void backspace_join_line(char *buffer, size_t *len) {
  if (cursor_y == 0 || cursor_x != 0) return;

  /* find the newline at end of previous line */
  size_t idx = get_buffer_index_from_cursor(buffer, *len);
  if (idx == 0) return;

  /* step back over the newline */
  size_t newline_pos = idx - 1;
  /* make sure it really is a newline */
  if (buffer[newline_pos] != '\n') return;

  /* new cursor_x = length of previous line */
  int prev_len = get_line_length(buffer, *len, cursor_y - 1);

  memmove(buffer + newline_pos,
          buffer + newline_pos + 1,
          *len - newline_pos - 1);
  (*len)--;
  buffer[*len] = '\0';

  cursor_y--;
  cursor_x = prev_len;
  fileref.lines--;
}

void delete_char(char *buffer, size_t *len) {
  size_t idx = get_buffer_index_from_cursor(buffer, *len);
  if (idx >= *len) return;
  memmove(buffer + idx, buffer + idx + 1, *len - idx - 1);
  (*len)--;
  buffer[*len] = '\0';
  fileref.lines = countlines_buf(buffer, *len);
}

/* -- word movement & kill ring --------------------------------------------- */

static void set_cursor_from_index(const char *buffer, size_t idx) {
  int x = 0, y = 0;
  for (size_t i = 0; i < idx; i++) {
    if (buffer[i] == '\n') { y++; x = 0; }
    else x++;
  }
  cursor_x = x;
  cursor_y = y;
}

void move_forward_word(const char *buffer, size_t len) {
  size_t idx = get_buffer_index_from_cursor(buffer, len);
  while (idx < len && !isalnum((unsigned char)buffer[idx]) && buffer[idx] != '_') idx++;
  while (idx < len &&  (isalnum((unsigned char)buffer[idx]) || buffer[idx] == '_')) idx++;
  set_cursor_from_index(buffer, idx);
}

void move_backward_word(const char *buffer, size_t len) {
  size_t idx = get_buffer_index_from_cursor(buffer, len);
  if (idx == 0) return;
  idx--;
  while (idx > 0 && !isalnum((unsigned char)buffer[idx]) && buffer[idx] != '_') idx--;
  while (idx > 0 &&  (isalnum((unsigned char)buffer[idx-1]) || buffer[idx-1] == '_')) idx--;
  set_cursor_from_index(buffer, idx);
}

void delete_forward_word(char *buffer, size_t *len) {
  size_t start = get_buffer_index_from_cursor(buffer, *len);
  size_t end   = start;
  while (end < *len && !isalnum((unsigned char)buffer[end]) && buffer[end] != '_') end++;
  while (end < *len &&  (isalnum((unsigned char)buffer[end]) || buffer[end] == '_')) end++;
  size_t kill_len = end - start;
  if (kill_len == 0) return;
  if (kill_len < KILL_RING_MAX) {
    memcpy(kill_ring, buffer + start, kill_len);
    kill_ring_len = kill_len;
    kill_ring[kill_ring_len] = '\0';
  }
  memmove(buffer + start, buffer + end, *len - end);
  *len -= kill_len;
  buffer[*len] = '\0';
  fileref.lines = countlines_buf(buffer, *len);
}

void delete_backward_word(char *buffer, size_t *len) {
  size_t end = get_buffer_index_from_cursor(buffer, *len);
  if (end == 0) return;
  size_t start = end - 1;
  while (start > 0 && !isalnum((unsigned char)buffer[start]) && buffer[start] != '_') start--;
  while (start > 0 &&  (isalnum((unsigned char)buffer[start-1]) || buffer[start-1] == '_')) start--;
  size_t kill_len = end - start;
  if (kill_len < KILL_RING_MAX) {
    memcpy(kill_ring, buffer + start, kill_len);
    kill_ring_len = kill_len;
    kill_ring[kill_ring_len] = '\0';
  }
  memmove(buffer + start, buffer + end, *len - end);
  *len -= kill_len;
  buffer[*len] = '\0';
  fileref.lines = countlines_buf(buffer, *len);
  set_cursor_from_index(buffer, start);
}

void yank(char *buffer, size_t buffer_size, size_t *len) {
  if (kill_ring_len == 0 || *len + kill_ring_len >= buffer_size) return;
  size_t insert_pos = get_buffer_index_from_cursor(buffer, *len);
  memmove(buffer + insert_pos + kill_ring_len,
          buffer + insert_pos,
          *len - insert_pos);
  memcpy(buffer + insert_pos, kill_ring, kill_ring_len);
  *len += kill_ring_len;
  buffer[*len] = '\0';
  set_cursor_from_index(buffer, insert_pos + kill_ring_len);
  fileref.lines = countlines_buf(buffer, *len);
}

void kill_region(char *buffer, size_t *len) {
  if (!mark_active) return;
  size_t cursor_idx = get_buffer_index_from_cursor(buffer, *len);
  size_t lo = (mark_pos <= cursor_idx) ? mark_pos : cursor_idx;
  size_t hi = (mark_pos <= cursor_idx) ? cursor_idx : mark_pos;
  mark_active = 0;
  if (lo >= hi) return;
  size_t rlen = hi - lo;
  if (rlen < KILL_RING_MAX) {
    memcpy(kill_ring, buffer + lo, rlen);
    kill_ring_len = rlen;
    kill_ring[kill_ring_len] = '\0';
  }
  memmove(buffer + lo, buffer + hi, *len - hi);
  *len -= rlen;
  buffer[*len] = '\0';
  set_cursor_from_index(buffer, lo);
}

void copy_region(const char *buffer, size_t len) {
  if (!mark_active) return;
  size_t cursor_idx = get_buffer_index_from_cursor(buffer, len);
  size_t lo = (mark_pos <= cursor_idx) ? mark_pos : cursor_idx;
  size_t hi = (mark_pos <= cursor_idx) ? cursor_idx : mark_pos;
  mark_active = 0;
  if (lo >= hi) return;
  size_t rlen = hi - lo;
  if (rlen < KILL_RING_MAX) {
    memcpy(kill_ring, buffer + lo, rlen);
    kill_ring_len = rlen;
    kill_ring[kill_ring_len] = '\0';
  }
}

/* Return the first byte of the current logical line. */
static size_t current_line_start(const char *buffer, size_t len) {
  size_t pos = get_buffer_index_from_cursor(buffer, len);
  while (pos > 0 && buffer[pos - 1] != '\n') pos--;
  return pos;
}

static int org_heading_content_pos(const char *buffer, size_t len, size_t *out) {
  size_t p = current_line_start(buffer, len);
  if (p >= len || buffer[p] != '*') return 0;
  while (p < len && buffer[p] == '*') p++;
  if (p >= len || buffer[p] != ' ') return 0;
  while (p < len && buffer[p] == ' ') p++;
  *out = p;
  return 1;
}

/* C-c C-t: TODO -> DONE -> no keyword -> TODO on an Org heading. */
static int org_cycle_todo(char *buffer, size_t *len) {
  size_t start = current_line_start(buffer, *len);
  size_t p;
  if (!org_heading_content_pos(buffer, *len, &p)) return 0;
  int content_col = (int)(p - start);

  if (p + 4 <= *len && !memcmp(buffer + p, "TODO", 4) &&
      (p + 4 == *len || isspace((unsigned char)buffer[p + 4]))) {
    memcpy(buffer + p, "DONE", 4);
    set_status("TODO state: DONE");
  } else if (p + 4 <= *len && !memcmp(buffer + p, "DONE", 4) &&
             (p + 4 == *len || isspace((unsigned char)buffer[p + 4]))) {
    size_t remove = (p + 4 < *len && buffer[p + 4] == ' ') ? 5 : 4;
    memmove(buffer + p, buffer + p + remove, *len - p - remove);
    *len -= remove;
    buffer[*len] = '\0';
    if (cursor_x > content_col) {
      cursor_x -= (int)remove;
      if (cursor_x < content_col) cursor_x = content_col;
    }
    set_status("TODO state cleared");
  } else {
    if (*len + 5 >= BUFFER_SIZE) { set_status("Buffer full"); return 0; }
    memmove(buffer + p + 5, buffer + p, *len - p);
    memcpy(buffer + p, "TODO ", 5);
    *len += 5;
    buffer[*len] = '\0';
    if (cursor_x >= content_col) cursor_x += 5;
    set_status("TODO state: TODO");
  }
  fileref.lines = countlines_buf(buffer, *len);
  return 1;
}

/* C-c C-x: toggle the first checkbox on the current Org line. */
static int org_toggle_checkbox(char *buffer, size_t len) {
  size_t p = current_line_start(buffer, len);
  size_t end = p;
  while (end < len && buffer[end] != '\n') end++;
  for (; p + 2 < end; p++) {
    if (buffer[p] == '[' && buffer[p + 2] == ']' &&
        (buffer[p + 1] == ' ' || buffer[p + 1] == 'x' || buffer[p + 1] == 'X')) {
      buffer[p + 1] = buffer[p + 1] == ' ' ? 'X' : ' ';
      set_status(buffer[p + 1] == 'X' ? "Checkbox checked" : "Checkbox cleared");
      return 1;
    }
  }
  return 0;
}

/* -- search ---------------------------------------------------------------- */

int find_line_of_match(const char *buffer, const char *term) {
  size_t      term_len = strlen(term);
  const char *p        = buffer;
  int         line     = 0; /* 0-based to match cursor_y */

  if (term_len == 0) return -1;

  while (*p) {
    if (strncmp(p, term, term_len) == 0) return line;
    if (*p == '\n') line++;
    p++;
  }
  return -1;
}

void search_and_jump(char *term, const char *buffer) {
  int line = find_line_of_match(buffer, term);
  if (line != -1) {
    cursor_y      = line;
    scroll_offset = cursor_y > 0 ? cursor_y - 1 : 0;
    scroll_row    = 0;
    cursor_x      = 0;
    redraw_screen(buffer, length, status_msg);
  } else {
    set_status("No match for '%s'", term);
  }
}

int find_next_match(const char *buf, size_t len, const char *term, size_t from) {
  size_t tlen = strlen(term);
  if (!tlen) return -1;
  for (size_t i = from; i + tlen <= len; i++)
    if (strncmp(buf + i, term, tlen) == 0) return (int)i;
  return -1;
}

/* Search backward: find rightmost match strictly before position `before`. */
int find_prev_match(const char *buf, size_t len, const char *term, size_t before) {
  size_t tlen = strlen(term);
  if (!tlen || before == 0) return -1;
  size_t i = (before > len) ? len : before;
  while (i > 0) {
    i--;
    if (i + tlen <= len && strncmp(buf + i, term, tlen) == 0) return (int)i;
  }
  return -1;
}

/* C-c C-o (follow link): if the cursor is inside a [[link]] or
   [[link][description]] on the current line:
     - [[file:path]] opens `path` in a new buffer (fopen to check it
       exists first; reports "ERROR file does not exist" otherwise).
     - anything else jumps to the next occurrence of the link text
       using the existing text-search machinery (so a following C-s
       continues past that match). */
void follow_link_at_cursor(const char *buffer, size_t len, char **buf_ptr, char *fname) {
  if (current_mode != &markdown_mode && current_mode != &org_mode) {
    set_status("Not a link (markdown/org mode only)");
    return;
  }

  size_t idx = get_buffer_index_from_cursor(buffer, len);

  size_t line_start = idx;
  while (line_start > 0 && buffer[line_start - 1] != '\n') line_start--;
  size_t line_end = idx;
  while (line_end < len && buffer[line_end] != '\n') line_end++;

  /* locate the [[ ... ]] span on this line that contains the cursor */
  long open = -1, close = -1;
  for (size_t i = line_start; i + 1 < line_end; i++) {
    if (buffer[i] == '[' && buffer[i + 1] == '[') {
      size_t j = i + 2;
      while (j + 1 < line_end && !(buffer[j] == ']' && buffer[j + 1] == ']')) j++;
      if (j + 1 < line_end && buffer[j] == ']' && buffer[j + 1] == ']') {
        if (idx >= i && idx <= j + 1) { open = (long)i; close = (long)j; break; }
        i = j + 1; /* skip past this link and keep scanning the line */
      }
    }
  }

  if (open < 0) { set_status("Not inside a [[link]]"); return; }

  size_t text_start = (size_t)open + 2;
  size_t text_end   = (size_t)close; /* exclusive, points at the first ']' */

  /* org-style [[link][description]]: the link target stops at '[' or ']' */
  size_t link_end = text_start;
  while (link_end < text_end && buffer[link_end] != '[' && buffer[link_end] != ']')
    link_end++;

  size_t link_len = link_end - text_start;
  if (link_len == 0 || link_len >= MINIBUFFER_MAX) { set_status("Empty link"); return; }

  char link_text[MINIBUFFER_MAX];
  memcpy(link_text, buffer + text_start, link_len);
  link_text[link_len] = '\0';

  const char *file_prefix = "file:";
  size_t file_prefix_len = strlen(file_prefix);
  if (strncmp(link_text, file_prefix, file_prefix_len) == 0) {
    char *path = link_text + file_prefix_len;
    FILE *f = fopen(path, "r");
    if (!f) {
      set_status("ERROR file does not exist: %s", path);
      return;
    }
    fclose(f);
    open_in_new_buf(path, buf_ptr, fname);
    return;
  }

  size_t from = (size_t)close + 2;
  int m = find_next_match(buffer, len, link_text, from);
  if (m < 0) m = find_next_match(buffer, len, link_text, 0);
  if (m < 0) { set_status("No match for '%s'", link_text); return; }

  strncpy(search_term, link_text, MINIBUFFER_MAX - 1);
  search_term[MINIBUFFER_MAX - 1] = '\0';
  search_match_pos = m;
  search_match_len = link_len;
  set_cursor_from_index(buffer, (size_t)m);
  set_status("Link -> '%s'", link_text);
}

/* Hungry delete: eat all preceding whitespace if at whitespace,
   otherwise kill the previous word (standard M-backspace). */
void hungry_delete_backward(char *buffer, size_t *len) {
  size_t end = get_buffer_index_from_cursor(buffer, *len);
  if (end == 0) return;
  size_t start = end;
  if (buffer[start - 1] == ' ' || buffer[start - 1] == '\t') {
    while (start > 0 && (buffer[start-1] == ' ' || buffer[start-1] == '\t'))
      start--;
  } else {
    start--;
    while (start > 0 && !isalnum((unsigned char)buffer[start]) && buffer[start] != '_')
      start--;
    while (start > 0 && (isalnum((unsigned char)buffer[start-1]) || buffer[start-1] == '_'))
      start--;
  }
  size_t kill_len = end - start;
  if (kill_len == 0) return;
  if (kill_len < KILL_RING_MAX) {
    memcpy(kill_ring, buffer + start, kill_len);
    kill_ring_len = kill_len;
    kill_ring[kill_ring_len] = '\0';
  }
  memmove(buffer + start, buffer + end, *len - end);
  *len -= kill_len;
  buffer[*len] = '\0';
  fileref.lines = countlines_buf(buffer, *len);
  set_cursor_from_index(buffer, start);
}

void undo_push(const char *buf, size_t len) {
  if (!undo_ring[0].data) return;
  modified = 1;
  undo_top = (undo_top + 1) % UNDO_DEPTH;
  memcpy(undo_ring[undo_top].data, buf, len);
  undo_ring[undo_top].len = len;
  undo_ring[undo_top].cx  = cursor_x;
  undo_ring[undo_top].cy  = cursor_y;
  if (undo_count < UNDO_DEPTH) undo_count++;
}

int undo_pop(char *buf, size_t *len) {
  if (undo_count == 0) return 0;
  memcpy(buf, undo_ring[undo_top].data, undo_ring[undo_top].len);
  *len     = undo_ring[undo_top].len;
  cursor_x = undo_ring[undo_top].cx;
  cursor_y = undo_ring[undo_top].cy;
  buf[*len] = '\0';
  undo_top  = (undo_top + UNDO_DEPTH - 1) % UNDO_DEPTH;
  undo_count--;
  return 1;
}

/* -- paren matching -------------------------------------------------------- */

int is_paren(char c) {
  return c=='(' || c==')' || c=='{' || c=='}' || c=='[' || c==']';
}

void update_paren_match(const char *buffer, size_t len) {
  paren_cursor_pos = -1;
  paren_match_pos  = -1;

  size_t idx = get_buffer_index_from_cursor(buffer, len);
  if (idx >= len) return;

  char c = buffer[idx];
  if (!is_paren(c)) return;

  const char *bt = brace_type_of_char(c);
  if (!bt) return;

  paren_cursor_pos = (int)idx;
  paren_match_pos  = find_matchingparen(buffer, (int)idx, bt);
}

const char *brace_type_of_char(char c) {
  if (c=='(' || c==')') return "()";
  if (c=='{' || c=='}') return "{}";
  if (c=='[' || c==']') return "[]";
  return NULL;
}

int find_matchingparen(const char *buf, int pos, const char *brace_type) {
  if (!buf || !brace_type || !brace_type[0] || !brace_type[1]) return -1;
  int len = (int)strlen(buf);
  if (pos < 0 || pos >= len) return -1;

  char open  = brace_type[0];
  char close = brace_type[1];
  char c     = buf[pos];
  int  dir   = 0;

  if      (c == open)  dir =  1;
  else if (c == close) dir = -1;
  else return -1;

  int depth = 1;
  for (int i = pos + dir; i >= 0 && i < len; i += dir) {
    if      (buf[i] == open)  depth++;
    else if (buf[i] == close) depth--;
    if (depth == 0) return i;
  }
  return -1;
}

/* -- multi-buffer management ----------------------------------------------- */

static int buf_alloc(int idx) {
  buf_slot_t *s = &buf_slots[idx];
  if (s->data) return 0;
  s->data = calloc(BUFFER_SIZE, 1);
  if (!s->data) return -1;
  for (int i = 0; i < UNDO_DEPTH; i++) {
    s->undo[i].data = calloc(BUFFER_SIZE, 1);
    if (!s->undo[i].data) {
      /* free everything allocated so far before returning */
      for (int j = 0; j < i; j++) { free(s->undo[j].data); s->undo[j].data = NULL; }
      free(s->data); s->data = NULL;
      return -1;
    }
  }
  s->undo_top   = -1;
  s->undo_count = 0;
  return 0;
}

static void buf_kill(int idx, char **buf_ptr, char *fname) {
  if (num_bufs <= 1) {
    set_status("Cannot kill the last buffer");
    return;
  }
  if (idx < 0 || idx >= num_bufs) return;

  buf_slot_t *s = &buf_slots[idx];

  /* free the buffer data and undo stack */
  if (s->data) { free(s->data); s->data = NULL; }
  for (int i = 0; i < UNDO_DEPTH; i++) {
    if (s->undo[i].data) { free(s->undo[i].data); s->undo[i].data = NULL; }
  }
  memset(s, 0, sizeof(buf_slot_t));

  /* shift subsequent buffers down */
  for (int i = idx; i < num_bufs - 1; i++) {
    memcpy(&buf_slots[i], &buf_slots[i + 1], sizeof(buf_slot_t));
  }
  memset(&buf_slots[num_bufs - 1], 0, sizeof(buf_slot_t));
  num_bufs--;

  /* if we killed the current buffer, switch to another */
  if (current_buf == idx) {
    if (current_buf >= num_bufs) current_buf = num_bufs - 1;
    if (current_buf < 0) current_buf = 0;
    buf_load_from_slot(current_buf, buf_ptr, fname);
  } else if (current_buf > idx) {
    current_buf--;
  }

  set_status("Buffer killed");
}

/*
 * Save live globals into slot idx.
 * No text copy needed: buffer always points to buf_slots[current_buf].data.
 * Undo data is already in the slot's undo[i].data because we keep
 * undo_ring[i].data == buf_slots[current_buf].undo[i].data at all times.
 */
static void buf_save_to_slot(int idx) {
  buf_slot_t *s    = &buf_slots[idx];
  s->length        = length;
  s->cursor_x      = cursor_x;
  s->cursor_y      = cursor_y;
  s->scroll_offset = scroll_offset;
  s->scroll_x      = scroll_x;
  s->scroll_row    = scroll_row;
  s->mark_active   = mark_active;
  s->mark_pos      = mark_pos;
  s->mode          = current_mode;
  memcpy(&s->fileref, &fileref, sizeof(fileref));
  for (int i = 0; i < UNDO_DEPTH; i++) {
    s->undo[i].len = undo_ring[i].len;
    s->undo[i].cx  = undo_ring[i].cx;
    s->undo[i].cy  = undo_ring[i].cy;
  }
  s->undo_top   = undo_top;
  s->undo_count = undo_count;
  s->modified   = modified;
}

/* Restore all globals from slot idx, updating the buffer pointer and fname. */
static void buf_load_from_slot(int idx, char **buf_ptr, char *fname) {
  buf_slot_t *s  = &buf_slots[idx];
  current_buf    = idx;
  *buf_ptr       = s->data;
  length         = s->length;
  cursor_x       = s->cursor_x;
  cursor_y       = s->cursor_y;
  scroll_offset  = s->scroll_offset;
  scroll_x       = s->scroll_x;
  scroll_row     = s->scroll_row;
  mark_active    = s->mark_active;
  mark_pos       = s->mark_pos;
  current_mode   = s->mode;
  memcpy(&fileref, &s->fileref, sizeof(fileref));
  strncpy(fname, s->fileref.name, 255);
  fname[255] = '\0';
  for (int i = 0; i < UNDO_DEPTH; i++) {
    undo_ring[i].data = s->undo[i].data;
    undo_ring[i].len  = s->undo[i].len;
    undo_ring[i].cx   = s->undo[i].cx;
    undo_ring[i].cy   = s->undo[i].cy;
  }
  undo_top   = s->undo_top;
  undo_count = s->undo_count;
  modified   = s->modified;
  search_match_pos = -1;
  search_match_len = 0;
}

/*
 * Open 'input' (the raw minibuffer "find-file <path>" string) in a new slot.
 * Updates *buf_ptr and fname to reflect the newly active buffer.
 */
static int open_in_new_buf(char *input, char **buf_ptr, char *fname) {
  if (num_bufs >= MAX_OPEN_BUFFERS) {
    set_status("Too many open buffers (max %d)", MAX_OPEN_BUFFERS);
    return -1;
  }
  int new_idx = num_bufs;
  if (buf_alloc(new_idx) != 0) {
    set_status("Out of memory");
    return -1;
  }
  int prev_modified = modified;
  buf_save_to_slot(current_buf);
  current_buf = new_idx;
  num_bufs++;
  *buf_ptr = buf_slots[new_idx].data;
  /* redirect global undo ring to the new slot's buffers */
  for (int i = 0; i < UNDO_DEPTH; i++) {
    undo_ring[i].data = buf_slots[new_idx].undo[i].data;
    undo_ring[i].len  = 0;
    undo_ring[i].cx   = 0;
    undo_ring[i].cy   = 0;
  }
  undo_top = -1; undo_count = 0;
  /* find_file sets length, cursor, scroll, fileref, current_mode */
  if (find_file(input, *buf_ptr, fname) != 0) {
    /* new / missing file -- start with empty buffer */
    length = 0;
    cursor_x = cursor_y = scroll_offset = scroll_row = 0;
    (*buf_ptr)[0] = '\0';
    fileref.lines = 1;
  }
  buf_save_to_slot(new_idx);
  if (prev_modified)
    set_status("%s  [%d/%d]  (buf %d has unsaved changes)",
               fname, current_buf + 1, num_bufs, current_buf);
  else
    set_status("%s  [%d/%d]", fname, current_buf + 1, num_bufs);
  return 0;
}

/* -- help buffer ----------------------------------------------------------- */

static const char HELP_TEXT[] =
  "tbmacs (tbg) -- key bindings\n"
  "============================\n"
  "\n"
  "FILES & BUFFERS\n"
  "  C-x C-s          Save\n"
  "  C-x C-c          Quit  (prompts if unsaved)\n"
  "  C-x C-f          Open file  (TAB completes path)\n"
  "  C-x d            Dired directory browser  (prompts for directory)\n"
  "  C-x k            Kill current buffer\n"
  "  C-x left/right   Previous / next buffer\n"
  "  C-x w            Toggle soft word wrap\n"
  "  C-o              Cycle to next buffer\n"
  "\n"
  "DIRED BROWSER\n"
  "  C-n / Down       Next entry\n"
  "  C-p / Up         Previous entry\n"
  "  RET / Right      Open file or enter directory\n"
  "  - / Left / BS    Go to parent directory\n"
  "  q / ESC          Close dired\n"
  "  C-x k            Close dired\n"
  "  C-x C-c          Quit program\n"
  "\n"
  "ORG MODE\n"
  "  C-c C-o          Follow link at cursor\n"
  "  C-c C-t          Cycle heading TODO state\n"
  "  C-c C-x          Toggle checkbox on current line\n"
  "\n"
  "WINDOWS  (requires tmux or screen)\n"
  "  C-x 2            Split: new pane below\n"
  "  C-x 3            Split: new pane right\n"
  "  C-x 1            Unsplit: close all other panes\n"
  "  C-x o            Switch to other pane\n"
  "\n"
  "MOVEMENT\n"
  "  C-a / Home       Beginning of line\n"
  "  C-e / End        End of line\n"
  "  Arrow keys       Character / line movement\n"
  "  M-f / M-b        Forward / backward word\n"
  "  M-< / M->        Beginning / end of buffer\n"
  "  PgUp / PgDn      Page up / down\n"
  "  C-l              Recenter screen\n"
  "  C-x < / C-x >    Scroll left / right  (long lines)\n"
  "                   (no-op when soft wrap is on)\n"
  "\n"
  "EDITING\n"
  "  TAB              Indent  (inserts spaces), or accept the\n"
  "                   completion shown in grey  (see autocomplete)\n"
  "  Enter            New line  (auto-indents)\n"
  "  Backspace        Delete char left\n"
  "  Delete / C-d     Delete char right\n"
  "  C-k              Kill to end of line\n"
  "  M-d              Delete forward word\n"
  "  M-Backspace      Hungry delete backward\n"
  "                   (eats whitespace, or kills word)\n"
  "  M-q              Fill / reflow paragraph\n"
  "  C-x u            Undo\n"
  "\n"
  "MARK & REGION\n"
  "  C-SPC            Set mark\n"
  "  C-h              Select all\n"
  "  C-w              Cut region\n"
  "  M-w              Copy region\n"
  "  C-y              Yank  (paste)\n"
  "\n"
  "SEARCH & REPLACE\n"
  "  C-s              Incremental search forward\n"
  "                   (C-s again = next match, Enter = keep & exit)\n"
  "  C-r              Incremental search backward\n"
  "  C-x %            Query replace\n"
  "                   y = replace   n = skip\n"
  "                   ! = replace all   q = quit\n"
  "\n"
  "MOUSE\n"
  "  Left click       Position cursor\n"
  "  Scroll wheel     Scroll buffer\n"
  "\n"
  "CANCEL\n"
  "  C-g / ESC        Cancel any pending operation\n"
  "\n"
  "M-x COMMANDS\n"
  "  TAB at the M-x prompt completes the command name.  The\n"
  "  rest of the best match is shown in grey as you type and\n"
  "  TAB inserts exactly that; when the prefix still fits more\n"
  "  than one command they are listed above the prompt.  This\n"
  "  is always on -- it is not the autocomplete dictionary.\n"
  "\n"
  "  help                    This buffer\n"
  "  find-file <path>        Open file\n"
  "  dired <path>            Open directory browser\n"
  "  goto-line <N>           Jump to line N\n"
  "  kill-line               Kill current line\n"
  "  delete-char             Delete char at cursor\n"
  "  evil-mode              Open this file in tbv in a tmux pane\n"
  "  redraw-screen           Force full redraw\n"
  "  c-mode                  C / C++ highlighting\n"
  "  python-mode             Python highlighting\n"
  "  lisp-mode               Lisp / Scheme highlighting\n"
  "  sh-mode                 Shell highlighting\n"
  "  lua-mode                Lua highlighting\n"
  "  roff-mode               Roff / man highlighting\n"
  "  makefile-mode           Makefile highlighting\n"
  "  javascript-mode         JavaScript / TypeScript highlighting\n"
  "  rust-mode               Rust highlighting\n"
  "  pascal-mode             Pascal highlighting\n"
  "  assembly-mode           Assembly highlighting\n"
  "  java-mode / go-mode     Java or Go highlighting\n"
  "  csharp-mode             C# highlighting\n"
  "  sql-mode / json-mode    SQL or JSON highlighting\n"
  "  html-mode / css-mode    HTML/XML or CSS highlighting\n"
  "  soft-wrap-mode          Toggle soft word wrap  (off by default)\n"
  "                          Long lines fold at the last space that\n"
  "                          fits; no continuation character is shown.\n"
  "  toggle-single-quotes    Toggle highlighting of single-quoted strings\n"
  "  autocomplete            Toggle word completion  (off by default)\n"
  "                          The rest of the word appears in grey as\n"
  "                          you type; TAB accepts it.\n"
  "  autocomplete file:PATH  Load PATH as the dictionary, e.g.\n"
  "                          M-x autocomplete file:/home/user/c.lisp\n"
  "                          A dictionary is a file of s-expressions:\n"
  "                            (autocomplete 'int' 'char' '#define')\n"
  "                          Atoms may be 'quoted', \"quoted\", or bare;\n"
  "                          \";\" comments run to end of line.\n"
  "  autocomplete +file:PATH Merge PATH into the current dictionary\n"
  "  autocomplete buffer     Build the dictionary out of the words\n"
  "                          already in this buffer, and keep it in\n"
  "                          step as you type\n"
  "  autocomplete +buffer    Merge this buffer's words into the\n"
  "                          current dictionary, once\n"
  "  autocomplete mode       Use the major mode's keywords instead\n"
  "                          (the default when no file is loaded)\n"
  "  autocomplete on | off   Enable / disable completion\n"
  "  autocomplete clear      Empty the dictionary\n";

static void open_help_buffer(char **buf_ptr, char *fname) {
  /* if *help* is already open, just switch to it */
  for (int i = 0; i < num_bufs; i++) {
    if (strcmp(buf_slots[i].fileref.name, "*help*") == 0) {
      buf_save_to_slot(current_buf);
      buf_load_from_slot(i, buf_ptr, fname);
      set_status("*help*  [%d/%d]", current_buf + 1, num_bufs);
      return;
    }
  }

  if (num_bufs >= MAX_OPEN_BUFFERS) { set_status("Too many open buffers"); return; }
  int new_idx = num_bufs;
  if (buf_alloc(new_idx) != 0) { set_status("Out of memory"); return; }

  buf_save_to_slot(current_buf);
  current_buf = new_idx;
  num_bufs++;
  *buf_ptr = buf_slots[new_idx].data;

  size_t hlen = strlen(HELP_TEXT);
  if (hlen >= BUFFER_SIZE) hlen = BUFFER_SIZE - 1;
  memcpy(*buf_ptr, HELP_TEXT, hlen);
  (*buf_ptr)[hlen] = '\0';

  length        = hlen;
  cursor_x      = 0; cursor_y = 0;
  scroll_offset = 0; scroll_x = 0; scroll_row = 0;
  modified      = 0;
  strncpy(fileref.name, "*help*", sizeof(fileref.name) - 1);
  strncpy(fname, "*help*", 255); fname[255] = '\0';
  fileref.lines = countlines_buf(*buf_ptr, hlen);
  current_mode  = NULL;

  for (int i = 0; i < UNDO_DEPTH; i++) {
    undo_ring[i].data = buf_slots[new_idx].undo[i].data;
    undo_ring[i].len  = 0;
    undo_ring[i].cx   = 0;
    undo_ring[i].cy   = 0;
  }
  undo_top = -1; undo_count = 0;
  buf_save_to_slot(new_idx);
  set_status("*help*  [%d/%d]", current_buf + 1, num_bufs);
}

/* -- quit ------------------------------------------------------------------ */

void quit_no_save(const char *buffer) {
  tb_shutdown();
  free((void *)buffer);
  exit(0);
}

void quit_with_save(const char *filename, const char *buffer, size_t len) {
  /* Quitting is the last moment the buffer exists; a failed write must not
   * be followed by exit(0). */
  if (save_to_file(filename, buffer, len) < 0) {
    set_status("Cannot write %s -- not quitting", filename);
    return;
  }
  tb_shutdown();
  free((void *)buffer);
  exit(0);
}

/* -- find-file tab completion ---------------------------------------------- */

static int cmp_entry(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

static void complete_find_file(void) {
  const char *ff     = "find-file ";
  size_t      ff_len = strlen(ff);
  if (strncmp(minibuffer_input, ff, ff_len) != 0) return;

  char *partial = minibuffer_input + ff_len;

  /* optional: expand leading ~ to $HOME */
  char expanded[MINIBUFFER_MAX] = {0};
  if (partial[0] == '~' && (partial[1] == '/' || partial[1] == '\0')) {
    const char *home = getenv("HOME");
    if (home) {
      snprintf(expanded, sizeof(expanded), "%s%s", home, partial + 1);
      partial = expanded;
    }
  }

  /* split into directory part and filename prefix */
  char dir_part[MINIBUFFER_MAX]    = {0};
  char file_prefix[MINIBUFFER_MAX] = {0};
  char *last_slash = strrchr(partial, '/');
  if (last_slash) {
    size_t dlen = (size_t)(last_slash - partial) + 1;
    strncpy(dir_part, partial, dlen < sizeof(dir_part) ? dlen : sizeof(dir_part) - 1);
    strncpy(file_prefix, last_slash + 1, sizeof(file_prefix) - 1);
  } else {
    strncpy(file_prefix, partial, sizeof(file_prefix) - 1);
  }

  const char *open_dir = dir_part[0] ? dir_part : ".";
  size_t      fp_len   = strlen(file_prefix);

  /* collect matches from the directory */
#define MAX_COMPLETIONS 128
  static char matches[MAX_COMPLETIONS][256];
  int match_count = 0;

  DIR *d = opendir(open_dir);
  if (!d) { set_status("Cannot open: %s", open_dir); return; }

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && match_count < MAX_COMPLETIONS) {
    /* skip . and ..; skip hidden files unless prefix starts with '.' */
    if (ent->d_name[0] == '.') {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
      if (file_prefix[0] != '.') continue;
    }
    if (fp_len == 0 || strncmp(ent->d_name, file_prefix, fp_len) == 0) {
      strncpy(matches[match_count], ent->d_name, 255);
      matches[match_count][255] = '\0';
      match_count++;
    }
  }
  closedir(d);

  if (match_count == 0) { set_status("No match"); return; }

  /* sort alphabetically so output is predictable */
  qsort(matches, match_count, sizeof(matches[0]), cmp_entry);

  /* find longest common prefix across all matches */
  char   common[256];
  strncpy(common, matches[0], 255);
  common[255] = '\0';
  size_t common_len = strlen(common);
  for (int i = 1; i < match_count; i++) {
    size_t j = 0;
    while (j < common_len && matches[i][j] == common[j]) j++;
    common_len = j;
  }
  common[common_len] = '\0';

  /* for a unique match, check if it's a directory so we can append '/' */
  int append_slash = 0;
  if (match_count == 1) {
    char full[512];
    snprintf(full, sizeof(full), "%s%s", dir_part[0] ? dir_part : "", matches[0]);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) append_slash = 1;
  }

  /* write the completed path back into the minibuffer */
  char new_tail[MINIBUFFER_MAX];
  snprintf(new_tail, sizeof(new_tail), "%s%s%s",
           dir_part, common, append_slash ? "/" : "");

  /* if ~ was expanded, keep the expanded form so the path stays valid */
  snprintf(minibuffer_input, MINIBUFFER_MAX, "find-file %s", new_tail);
  minibuffer_length = (int)strlen(minibuffer_input);
  minibuffer_cursor = minibuffer_length;

  /* show candidates when ambiguous */
  if (match_count > 1) {
    char list[256] = {0};
    int  pos       = 0;
    for (int i = 0; i < match_count && pos < (int)sizeof(list) - 4; i++) {
      if (i > 0) list[pos++] = ' ';
      int nlen = (int)strlen(matches[i]);
      if (pos + nlen >= (int)sizeof(list) - 4) {
        strncpy(list + pos, "...", sizeof(list) - pos - 1);
        break;
      }
      memcpy(list + pos, matches[i], nlen);
      pos += nlen;
    }
    set_status("%d: %s", match_count, list);
  }
}

/* -- drawing --------------------------------------------------------------- */

void draw_minibuffer(void) {
  if (!minibuffer_active) return;
  int  width = tb_width();
  int  y     = tb_height() - 1;
  char prompt[MINIBUFFER_MAX + 16];
  int  prefix_len;

  if (replace_state == 1) {
    snprintf(prompt, sizeof(prompt), "Replace: %s", minibuffer_input);
    prefix_len = 9;
  } else if (replace_state == 2) {
    snprintf(prompt, sizeof(prompt), "With: %s", minibuffer_input);
    prefix_len = 6;
  } else if (search_backward_active) {
    snprintf(prompt, sizeof(prompt), "Search backward: %s", minibuffer_input);
    prefix_len = 17;
  } else if (search_forward_active) {
    snprintf(prompt, sizeof(prompt), "Search: %s", minibuffer_input);
    prefix_len = 8;
  } else {
    snprintf(prompt, sizeof(prompt), "M-x %s", minibuffer_input);
    prefix_len = 4;
  }

  for (int x = 0; x < width; ++x)
    tb_set_cell(x, y, ' ', 0, 6);
  for (int i = 0; prompt[i] && i < width; ++i)
    tb_set_cell(i, y, prompt[i], 0, 6);

  /* UX-6: draw cursor inside the minibuffer input */
  int cur_screen = prefix_len + minibuffer_cursor;
  if (cur_screen < width) {
    char cur_ch = prompt[cur_screen] ? prompt[cur_screen] : ' ';
    tb_set_cell(cur_screen, y, cur_ch, 0, 3);  /* black on yellow */
  }

  /* M-x command ghost: drawn after the cursor so its first cell can keep the
   * cursor background, and only ever past the end of the typed text. */
  ac_mx_draw_ghost(cur_screen, y, width);

  /* An open minibuffer sits on the message row, so a message set while it is
   * open -- the completion candidate lists, "No match" -- is painted over
   * before it is ever seen.  Repeat it on the status bar row above, which is
   * the only line left; it goes back to name and position when status_ttl
   * runs out.  Without this the ambiguous-prefix listings are dead code. */
  if (status_msg) {
    int sy = y - 1;
    if (sy >= 0) {
      for (int x = 0; x < width; ++x)
        tb_set_cell(x, sy, ' ', STATUS_FG, STATUS_BG);
      for (int i = 0; status_msg[i] && i < width; ++i)
        tb_set_cell(i, sy, status_msg[i], STATUS_FG, STATUS_BG);
    }
  }
}

void draw_status_bar(const char *msg, const char *buffer, size_t len) {
  int width  = tb_width();
  int height = tb_height();
  int y      = height - 2;

  for (int x = 0; x < width; ++x)
    tb_set_cell(x, y, ' ', STATUS_FG, STATUS_BG);

  /* left: modified flag + buffer indicator + filename */
  char left[280];
  snprintf(left, sizeof(left), " [%d/%d] %s%.120s ",
           current_buf + 1, num_bufs,
           modified ? "** " : "",
           fileref.name);
  for (int i = 0; left[i] && i < width; ++i)
    tb_set_cell(i, y, left[i], STATUS_FG, STATUS_BG);

  /* right: line/col */
  char right[128];
  snprintf(right, sizeof(right), " %sLn %d/%d, Col %d ",
           soft_wrap ? "Wrap  " : "",
           cursor_y + 1, fileref.lines, cursor_x + 1);
  int right_start = width - (int)strlen(right) - 1;
  if (right_start < 0) right_start = 0;
  for (int i = 0; right[i] && right_start + i < width; ++i)
    tb_set_cell(right_start + i, y, right[i], STATUS_FG, STATUS_BG);

  if (msg) {
    for (int i = 0; msg[i] && i < width; ++i)
      tb_set_cell(i, y + 1, msg[i], D_FOREGROUND, D_BACKGROUND);
  }
}

/* -- redraw_screen --------------------- */

/* Sanitize a raw buffer byte for display.  A cell must never hold a control
 * byte: termbox2 sends cell contents verbatim, so a literal '\t' (common in
 * Makefiles) moves the physical terminal cursor to the next tab stop while
 * termbox2 still assumes a 1-column advance.  The screen and termbox's front
 * buffer then diverge, and the diff-based tb_present() stops repainting cells
 * it believes are unchanged -- leaving stale text (the scroll artifacts).
 * The cursor model is strictly 1 byte = 1 column, so tab renders as a single
 * space and other control bytes as '?'.  Also forces unsigned so high UTF-8
 * bytes aren't sign-extended into bogus codepoints.
 * Bytes 0x80-0x9F are control bytes too: shown verbatim they become C1
 * control codepoints (0x9B is CSI!) that the terminal executes instead of
 * printing, desyncing it from the front buffer -- the same artifact
 * mechanism as tabs.  UTF-8 continuation bytes land in this range, so any
 * multi-byte character used to trigger it. */
static uint32_t display_ch(char c) {
  unsigned char u = (unsigned char)c;
  if (u == '\t')                          return ' ';
  if (u < 32 || (u >= 127 && u <= 0x9f)) return '?';
  return u;
}

/* Advance the wrap row so that it contains logical column `x` of the line at
 * index `ls`.  A no-op when soft wrap is off, so the drawing loop can call it
 * unconditionally wherever it moves along the line.  Both `x` and *row_end
 * only ever increase, so the whole line costs one pass. */
static void wrap_sync(const char *buffer, size_t ls, int ll, int width, int x,
                      int *row_start, int *row_end, int *row_y) {
  if (!soft_wrap) return;
  while (x >= *row_end && *row_end < ll) {
    *row_start = *row_end;
    *row_end   = wrap_break(buffer, ls, ll, *row_start, width);
    (*row_y)++;
  }
}

void redraw_screen(const char *buffer, size_t len, const char *status) {
  /* The browser owns the screen while it is up.  dired_enter() only sets the
   * flag -- nothing painted until dired_handle_key() got its first key, so
   * "M-x dired" (and C-x d, and following a link into a directory) opened on
   * a blank screen that filled in when you pressed something.  Every paint
   * goes through here, so this is the one place that has to know. */
  if (dired_active) { draw_dired(); return; }

  /* Use consistent attrs so the diff engine sees real fg changes, not
   * TB_DEFAULT(0) vs D_FOREGROUND(7) artifacts on cells past EOL. */
  tb_set_clear_attrs(D_FOREGROUND, D_BACKGROUND);
  tb_clear();
  int screen_width  = tb_width();
  int screen_height = tb_height() - 2;

  /* compute paren highlight range */
  int lo = -1, hi = -1;
  if (paren_cursor_pos != -1 && paren_match_pos != -1) {
    lo = paren_cursor_pos < paren_match_pos
      ? paren_cursor_pos : paren_match_pos;
    hi = paren_cursor_pos < paren_match_pos
      ? paren_match_pos : paren_cursor_pos;
  }

  /* compute region highlight range */
  size_t region_lo = 0, region_hi = 0;
  int region_active = 0;
  if (mark_active) {
    size_t cursor_idx = get_buffer_index_from_cursor(buffer, len);
    region_lo = (mark_pos <= cursor_idx) ? mark_pos : cursor_idx;
    region_hi = (mark_pos <= cursor_idx) ? cursor_idx : mark_pos;
    region_active = (region_lo < region_hi);
  }

  /* skip lines above scroll_offset */
  int    logical_y = 0;
  size_t i         = 0;
  while (i < len && logical_y < scroll_offset) {
    if (buffer[i] == '\n') logical_y++;
    i++;
  }

  if (soft_wrap) scroll_x = 0;   /* nothing can be off the right edge */

  /* draw visible lines.  Under soft wrap the topmost logical line may start
   * above the window: it is drawn from a negative row so its hidden rows fall
   * off the top (termbox discards out-of-range cells) while the syntax
   * highlighting state still walks every character. */
  int  screen_y  = soft_wrap ? -scroll_row : 0;
  char in_string = 0; /* 0=none, '"'=double, '\''=single — resets each redraw */
  char in_atag   = 0;
  int  in_comment = 0; /* BUG-4 fix: local so it resets each full redraw pass */
  
  while (i < len && screen_y < screen_height) {

    if (buffer[i] == '\n') {
      in_string = 0; /* unterminated string doesn't bleed to next line */
      in_atag   = 0;
      logical_y++;
      screen_y++;
      i++;
      continue;
    }

    /* draw one logical line: with horizontal scroll (UX-5), or folded across
     * several screen rows when soft wrap is on.  row_y is the row the next
     * character lands on -- it equals screen_y until the line wraps. */
    int    line_x    = 0;
    int    row_y     = screen_y;
    int    row_start = 0;              /* logical column this row begins at */
    int    row_end   = 0;              /* logical column this row ends at */
    size_t line_s    = i;              /* buffer index of the line start */
    int    line_l    = 0;              /* logical length of the line */
    if (soft_wrap) {
      size_t t = i;
      while (t < len && buffer[t] != '\n') t++;
      line_l  = (int)(t - line_s);
      row_end = wrap_break(buffer, line_s, line_l, 0, screen_width);
    }

    while (i < len && buffer[i] != '\n') {
      wrap_sync(buffer, line_s, line_l, screen_width, line_x,
                &row_start, &row_end, &row_y);
      if (soft_wrap && row_y >= screen_height) { /* rest of line is off-screen */
        while (i < len && buffer[i] != '\n') i++;
        break;
      }
      /* screen column; negative = scrolled off left */
      int sc = soft_wrap ? line_x - row_start : line_x - scroll_x;
      if (!soft_wrap && sc >= screen_width) { /* past right edge -- skip rest */
        while (i < len && buffer[i] != '\n') i++;
        break;
      }
      if (sc >= 0) {
        int        keyword_drawn = 0;
        uintattr_t fg            = D_FOREGROUND;
        uintattr_t bg            = D_BACKGROUND;

        /* -- priority 1: paren region --------------------------------- */
        if (lo != -1 && (int)i >= lo && (int)i <= hi) {
          bg = ((int)i == lo || (int)i == hi) ? 6 : 5;
          tb_set_cell(sc, row_y, display_ch(buffer[i]), fg, bg);
          line_x++; i++;
          continue;
        }

        /* -- priority 1b: region selection ---------------------------- */
        if (region_active && i >= region_lo && i < region_hi) {
          tb_set_cell(sc, row_y, display_ch(buffer[i]), 7, 4);
          line_x++; i++;
          continue;
        }

        /* -- priority 1c: search match -------------------------------- */
        if (search_match_pos >= 0 &&
            (int)i >= search_match_pos &&
            (int)i < search_match_pos + (int)search_match_len) {
          tb_set_cell(sc, row_y, display_ch(buffer[i]), 0, 3);
          line_x++; i++;
          continue;
        }

        /* -- priority 2: C block comment ------------------------------
         * Drawn one byte per pass so a comment can straddle a wrap: the
         * delimiters are tracked in `in_comment` instead of being painted
         * two cells at a time.  2 = the closing '/', 3 = the opening '*'
         * (which must not itself be read as the start of a close). */
        if (mode_has_c_comments(current_mode)) {
          if (in_comment) {
            tb_set_cell(sc, row_y, display_ch(buffer[i]), 5, D_BACKGROUND);
            if      (in_comment == 2) in_comment = 0;
            else if (in_comment == 3) in_comment = 1;
            else if (buffer[i] == '*' && i + 1 < len && buffer[i+1] == '/')
              in_comment = 2;
            line_x++; i++;
            continue;
          }
          if (buffer[i] == '/' && i + 1 < len && buffer[i+1] == '*') {
            in_comment = 3;
            tb_set_cell(sc, row_y, display_ch(buffer[i]), 5, D_BACKGROUND);
            line_x++; i++;
            continue;
          }
        }

        /* -- Org structure --------------------------------------------
         * Paint structural lines as a unit. Headings vary by depth;
         * directives and property drawers remain visually distinct from
         * ordinary comments. */
        if (current_mode == &org_mode && line_x == 0) {
          uintattr_t org_fg = 0;
          if (buffer[i] == '*') {
            size_t level = 0;
            while (i + level < len && buffer[i + level] == '*') level++;
            if (i + level < len && buffer[i + level] == ' ')
              org_fg = (uintattr_t)(2 + (level - 1) % 5);
          } else if (buffer[i] == '#' && i + 1 < len && buffer[i+1] == '+') {
            org_fg = 6;
          } else if (buffer[i] == ':' && i + 1 < len) {
            org_fg = 5;
          } else if (buffer[i] == '#') {
            org_fg = 5;
          }
          if (org_fg) {
            while (i < len && buffer[i] != '\n') {
              wrap_sync(buffer, line_s, line_l, screen_width, line_x,
                        &row_start, &row_end, &row_y);
              if (soft_wrap && row_y >= screen_height) break;
              sc = soft_wrap ? line_x - row_start : line_x - scroll_x;
              if (sc >= 0 && sc < screen_width)
                tb_set_cell(sc, row_y, display_ch(buffer[i]), org_fg, D_BACKGROUND);
              line_x++; i++;
            }
            continue;
          }
        }

        /* -- priority 2b: line comment / roff macro ------------------- */
        if ((line_x == 0 &&
             ((buffer[i] == '#' &&
              (current_mode == &sh_mode      ||
               current_mode == &python_mode  ||
               current_mode == &makefile_mode ||
               current_mode == NULL))
             ||
              (buffer[i] == '.' && current_mode == &roff_mode)
             ))
            || (buffer[i] == ';' &&
                (current_mode == &assembly_mode || current_mode == &lisp_mode))
            || (buffer[i] == '/' && i + 1 < len && buffer[i+1] == '/' &&
                mode_has_slash_comments(current_mode))
            || (buffer[i] == '-' && i + 1 < len && buffer[i+1] == '-' &&
                (current_mode == &lua_mode || current_mode == &sql_mode))) {
          while (i < len && buffer[i] != '\n') {
            wrap_sync(buffer, line_s, line_l, screen_width, line_x,
                      &row_start, &row_end, &row_y);
            if (soft_wrap && row_y >= screen_height) break;
            sc = soft_wrap ? line_x - row_start : line_x - scroll_x;
            if (sc >= 0 && sc < screen_width)
              tb_set_cell(sc, row_y, display_ch(buffer[i]), 5, D_BACKGROUND);
            line_x++; i++;
          }
          continue;
        }

        /* -- priority 2c: inside <...> -------------------------------- */
        if (in_atag) {
          tb_set_cell(sc, row_y, display_ch(buffer[i]), 5, D_BACKGROUND);
          if (buffer[i] == in_atag) in_atag = 0;
          line_x++; i++;
          continue;
        }
        /* -- priority 2d: inside string ------------------------------- */
        if (in_string) {
          tb_set_cell(sc, row_y, display_ch(buffer[i]), 3, D_BACKGROUND);
          if (buffer[i] == in_string && (i == 0 || buffer[i-1] != '\\'))
            in_string = 0;
          line_x++; i++;
          continue;
        }
        /* opening < */
        if (buffer[i] == '<') {
          in_atag = '>';
          tb_set_cell(sc, row_y, display_ch(buffer[i]), 5, D_BACKGROUND);
          line_x++; i++;
          continue;
        }
        /* opening quote */
        if (IS_STRING_QUOTE(buffer[i])) {
          in_string = buffer[i];
          tb_set_cell(sc, row_y, display_ch(buffer[i]), 3, D_BACKGROUND);
          line_x++; i++;
          continue;
        }

        /* -- priority 3: keyword -------------------------------------- */
        if (current_mode) {
          for (int k = 0; k < current_mode->keyword_count; k++) {
            const char *kw  = current_mode->keywords[k];
            size_t      kwl = strlen(kw);
            if ((mode_keywords_ignore_case(current_mode)
                   ? strncasecmp(buffer + i, kw, kwl)
                   : strncmp(buffer + i, kw, kwl)) == 0) {
              char before = (i > 0)         ? buffer[i - 1]   : ' ';
              char after  = (i + kwl < len) ? buffer[i + kwl] : ' ';
              if (!isalnum((unsigned char)before) && before != '_' &&
                  !isalnum((unsigned char)after)  && after  != '_') {
                /* stepped one cell at a time so a keyword split by a wrap
                 * still lands on the right rows */
                for (size_t j = 0; j < kwl; j++) {
                  wrap_sync(buffer, line_s, line_l, screen_width, line_x,
                            &row_start, &row_end, &row_y);
                  int ksc = soft_wrap ? line_x - row_start : line_x - scroll_x;
                  if (ksc >= 0 && ksc < screen_width)
                    tb_set_cell(ksc, row_y, display_ch(buffer[i]),
                                current_mode->keyword_color, D_BACKGROUND);
                  line_x++; i++;
                }
                keyword_drawn = 1;
                break;
              }
            }
          }
        }
        if (keyword_drawn) continue;

        /* -- priority 4: normal character ----------------------------- */
        tb_set_cell(sc, row_y, display_ch(buffer[i]), fg, bg);
      } else {
        /* left of scroll window: still need to track syntax state */
        if (mode_has_c_comments(current_mode)) {
          if (in_comment) {
            if (buffer[i] == '*' && i+1 < len && buffer[i+1] == '/') {
              i++; line_x++; in_comment = 0;
            }
          } else if (buffer[i] == '/' && i+1 < len && buffer[i+1] == '*') {
            i++; line_x++; in_comment = 1;
          }
        }
        if (!in_comment && !in_atag && !in_string) {
          if (buffer[i] == '<')  in_atag  = '>';
          else if (IS_STRING_QUOTE(buffer[i])) in_string = buffer[i];
        } else if (in_string) {
          if (buffer[i] == in_string && (i == 0 || buffer[i-1] != '\\'))
            in_string = 0;
        } else if (in_atag) {
          if (buffer[i] == in_atag) in_atag = 0;
        }
      }
      line_x++;
      i++;
    }
    /* Explicitly blank cells past EOL so stale content can't persist.
     * tb_clear() already set them to spaces, but the diff engine compares
     * fg too; writing here with D_FOREGROUND/D_BACKGROUND makes the attrs
     * match our normal-text rendering and guarantees a clean diff signal. */
    {
      int eol = soft_wrap ? line_x - row_start : line_x - scroll_x;
      if (eol < 0) eol = 0;
      for (int cx = eol; cx < screen_width; cx++)
        tb_set_cell(cx, row_y, ' ', D_FOREGROUND, D_BACKGROUND);
    }
    /* A row filled right out to the last column gets one extra, blank row.
     * Without a continuation glyph there is otherwise no cell left to show
     * the cursor in when it sits at the end of such a line.  wrap_rows_of()
     * counts this row too, so the scroll arithmetic agrees. */
    if (soft_wrap && line_x - row_start == screen_width) row_y++;
    screen_y = row_y;
    /* inner loop stopped at \n -- outer loop will handle it next */
  }

  /* If the buffer (or the visible tail of it) doesn't end in a newline, the
   * last line was drawn on row screen_y but nothing bumped screen_y for it --
   * only a literal '\n' byte does that above. Without this, the "blank any
   * remaining screen rows" loop below starts at that same row and immediately
   * erases the line we just drew. This is why text on a brand-new blank file,
   * or on the buffer's last (newline-less) line, was invisible until Enter
   * added a real '\n' and pushed screen_y past it. */
  if (i == len && i > 0 && buffer[i - 1] != '\n' && screen_y < screen_height)
    screen_y++;

  /* Blank any remaining screen rows (if buffer ended before filling screen).
   * tb_clear() already set them to spaces, but the diff engine needs explicit
   * attributes to signal these cells changed. */
  for (int remaining_y = screen_y; remaining_y < screen_height; remaining_y++) {
    for (int cx = 0; cx < screen_width; cx++)
      tb_set_cell(cx, remaining_y, ' ', D_FOREGROUND, D_BACKGROUND);
  }

  /* draw cursor on top of everything */
  int draw_y, draw_x;
  if (soft_wrap) {
    int crow, ccol;
    wrap_cursor_rowcol(buffer, len, screen_width, &crow, &ccol);
    draw_x = ccol;
    draw_y = (cursor_y < scroll_offset)
           ? -1                              /* above the window */
           : wrap_rows_between(buffer, len, scroll_offset, cursor_y,
                               screen_width) - scroll_row + crow;
  } else {
    draw_y = cursor_y - scroll_offset;
    draw_x = cursor_x - scroll_x;
  }
  if (draw_y >= 0 && draw_y < screen_height &&
      draw_x >= 0 && draw_x < screen_width) {
    size_t idx    = get_buffer_index_from_cursor(buffer, len);
    char cur_char = (idx < len) ? buffer[idx] : ' ';
    uint32_t display = (cur_char == ' ' || cur_char == '\n' || cur_char == '\t')
                     ? '_' : display_ch(cur_char);
    tb_set_cell(draw_x, draw_y, display, D_FOREGROUND, CURSOR_BG);
  }

  /* Completion ghost: drawn after the cursor so its first cell can keep
   * CURSOR_BG, and only ever at end of line, so no real text is covered. */
  ac_draw_ghost(buffer, len, draw_x, draw_y, screen_width, screen_height);

  draw_status_bar(status, buffer, len);
  draw_minibuffer();
#ifdef MENU_BAR_MODE
  menu_draw(tb_width());
#endif
  tb_present();
}

/* Erase the screen and repaint every cell from scratch.
 * tb_present() is diff-based: it only sends cells that changed vs the front
 * buffer.  If the physical terminal drifts (resize race, external writes,
 * SIGTSTP/continue) the diff sends nothing.  refresh_invalidate() discards
 * the front buffer and erases the screen so the next redraw_screen sends
 * every cell -- without tb_shutdown/tb_init, so the 256-color output mode
 * and input mode survive.
 */
void force_redraw(const char *buf, size_t len, const char *status) {
  refresh_invalidate();
  clamp_cursor(buf, len);
  redraw_screen(buf, len, status);
}

/* -- menu bar system ------------------------------------------------------- */

#ifdef MENU_BAR_MODE

void menu_init(void) {
  menu_state.menu_bar_active = 0;
  menu_state.current_menu = 0;
  menu_state.current_item = 0;
  menu_state.menu_count = 7;
#define MENU_BEGIN(n, name) do { strcpy(menu_state.menus[n].title, name); \
  menu_state.menus[n].item_count = 0; } while (0)
#define MENU_ADD(n, text, command) do { int _i = menu_state.menus[n].item_count++; \
  strcpy(menu_state.menus[n].items[_i].label, text); \
  menu_state.menus[n].items[_i].action = command; } while (0)

  MENU_BEGIN(0, "File");
  MENU_ADD(0, "New Buffer", MENU_ACTION_NEW_BUFFER);
  MENU_ADD(0, "Open File...", MENU_ACTION_OPEN_FILE);
  MENU_ADD(0, "Open Directory...", MENU_ACTION_OPEN_DIR);
  MENU_ADD(0, "Save", MENU_ACTION_SAVE);
  MENU_ADD(0, "Save As...", MENU_ACTION_SAVE_AS);
  MENU_ADD(0, "Kill Buffer", MENU_ACTION_KILL_BUFFER);
  MENU_ADD(0, "Quit", MENU_ACTION_QUIT);

  MENU_BEGIN(1, "Edit");
  MENU_ADD(1, "Undo", MENU_ACTION_UNDO);
  MENU_ADD(1, "Cut", MENU_ACTION_CUT);
  MENU_ADD(1, "Copy", MENU_ACTION_COPY);
  MENU_ADD(1, "Paste", MENU_ACTION_PASTE);
  MENU_ADD(1, "Select All", MENU_ACTION_SELECT_ALL);
  MENU_ADD(1, "Kill Line", MENU_ACTION_KILL_LINE);
  MENU_ADD(1, "Delete Character", MENU_ACTION_DELETE_CHAR);

  MENU_BEGIN(2, "Search");
  MENU_ADD(2, "Search Forward...", MENU_ACTION_SEARCH_FORWARD);
  MENU_ADD(2, "Search Backward...", MENU_ACTION_SEARCH_BACKWARD);
  MENU_ADD(2, "Query Replace...", MENU_ACTION_QUERY_REPLACE);

  MENU_BEGIN(3, "Mode");
  MENU_ADD(3, "Text (no highlighting)", MENU_ACTION_TEXT_MODE);
  MENU_ADD(3, "C", MENU_ACTION_C_MODE);
  MENU_ADD(3, "Python", MENU_ACTION_PYTHON_MODE);
  MENU_ADD(3, "Lisp / Scheme", MENU_ACTION_LISP_MODE);
  MENU_ADD(3, "Lua", MENU_ACTION_LUA_MODE);
  MENU_ADD(3, "Shell", MENU_ACTION_SH_MODE);
  MENU_ADD(3, "Makefile", MENU_ACTION_MAKEFILE_MODE);
  MENU_ADD(3, "Roff / man", MENU_ACTION_ROFF_MODE);
  MENU_ADD(3, "Markdown", MENU_ACTION_MARKDOWN_MODE);
  MENU_ADD(3, "Org", MENU_ACTION_ORG_MODE);
  MENU_ADD(3, "JavaScript / TypeScript", MENU_ACTION_JAVASCRIPT_MODE);
  MENU_ADD(3, "Rust", MENU_ACTION_RUST_MODE);
  MENU_ADD(3, "Pascal", MENU_ACTION_PASCAL_MODE);
  MENU_ADD(3, "Assembly", MENU_ACTION_ASSEMBLY_MODE);
  MENU_ADD(3, "Java", MENU_ACTION_JAVA_MODE);
  MENU_ADD(3, "Go", MENU_ACTION_GO_MODE);
  MENU_ADD(3, "C#", MENU_ACTION_CSHARP_MODE);
  MENU_ADD(3, "SQL", MENU_ACTION_SQL_MODE);
  MENU_ADD(3, "JSON", MENU_ACTION_JSON_MODE);
  MENU_ADD(3, "HTML / XML", MENU_ACTION_HTML_MODE);
  MENU_ADD(3, "CSS", MENU_ACTION_CSS_MODE);

  MENU_BEGIN(4, "Buffers");
  MENU_ADD(4, "Next Buffer", MENU_ACTION_NEXT_BUFFER);
  MENU_ADD(4, "List Buffers", MENU_ACTION_LIST_BUFFERS);
  MENU_ADD(4, "Kill Buffer", MENU_ACTION_KILL_BUFFER);

  MENU_BEGIN(5, "Options");
  MENU_ADD(5, "Toggle Soft Wrap", MENU_ACTION_SOFT_WRAP);
  MENU_ADD(5, "Toggle Single Quotes", MENU_ACTION_SINGLE_QUOTES);
  MENU_ADD(5, "Toggle Nano Bar", MENU_ACTION_NANO_MODE);

  MENU_BEGIN(6, "Help");
  MENU_ADD(6, "Keyboard Help", MENU_ACTION_HELP);
#undef MENU_ADD
#undef MENU_BEGIN
}

void menu_draw(int screen_width) {
  if (!menu_state.menu_bar_active) return;

  int y = 0;
  int x = 0;

  /* draw menu titles on top row: white text on black background */
  for (int i = 0; i < menu_state.menu_count; i++) {
    int is_selected = (i == menu_state.current_menu);
    int fg = is_selected ? 3 : 7;    /* selected: yellow; normal: white */
    int bg = is_selected ? 8 : 0;    /* selected: grey; normal: black */

    for (char *p = menu_state.menus[i].title; *p && x < screen_width; p++) {
      tb_set_cell(x++, y, *p, fg, bg);
    }
    if (x < screen_width) tb_set_cell(x++, y, ' ', fg, bg);
  }

  /* fill rest of menu bar with black */
  while (x < screen_width) {
    tb_set_cell(x++, y, ' ', 7, 0);
  }

  /* draw submenu if menu is selected */
  if (menu_state.menu_bar_active && menu_state.current_menu >= 0 &&
      menu_state.current_menu < menu_state.menu_count) {
    menu_t *menu = &menu_state.menus[menu_state.current_menu];
    for (int i = 0; i < menu->item_count && i + 1 < tb_height() - 2; i++) {
      int is_selected = (i == menu_state.current_item);
      int fg = is_selected ? 3 : 7;  /* selected: yellow; normal: white */
      int bg = is_selected ? 8 : 0;  /* selected: grey; normal: black */

      int item_y = i + 1;
      int item_x = 0;

      for (char *p = menu->items[i].label; *p && item_x < screen_width; p++) {
        tb_set_cell(item_x++, item_y, *p, fg, bg);
      }

      while (item_x < screen_width) {
        tb_set_cell(item_x++, item_y, ' ', fg, bg);
      }
    }
  }
}

void menu_navigate_left(void) {
  menu_state.current_menu--;
  if (menu_state.current_menu < 0)
    menu_state.current_menu = menu_state.menu_count - 1;
  menu_state.current_item = 0;
}

void menu_navigate_right(void) {
  menu_state.current_menu++;
  if (menu_state.current_menu >= menu_state.menu_count)
    menu_state.current_menu = 0;
  menu_state.current_item = 0;
}

void menu_navigate_up(void) {
  if (menu_state.current_menu >= 0 && menu_state.current_menu < menu_state.menu_count) {
    menu_state.current_item--;
    if (menu_state.current_item < 0)
      menu_state.current_item = menu_state.menus[menu_state.current_menu].item_count - 1;
  }
}

void menu_navigate_down(void) {
  if (menu_state.current_menu >= 0 && menu_state.current_menu < menu_state.menu_count) {
    menu_state.current_item++;
    if (menu_state.current_item >= menu_state.menus[menu_state.current_menu].item_count)
      menu_state.current_item = 0;
  }
}

menu_action_t menu_select(void) {
  if (menu_state.current_menu >= 0 && menu_state.current_menu < menu_state.menu_count) {
    menu_t *menu = &menu_state.menus[menu_state.current_menu];
    if (menu_state.current_item >= 0 && menu_state.current_item < menu->item_count) {
      return menu->items[menu_state.current_item].action;
    }
  }
  return MENU_ACTION_NONE;
}

void menu_toggle(void) {
  menu_state.menu_bar_active = !menu_state.menu_bar_active;
  if (menu_state.menu_bar_active) {
    menu_state.current_menu = 0;
    menu_state.current_item = 0;
  }
}

#endif /* MENU_BAR_MODE */

void menu_handle_action(menu_action_t action, char **buffer, size_t *len, char *filename) {
#ifdef MENU_BAR_MODE
  menu_state.menu_bar_active = 0;
#endif

  switch (action) {
    case MENU_ACTION_NEW_BUFFER: {
      if (num_bufs < MAX_OPEN_BUFFERS) {
        buf_save_to_slot(current_buf);
        int new_buf = num_bufs++;
        if (buf_alloc(new_buf) == 0) {
          current_buf = new_buf;
          buf_load_from_slot(new_buf, buffer, filename);
        }
        set_status("New buffer created");
      } else {
        set_status("Max buffers reached");
      }
      break;
    }
    case MENU_ACTION_OPEN_FILE: {
      minibuffer_active = 1;
      search_forward_active = 0;
      search_backward_active = 0;
      replace_state = 0;
      minibuffer_length = 0;
      minibuffer_cursor = 0;
      memset(minibuffer_input, 0, MINIBUFFER_MAX);
      saved_cursor_x = cursor_x;
      saved_cursor_y = cursor_y;
      set_status("C-x C-f to open file");
      break;
    }
    case MENU_ACTION_OPEN_DIR: {
      minibuffer_active = 1;
      search_forward_active = 0;
      search_backward_active = 0;
      replace_state = 0;
      minibuffer_length = 0;
      minibuffer_cursor = 0;
      memset(minibuffer_input, 0, MINIBUFFER_MAX);
      strncpy(minibuffer_input, "dired ", MINIBUFFER_MAX - 1);
      minibuffer_length = 6;
      minibuffer_cursor = 6;
      saved_cursor_x = cursor_x;
      saved_cursor_y = cursor_y;
      break;
    }
    case MENU_ACTION_SAVE: {
      if (strcmp(filename, "_scratch_") != 0) {
        if (save_to_file(filename, *buffer, *len) == 0)
          set_status("Saved: %s", filename);
        else
          set_status("Cannot write %s", filename);
      } else {
        set_status("Cannot save _scratch_ buffer - use Save As");
      }
      break;
    }
    case MENU_ACTION_SAVE_AS: {
      minibuffer_active = 1;
      search_forward_active = 0;
      search_backward_active = 0;
      replace_state = 0;
      minibuffer_length = 0;
      minibuffer_cursor = 0;
      memset(minibuffer_input, 0, MINIBUFFER_MAX);
      /* Save As prefilled "find-file ", which opens a file rather than
       * writing this one out -- the opposite of what the item promises. */
      strncpy(minibuffer_input, "write-file ", MINIBUFFER_MAX - 1);
      minibuffer_length = 11;
      minibuffer_cursor = 11;
      saved_cursor_x = cursor_x;
      saved_cursor_y = cursor_y;
      break;
    }
    case MENU_ACTION_KILL_BUFFER: {
      buf_kill(current_buf, buffer, filename);
      break;
    }
    case MENU_ACTION_QUIT: {
      if (modified) {
        set_status("File has unsaved changes. C-x C-c to quit");
      } else {
        tb_shutdown();
        exit(0);
      }
      break;
    }
    case MENU_ACTION_UNDO: {
      if (undo_pop(*buffer, len)) {
        fileref.lines = countlines_buf(*buffer, *len);
        clamp_cursor(*buffer, *len);
        set_status("Undo!");
      } else set_status("Nothing to undo");
      break;
    }
    case MENU_ACTION_COPY: {
      if (mark_active) {
        copy_region(*buffer, *len);
        set_status("Region copied");
      } else {
        set_status("No region selected");
      }
      break;
    }
    case MENU_ACTION_CUT: {
      if (mark_active) {
        /* undo_push and the line-count refresh are what C-w does; without
         * them a cut made from the menu (or from M-x kill-region) could not
         * be undone and left the status bar reporting the old line count. */
        undo_push(*buffer, *len);
        kill_region(*buffer, len);
        mark_active = 0;
        fileref.lines = countlines_buf(*buffer, *len);
        clamp_cursor(*buffer, *len);
        set_status("Region cut");
      } else {
        set_status("No region selected");
      }
      break;
    }
    case MENU_ACTION_PASTE: {
      mark_active = 0;
      undo_push(*buffer, *len);          /* as C-y does; see MENU_ACTION_CUT */
      yank(*buffer, BUFFER_SIZE, len);
      fileref.lines = countlines_buf(*buffer, *len);
      clamp_cursor(*buffer, *len);
      set_status("Pasted from kill ring");
      break;
    }
    case MENU_ACTION_SELECT_ALL: {
      mark_pos = 0;
      mark_active = 1;
      set_cursor_from_index(*buffer, *len);
      clamp_cursor(*buffer, *len);
      set_status("All text selected");
      break;
    }
    case MENU_ACTION_KILL_LINE: {
      undo_push(*buffer, *len);
      kill_to_end_of_line(*buffer, len);
      fileref.lines = countlines_buf(*buffer, *len);
      set_status("Line killed");
      break;
    }
    case MENU_ACTION_DELETE_CHAR: {
      undo_push(*buffer, *len);
      delete_char(*buffer, len);
      clamp_cursor(*buffer, *len);
      set_status("Character deleted");
      break;
    }
    case MENU_ACTION_SEARCH_FORWARD:
    case MENU_ACTION_SEARCH_BACKWARD: {
      minibuffer_active = 1;
      search_forward_active = action == MENU_ACTION_SEARCH_FORWARD;
      search_backward_active = action == MENU_ACTION_SEARCH_BACKWARD;
      replace_state = 0;
      minibuffer_length = 0;
      minibuffer_cursor = 0;
      memset(minibuffer_input, 0, MINIBUFFER_MAX);
      saved_cursor_x = cursor_x;
      saved_cursor_y = cursor_y;
      break;
    }
    case MENU_ACTION_QUERY_REPLACE: {
      minibuffer_active = 1;
      search_forward_active = search_backward_active = 0;
      replace_state = 1;
      minibuffer_length = minibuffer_cursor = 0;
      minibuffer_input[0] = '\0';
      saved_cursor_x = cursor_x; saved_cursor_y = cursor_y;
      break;
    }
    case MENU_ACTION_TEXT_MODE: {
      current_mode = NULL;
      set_status("Text mode");
      break;
    }
    case MENU_ACTION_C_MODE: {
      current_mode = &c_mode;
      set_status("C-mode");
      break;
    }
    case MENU_ACTION_PYTHON_MODE: {
      current_mode = &python_mode;
      set_status("Python-mode");
      break;
    }
    case MENU_ACTION_LISP_MODE: {
      current_mode = &lisp_mode;
      set_status("Lisp-mode");
      break;
    }
    case MENU_ACTION_LUA_MODE: {
      current_mode = &lua_mode;
      set_status("Lua-mode");
      break;
    }
    case MENU_ACTION_SH_MODE: {
      current_mode = &sh_mode;
      set_status("Sh-mode");
      break;
    }
    case MENU_ACTION_MAKEFILE_MODE: {
      current_mode = &makefile_mode;
      set_status("Makefile-mode");
      break;
    }
    case MENU_ACTION_ROFF_MODE: {
      current_mode = &roff_mode;
      set_status("Roff-mode");
      break;
    }
    case MENU_ACTION_MARKDOWN_MODE: {
      current_mode = &markdown_mode;
      set_status("Markdown-mode");
      break;
    }
    case MENU_ACTION_ORG_MODE: {
      current_mode = &org_mode;
      set_status("Org-mode");
      break;
    }
#define MODE_MENU_CASE(action, mode, label) \
    case action: { current_mode = &mode; set_status(label); break; }
    MODE_MENU_CASE(MENU_ACTION_JAVASCRIPT_MODE, javascript_mode, "JavaScript-mode")
    MODE_MENU_CASE(MENU_ACTION_RUST_MODE, rust_mode, "Rust-mode")
    MODE_MENU_CASE(MENU_ACTION_PASCAL_MODE, pascal_mode, "Pascal-mode")
    MODE_MENU_CASE(MENU_ACTION_ASSEMBLY_MODE, assembly_mode, "Assembly-mode")
    MODE_MENU_CASE(MENU_ACTION_JAVA_MODE, java_mode, "Java-mode")
    MODE_MENU_CASE(MENU_ACTION_GO_MODE, go_mode, "Go-mode")
    MODE_MENU_CASE(MENU_ACTION_CSHARP_MODE, csharp_mode, "Csharp-mode")
    MODE_MENU_CASE(MENU_ACTION_SQL_MODE, sql_mode, "SQL-mode")
    MODE_MENU_CASE(MENU_ACTION_JSON_MODE, json_mode, "JSON-mode")
    MODE_MENU_CASE(MENU_ACTION_HTML_MODE, html_mode, "HTML-mode")
    MODE_MENU_CASE(MENU_ACTION_CSS_MODE, css_mode, "CSS-mode")
#undef MODE_MENU_CASE
    case MENU_ACTION_LIST_BUFFERS: {
      /* The list goes on the status line: there is no window to split, and a
       * buffer of its own would itself need a slot.  The current buffer's
       * name and modified flag live in the globals -- buf_slots[current_buf]
       * only catches up at the next buf_save_to_slot(). */
      char list[200];
      int  pos = 0;
      for (int i = 0; i < num_bufs; i++) {
        const char *name = (i == current_buf) ? fileref.name
                                              : buf_slots[i].fileref.name;
        int mod = (i == current_buf) ? modified : buf_slots[i].modified;
        int n = snprintf(list + pos, sizeof(list) - (size_t)pos, "%s%d:%s%s",
                         pos ? "  " : "", i + 1, mod ? "**" : "",
                         name[0] ? name : "_scratch_");
        if (n < 0) break;
        if (n >= (int)sizeof(list) - pos) break;   /* truncated: stop here */
        pos += n;
      }
      list[pos] = '\0';
      set_status("Buffers: %s", list);
      break;
    }
    case MENU_ACTION_NEXT_BUFFER: {
      if (num_bufs > 1) {
        buf_save_to_slot(current_buf);
        buf_load_from_slot((current_buf + 1) % num_bufs, buffer, filename);
        set_status("%s  [%d/%d]", filename, current_buf + 1, num_bufs);
      } else set_status("Only one buffer");
      break;
    }
    case MENU_ACTION_SOFT_WRAP: {
      soft_wrap = !soft_wrap; scroll_x = scroll_row = 0;
      clamp_cursor(*buffer, *len);
      set_status(soft_wrap ? "Soft wrap enabled" : "Soft wrap disabled");
      break;
    }
    case MENU_ACTION_SINGLE_QUOTES: {
      highlight_single_quotes = !highlight_single_quotes;
      set_status(highlight_single_quotes ? "Single-quote highlighting enabled"
                                         : "Single-quote highlighting disabled");
      break;
    }
    case MENU_ACTION_HELP: {
      open_help_buffer(buffer, filename);
      break;
    }
    default:
      break;
  }
}

/* -- multiplexer split support (tmux / GNU screen) ------------------------- */

typedef enum { MUX_NONE, MUX_TMUX, MUX_SCREEN } mux_t;

/* Default to PATH lookup, but main() replaces this with a sibling path when
 * tbg itself was invoked through one (for example ./tbg -> ./tbv). */
static char tbv_command[PATH_MAX] = "tbv";

static void set_tbv_command(const char *argv0) {
  const char *slash = argv0 ? strrchr(argv0, '/') : NULL;
  if (!slash) return;
  size_t dirlen = (size_t)(slash - argv0);
  if (dirlen + sizeof("/tbv") > sizeof(tbv_command)) return;
  memcpy(tbv_command, argv0, dirlen);
  memcpy(tbv_command + dirlen, "/tbv", sizeof("/tbv"));
}

static mux_t detect_mux(void) {
  if (getenv("TMUX")) return MUX_TMUX;
  if (getenv("STY"))  return MUX_SCREEN;
  return MUX_NONE;
}

/* fork+exec argv[0]; parent returns immediately; child stdin/out/err → /dev/null */
static int mux_run(char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
    setsid();
    execvp(argv[0], argv);
    _exit(127);
  }
  return 0;
}

/* pane_below=1 → C-x 2 (below), pane_below=0 → C-x 3 (right) */
static void mux_split(int pane_below, const char *fname) {
  mux_t mux = detect_mux();
  int has_file = fname && strcmp(fname, "_scratch_") != 0;

  if (mux == MUX_TMUX) {
    const char *dir = pane_below ? "-v" : "-h";
    if (has_file) {
      char *av[] = { "tmux", "split-window", (char *)dir, "tbg", (char *)fname, NULL };
      mux_run(av);
    } else {
      char *av[] = { "tmux", "split-window", (char *)dir, "tbg", NULL };
      mux_run(av);
    }
  } else if (mux == MUX_SCREEN) {
    /* split the region, focus new region, open tbg there */
    if (pane_below) {
      char *av[] = { "screen", "-X", "split",  NULL };         mux_run(av);
    } else {
      char *av[] = { "screen", "-X", "vsplit", NULL };         mux_run(av);
    }
    { char *av[] = { "screen", "-X", "focus",  NULL };         mux_run(av); }
    if (has_file) {
      char *av[] = { "screen", "-X", "screen", "tbg", (char *)fname, NULL };
      mux_run(av);
    } else {
      char *av[] = { "screen", "-X", "screen", "tbg", NULL };
      mux_run(av);
    }
  } else {
    set_status("C-x %c: run tbg inside tmux or screen to split windows",
               pane_below ? '2' : '3');
  }
}

static void mux_unsplit(void) {
  mux_t mux = detect_mux();
  if (mux == MUX_TMUX) {
    /* move all other panes to their own windows (keeps them alive), stop when
       break-pane returns non-zero (no more panes to move) */
    char *av[] = { "sh", "-c",
      "i=0; while [ \"$i\" -lt 31 ] && "
        "tmux break-pane -d -s :.+ 2>/dev/null; do i=$((i+1)); done",
      NULL };
    mux_run(av);
  } else if (mux == MUX_SCREEN) {
    /* screen 'only' removes other regions but keeps their windows running */
    char *av[] = { "screen", "-X", "only", NULL };
    mux_run(av);
  } else {
    set_status("C-x 1: no multiplexer");
  }
}

static void mux_other_pane(void) {
  mux_t mux = detect_mux();
  if (mux == MUX_TMUX) {
    /* next pane in this window; fall back to next window if only one pane */
    char *av[] = { "sh", "-c",
      "tmux select-pane -t :.+ 2>/dev/null || tmux next-window 2>/dev/null",
      NULL };
    mux_run(av);
  } else if (mux == MUX_SCREEN) {
    char *av[] = { "screen", "-X", "focus", NULL };
    mux_run(av);
  } else {
    set_status("C-x o: no multiplexer");
  }
}

/* M-x evil-mode is intentionally a process boundary, not an in-process keymap
 * toggle: tbg and tbv keep their small, independent editing cores.  tmux
 * focuses a newly split pane by default, which makes this feel like entering
 * the vi-style editor while leaving the original tbg session intact. */
static void mux_evil_mode(const char *fname) {
  if (detect_mux() != MUX_TMUX) {
    set_status("evil-mode: tbg must be running inside tmux");
    return;
  }

  int has_file = fname && strcmp(fname, "_scratch_") != 0;
  int rc;
  if (has_file) {
    char *av[] = { "tmux", "split-window", "-h", tbv_command, (char *)fname, NULL };
    rc = mux_run(av);
  } else {
    char *av[] = { "tmux", "split-window", "-h", tbv_command, NULL };
    rc = mux_run(av);
  }

  if (rc < 0)
    set_status("evil-mode: unable to start tmux split");
  else if (modified && has_file)
    set_status("evil-mode: tbv opened the saved version; tbg has unsaved changes");
  else
    set_status("evil-mode: opened tbv in a tmux pane");
}

/* -- main ------------------------------------------------------------------ */

#include "dired.h"

/* Quote one argument for the shell command accepted by tmux new-session. */
static int shell_quote(char *dst, size_t cap, const char *src) {
  size_t pos = 0;
  if (cap < 3) return -1;
  dst[pos++] = '\'';
  while (*src) {
    if (*src == '\'') {
      static const char escaped[] = "'\\''";
      if (pos + sizeof(escaped) - 1 >= cap) return -1;
      memcpy(dst + pos, escaped, sizeof(escaped) - 1);
      pos += sizeof(escaped) - 1;
    } else {
      if (pos + 1 >= cap) return -1;
      dst[pos++] = *src;
    }
    src++;
  }
  if (pos + 2 > cap) return -1;
  dst[pos++] = '\'';
  dst[pos] = '\0';
  return (int)pos;
}

static int launch_in_tmux(const char *program, const char *filename) {
  char command[1024];
  int used = shell_quote(command, sizeof(command), program);
  if (used < 0) return -1;
  if (filename) {
    if ((size_t)used + 1 >= sizeof(command)) return -1;
    command[used++] = ' ';
    int n = shell_quote(command + used, sizeof(command) - (size_t)used, filename);
    if (n < 0) return -1;
  }
  char *av[] = { "tmux", "new-session", command, NULL };
  execvp(av[0], av);
  return -1;
}

int main(int argc, char *argv[]) {
  set_tbv_command(argv[0]);

  if (argc > 1 && strcmp(argv[1], "--tmux") == 0) {
    if (!getenv("TMUX")) {
      if (argc > 3) {
        fprintf(stderr, "usage: %s [--tmux] [file]\n", argv[0]);
        return 2;
      }
      if (launch_in_tmux(argv[0], argc == 3 ? argv[2] : NULL) < 0) {
        perror("tbg: unable to start tmux");
        return 1;
      }
    }
    /* Already in tmux: remove the launcher option and open the file normally. */
    argv++;
    argc--;
  }

  if (tb_init()) return 1;
  tb_set_output_mode(TB_OUTPUT_256);

  char filename[256];
  filename[sizeof(filename) - 1] = '\0';

  const size_t buffer_size = BUFFER_SIZE;

  /* allocate slot 0 (the initial buffer) */
  if (buf_alloc(0) != 0) { tb_shutdown(); return 1; }
  char *buffer = buf_slots[0].data;
  for (int i = 0; i < UNDO_DEPTH; i++)
    undo_ring[i].data = buf_slots[0].undo[i].data;

  if (argc > 1) {
    /* open the file given on the command line */
    strncpy(filename, argv[1], sizeof(filename) - 1);
    strcpy(fileref.name, filename);
    FILE *fp = fopen(filename, "r");
    if (fp) {
      length = fread(buffer, 1, buffer_size - 1, fp);
      buffer[length] = '\0';
      fclose(fp);
      if (length >= buffer_size - 1)
        set_status("WARNING: file truncated at %zu bytes!", length);
    }
    detect_major_mode(filename);
  } else {
    /* no argument: start with an empty _scratch_ buffer */
    strncpy(filename, "_scratch_", sizeof(filename) - 1);
    strcpy(fileref.name, "_scratch_");
    length = 0;
    buffer[0] = '\0';
  }

  fileref.lines = countlines_buf(buffer, length);
  buf_save_to_slot(0); /* snapshot initial state so cycling saves correctly */

  int awaiting_ctrl_x = 0;
  int awaiting_ctrl_c = 0;
  int pending_kill_line = 0; /* FIX: dispatch kill-line from main loop */
  int pending_delete_char = 0;
  int pending_search = 0;    /* 0 none, 1 forward, 2 backward -- see M-x */
  int need_force_redraw = 0;

  tb_set_input_mode(TB_INPUT_ALT);
  /* tb_set_input_mode(TB_INPUT_ALT | TB_INPUT_MOUSE); -- mouse disabled: breaks terminal copy/paste */
#ifdef MENU_BAR_MODE
  menu_init();
#endif
  redraw_screen(buffer, length, status_msg);

  while (1) {
    struct tb_event ev;
    tb_poll_event(&ev);

    /* -- menu bar handling ------------------------------------------------ */
#ifdef MENU_BAR_MODE
    if (menu_state.menu_bar_active && ev.type == TB_EVENT_KEY) {
      if (ev.key == TB_KEY_F10) {
        menu_toggle();
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      if (ev.key == TB_KEY_ESC) {
        menu_state.menu_bar_active = 0;
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      if (ev.key == TB_KEY_ARROW_LEFT) {
        menu_navigate_left();
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      if (ev.key == TB_KEY_ARROW_RIGHT) {
        menu_navigate_right();
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      if (ev.key == TB_KEY_ARROW_UP) {
        menu_navigate_up();
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      if (ev.key == TB_KEY_ARROW_DOWN) {
        menu_navigate_down();
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      if (ev.key == TB_KEY_ENTER) {
        menu_action_t action = menu_select();
        menu_handle_action(action, &buffer, &length, filename);
        redraw_screen(buffer, length, status_msg);
        continue;
      }
      /* ignore all other keys when menu is active */
      redraw_screen(buffer, length, status_msg);
      continue;
    }
#endif

    /* -- minibuffer input ------------------------------------------------- */
    if (minibuffer_active) {
      /* scroll wheel while minibuffer open -- disabled with mouse support
      if (ev.type == TB_EVENT_MOUSE) {
        if (ev.key == TB_KEY_MOUSE_WHEEL_UP) {
          scroll_offset -= 3;
          if (scroll_offset < 0) scroll_offset = 0;
        } else if (ev.key == TB_KEY_MOUSE_WHEEL_DOWN) {
          int ml = countlines_buf(buffer, length);
          scroll_offset += 3;
          if (scroll_offset >= ml) scroll_offset = ml - 1;
        }
        redraw_screen(buffer, length, status_msg);
        continue;
      } */
      if (ev.type == TB_EVENT_KEY) {

        /* UX-3: ESC or C-g cancels the minibuffer */
        if (ev.key == TB_KEY_ESC || ev.key == 0x07) {
          minibuffer_active      = 0;
          search_forward_active  = 0;
          search_backward_active = 0;
          replace_state          = 0;
          replace_match_pos     = -1;
          search_match_pos      = -1;
          search_match_len      = 0;
          memset(minibuffer_input, 0, MINIBUFFER_MAX);
          minibuffer_length = 0;
          minibuffer_cursor = 0;
          cursor_x = saved_cursor_x;
          cursor_y = saved_cursor_y;
          clamp_cursor(buffer, length);
          status_msg = NULL;
          redraw_screen(buffer, length, status_msg);
          continue;
        }

        /* TAB: complete the M-x command name, else the find-file path.  A
         * command name is tried first because it only ever matches on a bare
         * M-x line whose text is still a prefix of a command -- once a path
         * is being typed nothing matches and completion falls through. */
        if (ev.key == TB_KEY_TAB) {
          if (!ac_mx_complete()) complete_find_file();
          redraw_screen(buffer, length, status_msg);
          continue;
        }

        /* UX-6: left/right arrows move cursor within minibuffer */
        if (ev.key == TB_KEY_ARROW_LEFT) {
          if (minibuffer_cursor > 0) minibuffer_cursor--;
          redraw_screen(buffer, length, status_msg);
          continue;
        }
        if (ev.key == TB_KEY_ARROW_RIGHT) {
          if (minibuffer_cursor < minibuffer_length) minibuffer_cursor++;
          redraw_screen(buffer, length, status_msg);
          continue;
        }

        /* C-s while searching: jump to next forward occurrence */
        if ((search_forward_active || search_backward_active) && ev.key == TB_KEY_CTRL_S) {
          if (minibuffer_length > 0) {
            size_t from = (search_match_pos >= 0)
                        ? (size_t)search_match_pos + search_match_len
                        : 0;
            int m = find_next_match(buffer, length, minibuffer_input, from);
            if (m < 0) m = find_next_match(buffer, length, minibuffer_input, 0);
            if (m >= 0) {
              search_match_pos = m;
              search_match_len = strlen(minibuffer_input);
              set_cursor_from_index(buffer, (size_t)m);
              clamp_cursor(buffer, length);
            } else {
              set_status("No match: %s", minibuffer_input);
            }
          }
          redraw_screen(buffer, length, status_msg);
          continue;
        }

        /* C-r while searching: jump to previous occurrence */
        if ((search_forward_active || search_backward_active) && ev.key == TB_KEY_CTRL_R) {
          if (minibuffer_length > 0) {
            size_t before = (search_match_pos >= 0) ? (size_t)search_match_pos : length;
            int m = find_prev_match(buffer, length, minibuffer_input, before);
            if (m < 0) m = find_prev_match(buffer, length, minibuffer_input, length);
            if (m >= 0) {
              search_match_pos = m;
              search_match_len = strlen(minibuffer_input);
              set_cursor_from_index(buffer, (size_t)m);
              clamp_cursor(buffer, length);
            } else {
              set_status("No match: %s", minibuffer_input);
            }
          }
          redraw_screen(buffer, length, status_msg);
          continue;
        }

        if (ev.key == TB_KEY_ENTER) {
          /* replace state 1: got search term, ask for replacement */
          if (replace_state == 1) {
            strncpy(replace_search, minibuffer_input, MINIBUFFER_MAX - 1);
            replace_search[MINIBUFFER_MAX - 1] = '\0';
            replace_state = 2;
            memset(minibuffer_input, 0, MINIBUFFER_MAX);
            minibuffer_length = 0;
            minibuffer_cursor = 0;
            redraw_screen(buffer, length, status_msg);
            continue;
          }
          /* replace state 2: got replacement, find first match */
          if (replace_state == 2) {
            strncpy(replace_with, minibuffer_input, MINIBUFFER_MAX - 1);
            replace_with[MINIBUFFER_MAX - 1] = '\0';
            minibuffer_active = 0;
            int m = find_next_match(buffer, length, replace_search, 0);
            if (m >= 0) {
              replace_state     = 3;
              replace_match_pos = m;
              search_match_pos  = m;
              search_match_len  = strlen(replace_search);
              set_cursor_from_index(buffer, (size_t)m);
              clamp_cursor(buffer, length);
              set_status("Replace '%s' -> '%s'? (y)es (n)o (!)all (q)uit",
                         replace_search, replace_with);
            } else {
              replace_state = 0;
              set_status("No match for '%s'", replace_search);
            }
            redraw_screen(buffer, length, status_msg);
            continue;
          }
          /* dispatch M-x commands */
          if (!strcmp(minibuffer_input, "help") ||
              !strcmp(minibuffer_input, "help-commands") ||
              !strcmp(minibuffer_input, "describe-bindings") ||
              !strcmp(minibuffer_input, "help-keys")) {
            open_help_buffer(&buffer, filename);
          }
          else if (!strcmp(minibuffer_input, "kill-line")) {
            pending_kill_line = 1;
          }
          else if (!strcmp(minibuffer_input, "delete-char")) {
            pending_delete_char = 1;
          }
          else if (!strcmp(minibuffer_input, "evil-mode")) {
            mux_evil_mode(filename);
          }
          /* One chain, not two: started as a second "if" this branch broke
           * the dispatch in half, so help / kill-line / delete-char matched
           * in the first half, fell into the second, missed every test there
           * and were reported "Unknown command" after running. */
          else if (!strcmp(minibuffer_input, "c-mode")) {
            current_mode = &c_mode;
          }
          else if (!strcmp(minibuffer_input, "redraw-screen")) {
            need_force_redraw = 1;
          }
          else if (!strcmp(minibuffer_input, "python-mode")) {
            current_mode = &python_mode;
          }
          else if (!strcmp(minibuffer_input, "lisp-mode")) {
            current_mode = &lisp_mode;
          }
          else if (!strcmp(minibuffer_input, "sh-mode")) {
            current_mode = &sh_mode;
          }
          else if (!strcmp(minibuffer_input, "lua-mode")) {
            current_mode = &lua_mode;
          }
          else if (!strcmp(minibuffer_input, "roff-mode")) {
            current_mode = &roff_mode;
          }
          else if (!strcmp(minibuffer_input, "markdown-mode")) {
            current_mode = &markdown_mode;
          }
          else if (!strcmp(minibuffer_input, "org-mode")) {
            current_mode = &org_mode;
          }
          else if (!strcmp(minibuffer_input, "makefile-mode")) {
            current_mode = &makefile_mode;
          }
#define MX_MODE(command, mode) \
          else if (!strcmp(minibuffer_input, command)) { current_mode = &mode; }
          MX_MODE("javascript-mode", javascript_mode)
          MX_MODE("rust-mode", rust_mode)
          MX_MODE("pascal-mode", pascal_mode)
          MX_MODE("assembly-mode", assembly_mode)
          MX_MODE("java-mode", java_mode)
          MX_MODE("go-mode", go_mode)
          MX_MODE("csharp-mode", csharp_mode)
          MX_MODE("sql-mode", sql_mode)
          MX_MODE("json-mode", json_mode)
          MX_MODE("html-mode", html_mode)
          MX_MODE("css-mode", css_mode)
#undef MX_MODE
          else if (!strcmp(minibuffer_input, "menu-bar-mode")) {
#ifdef MENU_BAR_MODE
            menu_toggle();
            set_status(menu_state.menu_bar_active
                       ? "Menu bar enabled" : "Menu bar disabled");
#else
            set_status("There is no support built for menu-bar-mode.");
#endif
          }

          else if (!strcmp(minibuffer_input, "soft-wrap-mode")) {
            soft_wrap  = !soft_wrap;
            scroll_x   = 0;
            scroll_row = 0;
            clamp_cursor(buffer, length);
            set_status(soft_wrap ? "Soft wrap enabled" : "Soft wrap disabled");
          }
          else if (!strcmp(minibuffer_input, "toggle-single-quotes")) {
            highlight_single_quotes = !highlight_single_quotes;
            set_status(highlight_single_quotes
                       ? "Single-quote highlighting enabled"
                       : "Single-quote highlighting disabled");
          }
          else if (!strcmp(minibuffer_input, "kill-buffer")) {
            buf_kill(current_buf, &buffer, filename);
          }
          /* The menu bar's own actions (menubar.h), reachable by name.  Each
           * hands the work straight to menu_handle_action() so the item and
           * the command cannot drift apart.  The three menu items that only
           * prefill the minibuffer -- Open File, Open Directory, Save As --
           * are find-file, dired and write-file here instead. */
          else if (!strcmp(minibuffer_input, "new-buffer")) {
            menu_handle_action(MENU_ACTION_NEW_BUFFER, &buffer, &length, filename);
          }
          else if (!strcmp(minibuffer_input, "save-buffer")) {
            menu_handle_action(MENU_ACTION_SAVE, &buffer, &length, filename);
          }
          else if (!strcmp(minibuffer_input, "list-buffers")) {
            menu_handle_action(MENU_ACTION_LIST_BUFFERS, &buffer, &length, filename);
          }
          else if (!strcmp(minibuffer_input, "kill-ring-save")) {
            menu_handle_action(MENU_ACTION_COPY, &buffer, &length, filename);
          }
          else if (!strcmp(minibuffer_input, "kill-region")) {
            menu_handle_action(MENU_ACTION_CUT, &buffer, &length, filename);
          }
          else if (!strcmp(minibuffer_input, "yank")) {
            menu_handle_action(MENU_ACTION_PASTE, &buffer, &length, filename);
          }
          else if (!strcmp(minibuffer_input, "quit")) {
            menu_handle_action(MENU_ACTION_QUIT, &buffer, &length, filename);
          }
          /* Search has to re-open the minibuffer this dispatch is about to
           * close, so it is deferred through a pending flag -- the same
           * reason kill-line is. */
          else if (!strcmp(minibuffer_input, "isearch-forward")) {
            pending_search = 1;
          }
          else if (!strcmp(minibuffer_input, "isearch-backward")) {
            pending_search = 2;
          }
          else if (!strncmp(minibuffer_input, "write-file", 10)) {
            const char *path = minibuffer_input + 10;
            while (*path == ' ' || *path == '\t') path++;
            if (!*path) {
              set_status("write-file: no path given");
            } else if (save_to_file(path, buffer, length) < 0) {
              set_status("Cannot write %s", path);
            } else {
              /* The buffer now visits the file it was written to, as Emacs
               * does: saving again with C-x C-s must not go back to the old
               * name.  The mode follows the new extension. */
              snprintf(filename, 256, "%s", path);
              snprintf(fileref.name, sizeof(fileref.name), "%s", path);
              fileref.lines = countlines_buf(buffer, length);
              detect_major_mode(filename);
              set_status("Wrote %s", filename);
            }
          }
          /* Before the "dired" branch below: that one matches on a prefix,
           * so it would take "dired-jump" and try to open "-jump". */
          else if (!strcmp(minibuffer_input, "dired-jump")) {
            char dir[PATH_MAX];
            snprintf(dir, sizeof(dir), "%s", filename);
            char *slash = strrchr(dir, '/');
            if (!slash)        strcpy(dir, ".");   /* bare name: this directory */
            else if (slash == dir) dir[1] = '\0';  /* a file in "/"             */
            else               *slash = '\0';
            dired_enter(dir);
          }
          else if (!strncmp(minibuffer_input, "goto-line", 9)) {
            go_to_line(buffer, atoi(minibuffer_input + 9));
            clamp_cursor(buffer, length);
          }
          else if (!strncmp(minibuffer_input, "autocomplete", 12)) {
            ac_command(minibuffer_input + 12, buffer, length);
          }
          else if (!strncmp(minibuffer_input, "dired", 5)) {
            const char *path = minibuffer_input + 5;
            while (*path == ' ') path++;
            if (*path == '\0') path = ".";
            dired_enter(path);
          }
          else if (strstr(minibuffer_input, "find-file")) {
            open_in_new_buf(minibuffer_input, &buffer, filename);
          }
          else if (search_forward_active || search_backward_active) {
            strncpy(search_term, minibuffer_input, MINIBUFFER_MAX - 1);
            search_term[MINIBUFFER_MAX - 1] = '\0';
            if (search_match_pos < 0 && search_term[0]) {
              int m = search_backward_active
                ? find_prev_match(buffer, length, search_term, length)
                : find_next_match(buffer, length, search_term, 0);
              search_match_pos = m;
              search_match_len = (m >= 0) ? strlen(search_term) : 0;
              if (m >= 0) { set_cursor_from_index(buffer, (size_t)m); clamp_cursor(buffer, length); }
            }
            if (search_match_pos < 0)
              set_status("No match for '%s'", search_term);
            search_forward_active  = 0;
            search_backward_active = 0;
          }
          else {
            set_status("Unknown command: %s", minibuffer_input);
          }

          minibuffer_active = 0;
          minibuffer_cursor = 0;

          /* execute pending actions in main context */
          if (pending_kill_line) {
            pending_kill_line = 0;
            modified = 1;
            kill_to_end_of_line(buffer, &length);
            fileref.lines = countlines_buf(buffer, length);
          }
          if (pending_delete_char) {
            pending_delete_char = 0;
            undo_push(buffer, length);
            delete_char(buffer, &length);
            clamp_cursor(buffer, length);
          }
          /* M-x isearch-forward / isearch-backward: re-open the minibuffer
           * in search mode, exactly as C-s and C-r do from a cold start. */
          if (pending_search) {
            saved_cursor_x = cursor_x; saved_cursor_y = cursor_y;
            search_match_pos = -1; search_match_len = 0;
            minibuffer_active      = 1;
            search_forward_active  = (pending_search == 1);
            search_backward_active = (pending_search == 2);
            memset(minibuffer_input, 0, MINIBUFFER_MAX);
            minibuffer_length = 0; minibuffer_cursor = 0; status_msg = NULL;
            pending_search = 0;
          }

        } else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
          /* UX-6: cursor-aware backspace */
          if (minibuffer_cursor > 0) {
            memmove(minibuffer_input + minibuffer_cursor - 1,
                    minibuffer_input + minibuffer_cursor,
                    minibuffer_length - minibuffer_cursor + 1);
            minibuffer_length--;
            minibuffer_cursor--;
            minibuffer_input[minibuffer_length] = '\0';
          }
          if (search_forward_active || search_backward_active) {
            if (minibuffer_length > 0) {
              int m = search_backward_active
                ? find_prev_match(buffer, length, minibuffer_input, length)
                : find_next_match(buffer, length, minibuffer_input, 0);
              search_match_pos = m;
              search_match_len = (m >= 0) ? strlen(minibuffer_input) : 0;
              if (m >= 0) { set_cursor_from_index(buffer, (size_t)m); clamp_cursor(buffer, length); }
            } else {
              search_match_pos = -1;
              search_match_len = 0;
            }
          }
        } else if (ev.ch && minibuffer_length < MINIBUFFER_MAX - 1) {
          /* UX-6: cursor-aware insert */
          memmove(minibuffer_input + minibuffer_cursor + 1,
                  minibuffer_input + minibuffer_cursor,
                  minibuffer_length - minibuffer_cursor + 1);
          minibuffer_input[minibuffer_cursor] = (char)ev.ch;
          minibuffer_length++;
          minibuffer_cursor++;
          if (search_forward_active || search_backward_active) {
            int m = search_backward_active
              ? find_prev_match(buffer, length, minibuffer_input, length)
              : find_next_match(buffer, length, minibuffer_input, 0);
            search_match_pos = m;
            search_match_len = (m >= 0) ? strlen(minibuffer_input) : 0;
            if (m >= 0) { set_cursor_from_index(buffer, (size_t)m); clamp_cursor(buffer, length); }
          }
        }
      }
      if (need_force_redraw) {
        need_force_redraw = 0;
        force_redraw(buffer, length, status_msg);
      } else {
        redraw_screen(buffer, length, status_msg);
      }
      continue;
    }
    /* -- end minibuffer --------------------------------------------------- */

    /* -- dired browser ---------------------------------------------------- */
    if (dired_active) {
      if (!dired_handle_key(&ev, &buffer, filename))
        redraw_screen(buffer, length, status_msg);
      continue;
    }

    /* -- mouse events -- disabled: mouse mode breaks terminal copy/paste -----
    if (ev.type == TB_EVENT_MOUSE) {
      if (ev.key == TB_KEY_MOUSE_LEFT) {
        cursor_x = (int)ev.x + scroll_x;
        cursor_y = (int)ev.y + scroll_offset;
        clamp_cursor(buffer, length);
      } else if (ev.key == TB_KEY_MOUSE_WHEEL_UP) {
        scroll_offset -= 3;
        if (scroll_offset < 0) scroll_offset = 0;
        if (cursor_y > scroll_offset + tb_height() - 3)
          cursor_y = scroll_offset + tb_height() - 3;
        clamp_cursor(buffer, length);
      } else if (ev.key == TB_KEY_MOUSE_WHEEL_DOWN) {
        int max_lines = countlines_buf(buffer, length);
        scroll_offset += 3;
        if (scroll_offset >= max_lines) scroll_offset = max_lines - 1;
        if (cursor_y < scroll_offset) cursor_y = scroll_offset;
        clamp_cursor(buffer, length);
      }
      update_paren_match(buffer, length);
      redraw_screen(buffer, length, status_msg);
      continue;
    } */

    if (ev.type != TB_EVENT_KEY) continue;

    /* UX-4: decay status message TTL; only clear when it expires */
    if (status_ttl > 0) { if (--status_ttl == 0) status_msg = NULL; }
    else status_msg = NULL;

    /* UX-2: quit confirmation prompt ------------------------------------ */
    if (confirm_quit) {
      if (ev.ch == 'y' || ev.ch == 'Y') {
        quit_no_save(buffer);
      } else {
        confirm_quit = 0;
        status_msg = NULL;
      }
      redraw_screen(buffer, length, status_msg);
      continue;
    }

    /* -- C-g: keyboard-quit — cancel any pending operation ---------------- */
    if (ev.key == 0x07) {
      awaiting_ctrl_x        = 0;
      awaiting_ctrl_c        = 0;
      replace_state          = 0;  replace_match_pos     = -1;
      search_match_pos       = -1; search_match_len      = 0;
      search_forward_active  = 0;
      search_backward_active = 0;
      mark_active            = 0;
      confirm_quit           = 0;
      set_status("Quit");
      redraw_screen(buffer, length, status_msg);
      continue;
    }

    /* -- replace confirmation (state 3) ----------------------------------- */
    if (replace_state == 3) {
      size_t slen = strlen(replace_search);
      size_t rlen = strlen(replace_with);
      int advance = 0;

      if (ev.ch == 'y') {
        undo_push(buffer, length);
        if (length - slen + rlen < BUFFER_SIZE) {
          memmove(buffer + replace_match_pos + rlen,
                  buffer + replace_match_pos + slen,
                  length - (size_t)replace_match_pos - slen);
          memcpy(buffer + replace_match_pos, replace_with, rlen);
          length = length - slen + rlen;
          buffer[length] = '\0';
          fileref.lines = countlines_buf(buffer, length);
        }
        replace_match_pos = find_next_match(buffer, length, replace_search,
                                            (size_t)replace_match_pos + rlen);
        advance = 1;
      } else if (ev.ch == 'n') {
        replace_match_pos = find_next_match(buffer, length, replace_search,
                                            (size_t)replace_match_pos + 1);
        advance = 1;
      } else if (ev.ch == '!') {
        undo_push(buffer, length);
        int count = 0;
        size_t pos = (size_t)replace_match_pos;
        while (1) {
          int m = find_next_match(buffer, length, replace_search, pos);
          if (m < 0) break;
          if (length - slen + rlen >= BUFFER_SIZE) break;
          memmove(buffer + m + rlen, buffer + m + slen, length - (size_t)m - slen);
          memcpy(buffer + m, replace_with, rlen);
          length = length - slen + rlen;
          buffer[length] = '\0';
          pos = (size_t)m + rlen;
          count++;
        }
        fileref.lines = countlines_buf(buffer, length);
        replace_state = 0; replace_match_pos = -1;
        search_match_pos = -1; search_match_len = 0;
        set_status("Replaced %d occurrence%s", count, count == 1 ? "" : "s");
      } else if (ev.ch == 'q') {
        replace_state = 0; replace_match_pos = -1;
        search_match_pos = -1; search_match_len = 0;
      }

      if (advance) {
        if (replace_match_pos >= 0) {
          search_match_pos = replace_match_pos;
          search_match_len = slen;
          set_cursor_from_index(buffer, (size_t)replace_match_pos);
          clamp_cursor(buffer, length);
          set_status("Replace '%s' -> '%s'? (y)es (n)o (!)all (q)uit",
                     replace_search, replace_with);
        } else {
          replace_state = 0;
          search_match_pos = -1; search_match_len = 0;
          set_status("No more matches");
        }
      }

      update_paren_match(buffer, length);
      redraw_screen(buffer, length, status_msg);
      continue;
    }

    /* -- C-x prefix ------------------------------------------------------- */
    if (awaiting_ctrl_x) {
      awaiting_ctrl_x = 0;

      if (ev.key == TB_KEY_CTRL_C) {
#ifdef CONFIRM_QUIT
        if (modified) {
          confirm_quit = 1;
          set_status("Buffer modified. Quit without saving? (y/n)");
        } else {
          quit_no_save(buffer);
        }
#else
        quit_no_save(buffer);
#endif
      }
      else if (ev.key == TB_KEY_CTRL_S) {
        if (save_to_file(filename, buffer, length) == 0)
          set_status("Saved %s", filename);
        else
          set_status("Cannot write %s", filename);
      }
      else if (ev.ch == 'u') {
        /* C-x u: undo */
        if (undo_pop(buffer, &length)) {
          fileref.lines = countlines_buf(buffer, length);
          clamp_cursor(buffer, length);
          set_status("Undo!");
        } else {
          set_status("No more undo history");
        }
      }
      else if (ev.ch == '%') {
        /* C-x %: search and replace */
        saved_cursor_x = cursor_x;
        saved_cursor_y = cursor_y;
        replace_state = 1;
        minibuffer_active = 1;
        memset(minibuffer_input, 0, MINIBUFFER_MAX);
        minibuffer_length = 0;
        minibuffer_cursor = 0;
        status_msg = NULL;
      }
      else if (ev.key == TB_KEY_CTRL_F) {
        /* FIX: proper reset before pre-fill, no getcwd leak, no malloc */
	saved_cursor_x = cursor_x;
	saved_cursor_y = cursor_y;
        minibuffer_active = 1;
        memset(minibuffer_input, 0, MINIBUFFER_MAX);
        strncpy(minibuffer_input, "find-file ", MINIBUFFER_MAX - 1);
        minibuffer_length = (int)strlen(minibuffer_input);
        minibuffer_cursor = minibuffer_length;
        status_msg = NULL;
      }
      else if (ev.ch == 'd') {
        minibuffer_active = 1;
        memset(minibuffer_input, 0, MINIBUFFER_MAX);
        char cwd[MINIBUFFER_MAX];
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
        snprintf(minibuffer_input, MINIBUFFER_MAX, "dired %s", cwd);
        minibuffer_length = (int)strlen(minibuffer_input);
        minibuffer_cursor = minibuffer_length;
        status_msg = NULL;
      }
      else if (ev.key == TB_KEY_ARROW_RIGHT) {
        /* C-x →: next buffer */
        if (num_bufs > 1) {
          buf_save_to_slot(current_buf);
          int next = (current_buf + 1) % num_bufs;
          buf_load_from_slot(next, &buffer, filename);
          update_paren_match(buffer, length);
          set_status("%s  [%d/%d]", filename, current_buf + 1, num_bufs);
        }
      }
      else if (ev.key == TB_KEY_ARROW_LEFT) {
        /* C-x ←: previous buffer */
        if (num_bufs > 1) {
          buf_save_to_slot(current_buf);
          int prev = (current_buf + num_bufs - 1) % num_bufs;
          buf_load_from_slot(prev, &buffer, filename);
          update_paren_match(buffer, length);
          set_status("%s  [%d/%d]", filename, current_buf + 1, num_bufs);
        }
      }
      /* BUG-5 fix: horizontal scroll moved inside the C-x block */
      else if (ev.ch == '<') {
        if (soft_wrap) set_status("Lines are wrapped; nothing to scroll");
        else { scroll_x -= 4; if (scroll_x < 0) scroll_x = 0; }
      }
      else if (ev.ch == '>') {
        if (soft_wrap) set_status("Lines are wrapped; nothing to scroll");
        else scroll_x += 4;
      }
      else if (ev.ch == 'w') {
        soft_wrap = !soft_wrap;
        scroll_x = scroll_row = 0;
        clamp_cursor(buffer, length);
        set_status(soft_wrap ? "Soft wrap enabled" : "Soft wrap disabled");
      }
      else if (ev.ch == '2') { mux_split(1, filename); }
      else if (ev.ch == '3') { mux_split(0, filename); }
      else if (ev.ch == '1') { mux_unsplit(); }
      else if (ev.ch == 'o') { mux_other_pane(); }
    }

    /* -- C-c prefix -------------------------------------------------------- */
    else if (awaiting_ctrl_c) {
      awaiting_ctrl_c = 0;
      if (ev.key == TB_KEY_CTRL_O) {
        follow_link_at_cursor(buffer, length, &buffer, filename);
        update_paren_match(buffer, length);
        clamp_cursor(buffer, length);
      }
      else if (ev.key == TB_KEY_CTRL_T && current_mode == &org_mode) {
        size_t content;
        if (org_heading_content_pos(buffer, length, &content)) {
          (void)content;
          undo_push(buffer, length);
          org_cycle_todo(buffer, &length);
          clamp_cursor(buffer, length);
        } else set_status("Not on an Org heading");
      }
      else if (ev.key == TB_KEY_CTRL_X && current_mode == &org_mode) {
        size_t line = current_line_start(buffer, length), p = line;
        while (p + 2 < length && buffer[p] != '\n' &&
               !(buffer[p] == '[' && buffer[p+2] == ']' &&
                 (buffer[p+1] == ' ' || buffer[p+1] == 'x' || buffer[p+1] == 'X'))) p++;
        if (p + 2 < length && buffer[p] != '\n') {
          undo_push(buffer, length);
          org_toggle_checkbox(buffer, length);
        } else set_status("No checkbox on this line");
      }
    }

    /* -- M-x -------------------------------------------------------------- */
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'x') {
      saved_cursor_x = cursor_x;
      saved_cursor_y = cursor_y;
      minibuffer_active = 1;
      memset(minibuffer_input, 0, MINIBUFFER_MAX);
      minibuffer_length = 0;
      minibuffer_cursor = 0;
      status_msg = NULL;
      redraw_screen(buffer, length, status_msg);
      continue;
    }

    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'q') {
      mark_active = 0;
      undo_push(buffer, length);
      fill_paragraph(buffer, &length);
    }

    /* -- M-f / M-b / M-d -------------------------------------------------- */
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'f') {
      move_forward_word(buffer, length);
      clamp_cursor(buffer, length);
    }
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'b') {
      move_backward_word(buffer, length);
      clamp_cursor(buffer, length);
    }
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'd') {
      mark_active = 0;
      undo_push(buffer, length);
      delete_forward_word(buffer, &length);
      clamp_cursor(buffer, length);
    }
    else if ((ev.mod & TB_MOD_ALT) && (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2)) {
      mark_active = 0;
      undo_push(buffer, length);
      hungry_delete_backward(buffer, &length);
      clamp_cursor(buffer, length);
    }

    /* -- C-y yank --------------------------------------------------------- */
    else if (ev.key == TB_KEY_CTRL_Y) {
      mark_active = 0;
      undo_push(buffer, length);
      yank(buffer, buffer_size, &length);
      clamp_cursor(buffer, length);
    }

    /* -- C-w: cut region -------------------------------------------------- */
    else if (ev.key == TB_KEY_CTRL_W) {
      if (mark_active) {
        undo_push(buffer, length);
        kill_region(buffer, &length);
        fileref.lines = countlines_buf(buffer, length);
        clamp_cursor(buffer, length);
        set_status("Cut");
      }
    }

    /* -- M-w: copy region ------------------------------------------------- */
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'w') {
      if (mark_active) {
        copy_region(buffer, length);
        set_status("Copied");
      }
    }

    /* -- M-< / M-> -------------------------------------------------------- */
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == '<') {
      go_to_beginning_of_buffer(buffer);
      clamp_cursor(buffer, length);
    }
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == '>') {
      go_to_end_of_buffer(buffer);
      clamp_cursor(buffer, length);
    }

    /* -- C-x (set prefix) ------------------------------------------------- */
    else if (ev.key == TB_KEY_CTRL_X) {
      awaiting_ctrl_x = 1;
      set_status("C-x");
    }

    /* -- C-c (set prefix) --------------------------------------------------
       Only used standalone here (C-x C-c, the quit chord, is handled inside
       the awaiting_ctrl_x block above and never reaches this branch). */
    else if (ev.key == TB_KEY_CTRL_C) {
      awaiting_ctrl_c = 1;
      set_status("C-c");
    }

    /* -- F10: toggle menu bar ---------------------------------------------- */
    else if (ev.key == TB_KEY_F10) {
#ifdef MENU_BAR_MODE
      menu_toggle();
      redraw_screen(buffer, length, status_msg);
#else
      set_status("There is no support built for menu-bar-mode.");
#endif
    }

    /* -- C-o: cycle to next buffer ---------------------------------------- */
    else if (ev.key == TB_KEY_CTRL_O) {
      if (num_bufs > 1) {
        buf_save_to_slot(current_buf);
        int next = (current_buf + 1) % num_bufs;
        buf_load_from_slot(next, &buffer, filename);
        update_paren_match(buffer, length);
        set_status("%s  [%d/%d]", filename, current_buf + 1, num_bufs);
      }
    }

    /* -- C-s forward search / C-r backward search ------------------------- */
    else if (ev.key == TB_KEY_CTRL_S) {
      if (search_term[0] && search_match_pos >= 0) {
        size_t from = (size_t)search_match_pos + search_match_len;
        int m = find_next_match(buffer, length, search_term, from);
        if (m < 0) m = find_next_match(buffer, length, search_term, 0);
        if (m >= 0) {
          search_match_pos = m; search_match_len = strlen(search_term);
          set_cursor_from_index(buffer, (size_t)m); clamp_cursor(buffer, length);
        } else { set_status("No match for '%s'", search_term); }
      } else {
        saved_cursor_x = cursor_x; saved_cursor_y = cursor_y;
        search_match_pos = -1; search_match_len = 0;
        minibuffer_active = 1; search_forward_active = 1;
        memset(minibuffer_input, 0, MINIBUFFER_MAX);
        minibuffer_length = 0; minibuffer_cursor = 0; status_msg = NULL;
      }
    }

    else if (ev.key == TB_KEY_CTRL_R) {
      if (search_term[0] && search_match_pos >= 0) {
        int m = find_prev_match(buffer, length, search_term, (size_t)search_match_pos);
        if (m < 0) m = find_prev_match(buffer, length, search_term, length);
        if (m >= 0) {
          search_match_pos = m; search_match_len = strlen(search_term);
          set_cursor_from_index(buffer, (size_t)m); clamp_cursor(buffer, length);
        } else { set_status("No match for '%s'", search_term); }
      } else {
        saved_cursor_x = cursor_x; saved_cursor_y = cursor_y;
        search_match_pos = -1; search_match_len = 0;
        minibuffer_active = 1; search_backward_active = 1;
        memset(minibuffer_input, 0, MINIBUFFER_MAX);
        minibuffer_length = 0; minibuffer_cursor = 0; status_msg = NULL;
      }
    }

    else if (ev.key == TB_KEY_CTRL_L) {
      /* recenters screen and hard-resets terminal display */
      clamp_cursor(buffer, length);
      scroll_offset = cursor_y - (tb_height() - 2) / 2;
      if (scroll_offset < 0) scroll_offset = 0;
      scroll_row    = 0;
      update_paren_match(buffer, length);
      force_redraw(buffer, length, status_msg);
      continue;
    }

    /* -- C-SPC: set mark -------------------------------------------------- */
    else if (ev.key == TB_KEY_CTRL_2 && ev.ch == 0) {  /* Ctrl+Space: key=0 ch=0 */
      mark_pos    = get_buffer_index_from_cursor(buffer, length);
      mark_active = 1;
      set_status("Mark set");
    }
    /* -- C-k kill line ---------------------------------------------------- */
    else if (ev.key == TB_KEY_CTRL_K) {
      mark_active = 0;
      undo_push(buffer, length);
      kill_to_end_of_line(buffer, &length);
      fileref.lines = countlines_buf(buffer, length);
    }

    /* -- C-a / Home ------------------------------------------------------- */
    else if (ev.key == TB_KEY_HOME || ev.key == TB_KEY_CTRL_A) {
      go_to_bol(buffer, length);
    }

    /* -- C-e / End -------------------------------------------------------- */
    else if (ev.key == TB_KEY_END || ev.key == TB_KEY_CTRL_E) {
      go_to_eol(buffer, length);
    }

    /* -- Tab -------------------------------------------------------------- */
    else if (ev.key == TB_KEY_TAB) {
      mark_active = 0;
      /* ac_complete() pushes its own undo entry only when it inserts, so a
       * TAB that completes nothing still indents and undoes normally. */
      if (!ac_complete(buffer, buffer_size, &length)) {
        undo_push(buffer, length);
        insert_tab(buffer, buffer_size, &length);
      }
      clamp_cursor(buffer, length);
    }

    /* -- Page up/down ----------------------------------------------------- */
    else if (ev.key == TB_KEY_PGUP) {
      if (soft_wrap) {
        /* a screenful of rows, which may be less than a screenful of lines */
        wrap_scroll_backward(buffer, length, tb_height() - 2, tb_width());
        cursor_y = scroll_offset;
        cursor_x = wrap_row_start_col(buffer, length, cursor_y, scroll_row,
                                      tb_width());
      } else {
        scroll_offset -= (tb_height() - 2);
        if (scroll_offset < 0) scroll_offset = 0;
        cursor_y = scroll_offset;
      }
      clamp_cursor(buffer, length);
    }
    else if (ev.key == TB_KEY_PGDN) {
      if (soft_wrap) {
        wrap_scroll_forward(buffer, length, tb_height() - 2, tb_width());
        cursor_y = scroll_offset;
        cursor_x = wrap_row_start_col(buffer, length, cursor_y, scroll_row,
                                      tb_width());
      } else {
        scroll_offset += (tb_height() - 2);
        cursor_y = scroll_offset;
      }
      clamp_cursor(buffer, length);
    }

    /* -- Arrow left ------------------------------------------------------- */
    else if (ev.key == TB_KEY_ARROW_LEFT) {
      if (cursor_x > 0) {
        cursor_x--;
      } else if (cursor_y > 0) {
        /* BUG-3 fix: land at end of previous line, no extra decrement */
        cursor_y--;
        cursor_x = get_line_length(buffer, length, cursor_y);
      }
      clamp_cursor(buffer, length);
    }

    /* -- Arrow right ------------------------------------------------------ */
    else if (ev.key == TB_KEY_ARROW_RIGHT) {
      size_t cur = get_buffer_index_from_cursor(buffer, length);
      if (cur < length) {
        int line_len = get_line_length(buffer, length, cursor_y);
        if (cursor_x < line_len) cursor_x++;
        else { cursor_y++; cursor_x = 0; }
      }
      clamp_cursor(buffer, length);
    }

    /* -- Arrow up --------------------------------------------------------- */
    else if (ev.key == TB_KEY_ARROW_UP) {
      if (cursor_y > 0) cursor_y--;
      clamp_cursor(buffer, length);
    }

    /* -- Arrow down ------------------------------------------------------- */
    else if (ev.key == TB_KEY_ARROW_DOWN) {
      /* BUG-7 fix: compare against live line count, no O(n) scan */
      if (cursor_y + 1 < countlines_buf(buffer, length)) {
        cursor_y++;
        clamp_cursor(buffer, length);
      }
    }

    /* -- C-h: select all -------------------------------------------------- */
    /* TB_KEY_CTRL_H == TB_KEY_BACKSPACE (0x08); modern terminals send 0x7f  */
    /* for the backspace key, so this only fires on an explicit Ctrl+H.      */
    else if (ev.key == TB_KEY_CTRL_H) {
      mark_pos    = 0;
      mark_active = 1;
      go_to_end_of_buffer(buffer);
      go_to_eol(buffer, length);
      clamp_cursor(buffer, length);
      set_status("Mark set (select all)");
    }

    /* -- Backspace -------------------------------------------------------- */
    else if (ev.key == TB_KEY_BACKSPACE2 && length > 0) {
      undo_push(buffer, length);
      if (mark_active) {
        kill_region(buffer, &length);
        fileref.lines = countlines_buf(buffer, length);
        clamp_cursor(buffer, length);
      } else {
        if (cursor_x > 0) {
          cursor_x--;
          size_t del = get_buffer_index_from_cursor(buffer, length);
          if (del < length) {
            memmove(buffer + del, buffer + del + 1, length - del);
            length--;
            buffer[length] = '\0';
          }
        } else if (cursor_y > 0) {
          backspace_join_line(buffer, &length);
        }
        fileref.lines = countlines_buf(buffer, length);
      }
    }

    /* -- Delete / C-d: delete character to the right ----------------------- */
    else if ((ev.key == TB_KEY_DELETE || ev.key == TB_KEY_CTRL_D) && length > 0) {
      mark_active = 0;
      undo_push(buffer, length);
      delete_char(buffer, &length);
      clamp_cursor(buffer, length);
    }

    /* -- character insert / Enter ----------------------------------------- */
    else if ((ev.ch || ev.key == TB_KEY_ENTER) && length < buffer_size - 1) {
      mark_active = 0;
      undo_push(buffer, length);
      clamp_cursor(buffer, length);
      size_t insert_pos = get_buffer_index_from_cursor(buffer, length);
      char   ch         = (ev.key == TB_KEY_ENTER) ? '\n' : (char)ev.ch;

      memmove(buffer + insert_pos + 1, buffer + insert_pos,
              length - insert_pos);
      buffer[insert_pos] = ch;
      length++;
      buffer[length] = '\0';

      if (ch == '\n') {
        /* auto-indent: count leading whitespace on the line we just left */
        size_t sol = insert_pos;
        while (sol > 0 && buffer[sol - 1] != '\n') sol--;
        size_t indent = 0;
        while (sol + indent < insert_pos &&
               (buffer[sol + indent] == ' ' || buffer[sol + indent] == '\t'))
          indent++;
        if (indent > 0 && length + indent < BUFFER_SIZE) {
          memmove(buffer + insert_pos + 1 + indent,
                  buffer + insert_pos + 1,
                  length - (insert_pos + 1));
          memcpy(buffer + insert_pos + 1, buffer + sol, indent);
          length += indent;
          buffer[length] = '\0';
          cursor_x = (int)indent;
        } else {
          cursor_x = 0;
        }
        cursor_y++;
        fileref.lines++;
        clamp_cursor(buffer, length);
      } else {
        /* FIX: no screen-wrap bump - logical lines only */
        cursor_x++;
        clamp_cursor(buffer, length);
      }
    }
    update_paren_match(buffer, length);
    redraw_screen(buffer, length, status_msg);
  }

  tb_shutdown();
  free(buffer);
  return 0;
}
