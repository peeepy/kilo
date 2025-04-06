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

/*** defines ***/

// Macro to generate Ctrl+key combinations (masks bits)
#define CTRL_KEY(k) ((k) & 0x1f)

// Editor version constant
#define KILO_VERSION "0.0.1"
// Length of a tab stop constant
#define KILO_TAB_STOP 2
// How many times user is required to press CTRL-Q to quit if buffer is dirty
#define KILO_QUIT_TIMES 1

#define KILO_LINE_NUMBER_WIDTH 6

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
  PAGE_DOWN
};

enum editorHighlight {
  HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_NUMBER,
	HL_STRING,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/
struct editorSyntax {
	char *filetype;
	char **filematch;
	char **keywords;
	char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
	int flags;
};

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
  char statusmsg[80];
  time_t statusmsg_time;
	struct editorSyntax *syntax; // Pointer to the syntax highlighting struct
  struct termios orig_termios; // Original terminal settings to restore on exit
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
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

// --- Input ---
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorProcessKeypress();

// --- Init ---
void initEditor(); // Needed by main() in kilo.c

#endif // KILO_H_

