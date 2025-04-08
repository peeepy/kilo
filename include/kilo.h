#ifndef KILO_H_
#define KILO_H_

/*** includes ***/

#include <ctype.h>      // For iscntrl()
#include <stdio.h>      // For FILE, fopen, getline, perror, printf, snprintf, sscanf
#include <stdlib.h>     // For atexit, exit, free, malloc, realloc
#include <string.h>     // For memcpy, strlen
#include <sys/types.h>  // For ssize_t (needed for getline)
#include <termios.h>    // For struct termios, tcgetattr, tcsetattr, TCSAFLUSH, etc.
#include <unistd.h>     // For read, write, STDIN_FILENO, STDOUT_FILENO
#include <sys/ioctl.h>  // For ioctl, TIOCGWINSZ, struct winsize
#include <errno.h>      // For errno, EAGAIN
#include <dirent.h> // For directory handling
#include "debug.h"

/*** defines ***/

// Macro to generate Ctrl+key combinations (masks bits)
#define CTRL_KEY(k) ((k) & 0x1f)

#define DEBUG_TOGGLE_KEY CTRL_KEY('d')

// Editor version constant
#define KILO_VERSION "0.0.1"
// Length of a tab stop constant
#define KILO_TAB_STOP 2
// How many times user is required to press CTRL-Q to quit if buffer is dirty
#define KILO_QUIT_TIMES 1

#define KILO_LINE_NUMBER_WIDTH 6


typedef struct {
    char *name;
    // Syntax FG/BG pairs (store "r,g,b")
    char *hl_normal_fg; char *hl_normal_bg;
    char *hl_comment_fg; char *hl_comment_bg;
    char *hl_mlcomment_fg; char *hl_mlcomment_bg;
    char *hl_keyword1_fg; char *hl_keyword1_bg;
    char *hl_keyword2_fg; char *hl_keyword2_bg;
    char *hl_keyword3_fg; char *hl_keyword3_bg;
    char *hl_type_fg; char *hl_type_bg;
    char *hl_builtin_fg; char *hl_builtin_bg;
    char *hl_string_fg; char *hl_string_bg;
    char *hl_number_fg; char *hl_number_bg;
    char *hl_match_fg; char *hl_match_bg;
    // UI Elements
    char *ui_background_bg; // Just BG needed
    char *ui_lineno_fg;     char *ui_lineno_bg; //
    char *ui_status_fg;     char *ui_status_bg;
    char *ui_message_fg;    char *ui_message_bg;
    char *ui_tilde_fg;      char *ui_tilde_bg; //

    // --- New Status Bar Segment Colors ---
    // Mode Segment (e.g., NORMAL, INSERT)
    char *ui_status_mode_fg;
    char *ui_status_mode_bg;

    // File Info Segment
    char *ui_status_file_fg;
    char *ui_status_file_bg;

    // Git/Optional Info Segment (like time, day)
    char *ui_status_info_fg;
    char *ui_status_info_bg;

    // File Type / Encoding Segment
    char *ui_status_ft_fg;
    char *ui_status_ft_bg;

    // Position Segment (Line/Col/Percent)
    char *ui_status_pos_fg;
    char *ui_status_pos_bg;

    // Separator Colors (Foreground is the arrow color, Background is the base)
    // Often, the separator FG matches the BG of the section it points *from*
    // We might derive these dynamically later, but defining them allows flexibility.
    char *ui_status_sep_fg; // Potentially derived
    char *ui_status_sep_bg; // Often same as ui_status_bg

} editorTheme;

// Define symbolic names for special keys, starting from 1000
// to avoid collision with regular character byte values.
enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  H_KEY = 104, // Left
  J_KEY = 106, // down
  K_KEY = 107, // up
  L_KEY = 108, // right
  D_KEY = 101, // d for delete
  INSERT_KEY = 105, // i
  NORMAL_KEY = 27, // esc
};

enum editorMode { MODE_NORMAL, MODE_INSERT };


enum editorHighlight {
  HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_KEYWORD3,
  HL_TYPE,
  HL_BUILTIN,
  HL_NUMBER,
	HL_STRING,
	HL_MATCH,
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)


/*** data ***/
struct editorSyntax {
    char *filetype;
    char **filematch;
    // Separate keyword lists
    char **keywords1;
    char **keywords2;
    char **keywords3;
    char **types;
    char **builtins;
    // Comments and flags
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
    // Language icon to be used in status bar (UTF)
    char *status_icon;
};


typedef struct editorBuffer {
    char *filename;     // Full path to the file
    // Add other buffer-specific data you need:
    int dirty;          // Modified status
    erow *row;       // Rows specific to this buffer
    int numrows;
    int cx, cy, rx;  // Cursor position specific to this buffer
    int rowoff, coloff; // Scroll offset specific to this buffer
    struct editorSyntax *syntax; // Syntax highlighting specific to this buffer

    struct editorBuffer *next; // Pointer for linked list implementation
    struct editorBuffer *prev;
} editorBuffer;


// Structure to hold a single row of text in the editor
typedef struct erow {
	int idx;
  int size;       // Number of characters in the row
  int rsize;
  char *chars;    // Pointer to the character data for the row
  char *render;
  unsigned char *hl;
	int hl_open_comment;
} erow;

// Global structure to hold the editor's state
struct editorConfig {
  int cx, cy;             // Cursor position (cx: column, cy: row index within the file)
  int rx;
  int rowoff;             // Vertical scroll offset (which file row is at the top of the screen)
  int coloff;             // Horizontal scroll offset (which character column is at the left of the screen)
  int screenrows;         // Number of rows the terminal window can display
  int screencols;         // Number of columns the terminal window can display
  int numrows;            // Total number of rows in the file buffer
  erow *row;              // Pointer to an array of erow structures (the file content)
  int dirty;              // Whether the file has been modified externally since opening/saving
  char *filename;         // Pointer to the filename
  char *dirname;          // Pointer to the dirname
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax; // Pointer to the syntax highlighting struct
  struct editorSyntax *syntax_defs; // Dynamic array of syntax definitions
  int num_syntax_defs;             // Number of loaded definitions
  struct termios orig_termios; // Original terminal settings to restore on exit
  editorTheme theme; // Holds current theme colors as strings
  enum editorMode mode; // Holds current editor mode (INSERT or NORMAL)
    // --- New fields for multi-buffer support ---
  editorBuffer *buffer_list_head; // Head of the linked list of all open buffers
  editorBuffer *current_buffer;  // Pointer to the currently active buffer
  int num_buffers;              // Count of open buffers
};

extern struct editorConfig E;


struct abuf {
    char *b;    // Pointer to the buffer memory
    int len;    // Current length of the string in the buffer
};


/*** prototypes ***/
// --- Terminal ---
void die(const char* s);
void disableRawMode();
void enableRawMode();
int editorReadKey();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

// --- Syntax Highlighting ---
void editorUpdateSyntax(erow *row);
int editorSyntaxToColour(int hl);
void editorSelectSyntaxHighlight();
int is_separator(int c); // Might be static if only used in syntax.c
void loadSyntaxFiles(void);
void freeSyntaxDefs(void);


// --- Row Operations ---
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorUpdateRow(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int at);

// --- Editor Operations ---
void editorInsertChar(int c);
void editorInsertNewline();
void editorDelChar();

// --- File I/O ---
char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
void editorSave();
char *getEditingDirname(const char *filename);
char *findBasename(const char *path);

// --- Find ---
void editorFind();
void editorFindCallback(char *query, int key);

// --- Output ---
void editorScroll();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);
void editorClearStatusMessage();
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

// --- Themes -- 
void loadTheme(const char *theme_name);
void freeThemeColors(void);
void applyThemeDefaultColor(struct abuf *ab);
void applyTrueColor(struct abuf *ab, const char *fg_rgb_str, const char *bg_rgb_str);

// --- Input ---
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorProcessKeypress();

// --- Init ---
void initEditor();

// Debug
void editorDrawDebugOverlay(struct abuf *ab);

#endif // KILO_H_

