/*** includes ***/

/* Feature test macros - define these before including system headers
 * to request specific APIs or behaviors.
 * _DEFAULT_SOURCE: Newer way to get BSD + SVID + POSIX features (including things often covered by _BSD_SOURCE & _GNU_SOURCE)
 * _BSD_SOURCE: Request features originally from BSD systems.
 * _GNU_SOURCE: Request GNU extensions beyond POSIX.
 */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>      // For iscntrl()
#include <errno.h>      // For errno, EAGAIN
#include <stdio.h>      // For FILE, fopen, getline, perror, printf, snprintf, sscanf
#include <stdlib.h>     // For atexit, exit, free, malloc, realloc
#include <string.h>     // For memcpy, strlen
#include <sys/ioctl.h>  // For ioctl, TIOCGWINSZ, struct winsize
#include <sys/types.h>  // For ssize_t (needed for getline)
#include <termios.h>    // For struct termios, tcgetattr, tcsetattr, TCSAFLUSH, etc.
#include <unistd.h>     // For read, write, STDIN_FILENO, STDOUT_FILENO
#include <time.h> 
#include <stdarg.h>
#include <fcntl.h>

/*** defines ***/

// Macro to generate Ctrl+key combinations (masks bits)
#define CTRL_KEY(k) ((k) & 0x1f)

// Editor version constant
#define KILO_VERSION "0.0.1"
// Length of a tab stop constant
#define KILO_TAB_STOP 2
// How many times user is required to press CTRL-Q to quit if buffer is dirty
#define KILO_QUIT_TIMES 1

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

struct editorConfig E; // Global editor state instance

/*** filetypes ***/
// config for C syntax highlighting
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
	{
		"c",
		C_HL_extensions,
		C_HL_keywords,
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
};

// HLDB = highlight database
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));


/*** terminal ***/

/*
 * Clears the screen, prints an error message based on errno,
 * and terminates the program.
 */
void die(const char* s) {
  // Clear the screen (\x1b[2J) and reposition cursor to top-left (\x1b[H)
  // using ANSI escape sequences.
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  // Print the error message associated with the last system call error (errno)
  perror(s);
  exit(1);
}

/*
 * Restores the original terminal attributes saved in E.orig_termios.
 * Called automatically on exit using atexit().
 */
void disableRawMode() {
    // TCSAFLUSH: Apply changes after all pending output has been written,
    // and discard any pending input.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr"); // Use die() for error handling
}

/*
 * Enables raw mode for the terminal.
 * Raw mode allows reading input byte-by-byte, without buffering,
 * and disables default terminal processing like echoing input.
 */
void enableRawMode() {
    // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    // Register disableRawMode to be called automatically on program exit
    atexit(disableRawMode);

    struct termios raw = E.orig_termios; // Make a copy to modify

    /* Modify terminal flags:
     * Input flags (c_iflag):
     * BRKINT: Don't send SIGINT on break condition.
     * ICRNL: Don't translate carriage return (\r) to newline (\n).
     * INPCK: Disable input parity checking.
     * ISTRIP: Don't strip the 8th bit from input characters.
     * IXON: Disable software flow control (Ctrl+S/Ctrl+Q).
     * Output flags (c_oflag):
     * OPOST: Disable all output processing (like converting \n to \r\n).
     * Control flags (c_cflag):
     * CS8: Set character size to 8 bits per byte (common).
     * Local flags (c_lflag):
     * ECHO: Don't echo input characters back to the terminal.
     * ICANON: Disable canonical mode (read input byte-by-byte, not line-by-line).
     * IEXTEN: Disable implementation-defined input processing (e.g. Ctrl+V).
     * ISIG: Disable signal generation (e.g. Ctrl+C sends SIGINT, Ctrl+Z sends SIGTSTP).
     * Control characters (c_cc):
     * VMIN: Minimum number of bytes to read before read() returns (0 = non-blocking).
     * VTIME: Maximum time to wait for input in deciseconds (1 = 100ms timeout).
     */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout

    // Apply the modified terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*
 * Waits for and reads a single keypress from standard input.
 * Handles multi-byte escape sequences for special keys (arrows, Home, End, etc.).
 * Returns the character read or a special key code (from editorKey enum).
 */
int editorReadKey() {
    int nread;
    char c;
    // Keep reading until a byte is received or an error occurs (excluding timeout)
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // EAGAIN typically means the read timed out (VMIN=0, VTIME>0), which is expected.
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    // Check if the character is an escape character (start of escape sequence)
    if (c == '\x1b') {
        char seq[3]; // Buffer to read the rest of the sequence

        // Try reading the next two bytes of the sequence with timeouts
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b'; // Timeout or error, return ESC
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b'; // Timeout or error, return ESC

        // Check for common escape sequence patterns (CSI - Control Sequence Introducer)
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Sequences like \x1b[1~ (Home), \x1b[3~ (Delete), etc.
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; // Need the trailing '~'
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY; // Sometimes Home is 7~
                        case '8': return END_KEY;  // Sometimes End is 8~
                    }
                }
            } else {
                // Sequences like \x1b[A (Up), \x1b[B (Down), etc.
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY; // Sometimes Home is [H
                    case 'F': return END_KEY;  // Sometimes End is [F
                }
            }
        } else if (seq[0] == 'O') {
             // Less common sequences like \x1bOH (Home), \x1bOF (End) used by some terminals
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b'; // Unrecognized escape sequence, return ESC itself
    } else {
        return c; // Normal character keypress
    }
}

/*
 * Tries to get the current cursor position using ANSI escape sequences.
 * Sends "\x1b[6n" (Device Status Report - Cursor Position) and parses the response "\x1b[<row>;<col>R".
 * Returns 0 on success, -1 on failure.
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // Request cursor position report
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // Read the response from stdin, looking for the terminating 'R'
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break; // Error or timeout reading
        if (buf[i] == 'R') break; // Found end of response
        i++;
    }
    buf[i] = '\0'; // Null-terminate the buffer

    // Check if the response starts with the expected escape sequence
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // Parse the row and column numbers from the response string
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0; // Success
}

/*
 * Tries to get the terminal window size.
 * First attempts using ioctl(TIOCGWINSZ). If that fails, falls back
 * to moving the cursor far down-right and querying its position.
 * Returns 0 on success, -1 on failure.
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // Try the ioctl system call first (preferred method)
    // TIOCGWINSZ = Terminal IO Control Get Window SiZe
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Fallback: Move cursor far right (\x1b[999C) and far down (\x1b[999B),
        // then query its position. Terminals usually cap the position at the edge.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols); // Use the fallback position query
    } else {
        // Success using ioctl
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


/*** syntax highlighting ***/
int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
	row->hl = realloc(row->hl, row->rsize);
	// Sets all characters to HL_NORMAL by default
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL) return;

	char **keywords = E.syntax->keywords;

	char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
     if (!strncmp(&row->render[i], scs, scs_len)) {
       memset(&row->hl[i], HL_COMMENT, row->rsize - i);
       break;
     }
   }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }


    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}

        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

		if (E.syntax-> flags & HL_HIGHLIGHT_NUMBERS) {
			// If  character is a number, colour it accordingly
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || 
					(c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}

    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }
		
		prev_sep = is_separator(c);
		i++;
	}

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);

}

int editorSyntaxToColour(int hl) {
	switch (hl) {
		case HL_COMMENT:
		case HL_MLCOMMENT: return 31;
		case HL_KEYWORD1: return 35;
		case HL_KEYWORD2: return 32;
		case HL_STRING: return 35;
		case HL_NUMBER: return 36;
		case HL_MATCH: return 34;
		default: return 37;
	}
}

void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  char *ext = strrchr(E.filename, '.');

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;

				int filerow;
				for (filerow = 0; filerow < E.numrows; filerow++) {
					editorUpdateSyntax(&E.row[filerow]);
				}

        return;
      }
      i++;
    }
  }
}


/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}


/*
 *  Uses the `chars` string of an erow to fill in the contents of the `render` string.
 * Copies each character from `chars` to `render`.
 */

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;

  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);

  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);
  int idx = 0;

  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }

  row->render[idx] = '\0';
  row->rsize = idx;
	
	editorUpdateSyntax(row);
}

/*
 * Appends a new row of text to the editor's buffer (E.row).
 * Takes ownership of the string data by copying it.
 */
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
	for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

  E.row[at].size = len; // Store the length of the string
  // Allocate memory for the row's character data (+1 for null terminator)
  E.row[at].chars = malloc(len + 1);
  if (E.row[at].chars == NULL) die("malloc"); // Handle allocation failure

  memcpy(E.row[at].chars, s, len); // Copy the string content
  E.row[at].chars[len] = '\0'; // Null-terminate the copied string

  E.row[at].rsize = 0; // Size of the contents of E.row.render
  E.row[at].render = NULL; // Characters to draw on screen for a row of text
  E.row[at].hl = NULL;
	E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++; // Increment the total number of rows
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}


/*** editor operations ***/
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }

  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}



/*** file i/o ***/
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}


/*
 * Opens the specified file, reads its content line by line,
 * and appends each line to the editor buffer using editorAppendRow.
 */
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename); // Makes copy of the filename string & allocates rwquired memory

	editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r"); // Open file for reading
  if (!fp) die("fopen"); // Exit if file cannot be opened

  char *line = NULL; // Buffer for getline()
  size_t linecap = 0; // Capacity of the buffer for getline()
  ssize_t linelen; // Length of the line read by getline()

  // Read lines until EOF (-1 is returned)
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
      // Strip trailing newline (\n) or carriage return (\r) characters
      while (linelen > 0 && (line[linelen - 1] == '\n' ||
                              line[linelen - 1] == '\r'))
          linelen--;
      // Append the processed line to the editor buffer
      editorInsertRow(E.numrows, line, linelen);
  }

  // Clean up resources used by getline and fopen
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    // Pass NULL for the callback since save doesn't need live updates
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save cancelled");
      return;
    }
		editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

	static int saved_hl_line;
	static char *saved_hl = NULL;

	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1) direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);

    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      // E.rowoff = E.numrows;

			saved_hl_line = current;
			saved_hl = malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind() {
  // Keep track of original cursor and scroll to restore on cancel
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                             editorFindCallback);

  if (query) {
    free(query);
  } else {
    E.cx = saved_cx; // Restore cursor position
    E.cy = saved_cy;
    E.coloff = saved_coloff; // Restore scroll position
    E.rowoff = saved_rowoff;
    editorSetStatusMessage(""); // Clear search message
    return;
  }
}


/*** append buffer ***/
/* This section defines a simple dynamic string buffer (append buffer, abuf)
 * used to construct the output to be written to the terminal at once,
 * reducing flicker compared to many small writes. */

struct abuf {
    char *b;    // Pointer to the buffer memory
    int len;    // Current length of the string in the buffer
};

// Macro to initialize an append buffer
#define ABUF_INIT {NULL, 0}

/*
 * Appends a string 's' of length 'len' to the append buffer 'ab'.
 * Dynamically reallocates the buffer memory as needed.
 */
void abAppend(struct abuf *ab, const char *s, int len) {
    // Reallocate memory for the existing buffer + new string length
    char *new_buf = realloc(ab->b, ab->len + len);

    // Check if reallocation failed
    if (new_buf == NULL) {
        // In a real application, might try to handle this more gracefully.
        // For Kilo, returning is okay for now, but could lead to incomplete screen updates.
        return;
    }

    // Copy the new string 's' to the end of the newly allocated buffer
    memcpy(new_buf + ab->len, s, len);

    // Update the append buffer's pointer and length
    ab->b = new_buf;
    ab->len += len;
}

/*
 * Frees the memory allocated for the append buffer.
 */
void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

/*
 * Adjusts the row and column offsets (E.rowoff, E.coloff) based on the
 * current cursor position (E.cx, E.cy) to ensure the cursor stays
 * within the visible window area. Called before redrawing the screen.
 */
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  // Vertical scroll: If cursor is above the visible window
  if (E.cy < E.rowoff) {
      E.rowoff = E.cy;
  }
  // Vertical scroll: If cursor is below the visible window
  if (E.cy >= E.rowoff + E.screenrows) {
      E.rowoff = E.cy - E.screenrows + 1;
  }
  // Horizontal scroll: If cursor is left of the visible window
  if (E.rx < E.coloff) {
      E.coloff = E.rx;
  }
  // Horizontal scroll: If cursor is right of the visible window
  if (E.rx >= E.coloff + E.screencols) {
      E.coloff = E.rx - E.screencols + 1;
  }
}

/*
 * Fills the append buffer 'ab' with the content to be displayed on screen.
 * Draws each row, handling file content, tildes for empty lines,
 * and the welcome message. Accounts for scrolling (E.rowoff, E.coloff).
 */
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = editorSyntaxToColour(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

/*
Displays status bar with inverted colors: black text on a white background.
The escape sequence <esc>[7m switches to inverted colors;
<esc>[m switches back to normal formatting.
*/
void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    E.filename ? E.filename : "[Untitled]", E.numrows,
    E.dirty ? "(modified)" : "");

  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax? E.syntax->filetype : "no filetype", E.cy + 1, E.numrows);

  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}


/*
 * Refreshes the entire screen.
 * Hides cursor, adjusts scroll, prepares the output buffer, draws rows,
 * positions the cursor, shows cursor, writes the buffer to stdout, and frees the buffer.
 */
void editorRefreshScreen() {
    // Make sure scrolling offsets are up-to-date before drawing
    editorScroll();

    struct abuf ab = ABUF_INIT; // Initialize an empty append buffer

    // ANSI escape sequences:
    // \x1b[?25l: Hide cursor
    // \x1b[H: Move cursor to home position (top-left, 1;1)
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    // Draw the content (file rows, tildes, welcome message) into the buffer
    editorDrawRows(&ab);
    // Draws status bar at the bottom of the screen.
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Move cursor to its correct position (E.cx, E.cy) adjusted for scrolling
    char buf[32];
    // Note: Terminal rows/cols are 1-based, Kilo's cx/cy are 0-based.
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                              (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    // \x1b[?25h: Show cursor
    abAppend(&ab, "\x1b[?25h", 6);

    // Write the entire buffer content to standard output in one go
    write(STDOUT_FILENO, ab.b, ab.len);
    // Free the memory used by the append buffer
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}


/*** input ***/
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf); // Pass buf here so %s gets filled
    editorRefreshScreen();

    int c = editorReadKey();

    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
        if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') { // Escape key
        editorSetStatusMessage(""); // Clear status message on cancel
        if (callback) callback(buf, c); // Notify callback about escape
        free(buf);
        return NULL; // Return NULL for cancellation
    } else if (c == '\r') { // Enter key
        if (buflen != 0) { // Only return if something was typed
            editorSetStatusMessage(""); // Clear status message on success
            if (callback) callback(buf, c); // Notify callback about enter
            // buf[buflen] = '\0'; // Ensure null termination (already handled below)
            return buf; // Return the collected input
        }
        // If buffer is empty, Enter does nothing (or you could decide to cancel)
    } else if (!iscntrl(c) && c < 128) { // Regular character input
        if (buflen == bufsize - 1) { // Check if buffer needs resizing *before* adding
            bufsize *= 2;
            buf = realloc(buf, bufsize);
            if (buf == NULL) die("realloc"); // Handle allocation failure
        }
        buf[buflen++] = c; // Append the character
        buf[buflen] = '\0'; // Null-terminate the buffer
    }

    if (callback) callback(buf, c); // Update callback on other keypresses if needed
  }
}

/*
 * Moves the cursor based on the provided key code (ARROW_UP, DOWN, LEFT, RIGHT).
 * Handles moving between lines and prevents moving outside file boundaries.
 * Adjusts E.cx to stay within the bounds of the new line after vertical movement.
 */
void editorMoveCursor(int key) {
    // Get a pointer to the current row, or NULL if cursor is beyond file content
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--; // Move left within the line
            } else if (E.cy > 0) {
                // Move to the end of the previous line if at start of current line
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++; // Move right within the line
            } else if (row && E.cx == row->size) {
                 // Move to the start of the next line if at end of current line
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--; // Move up one row
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) { // Allow moving cursor one line past the last line
                E.cy++; // Move down one row
            }
            break;
    }

    // After moving UP or DOWN, snap E.cx to the end of the line if it's past it.
    // Get the row pointer for the *new* cursor line.
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0; // Get length of the new row (0 if past end)
    if (E.cx > rowlen) {
        E.cx = rowlen; // Snap cursor horizontally
    }
}

/*
 * Reads a keypress using editorReadKey() and processes it.
 * Handles quitting (Ctrl+Q), cursor movement keys, Home, End, PageUp, PageDown.
 */
void editorProcessKeypress() {
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey(); // Get the next keypress (char or special key code)

  switch (c) {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }

      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
        E.cx = 0; // Move cursor to beginning of the line
        break;

    case END_KEY:
        // Move cursor to end of the current line content
          if (E.cy < E.numrows) // Check if cursor is on a valid line
              E.cx = E.row[E.cy].size;
        break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
        { // Block scope for variable declaration
            // Move cursor up/down by a full screen height
            if (c == PAGE_UP) {
                E.cy = E.rowoff; // Move cursor to top of screen
            } else if (c == PAGE_DOWN) {
                E.cy = E.rowoff + E.screenrows - 1; // Move cursor to bottom
                if (E.cy > E.numrows) E.cy = E.numrows; // Don't go past end of file
            }

            int times = E.screenrows;
            while (times--) // Simulate multiple arrow key presses
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

    // Arrow keys delegate to editorMoveCursor
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    
    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
      editorInsertChar(c);
      break;
  }

  quit_times = KILO_QUIT_TIMES;
}


/*** init ***/

/*
 * Initializes the editor state structure E.
 * Sets initial cursor position, scroll offsets, clears row data,
 * and gets the terminal window size.
 */
void initEditor() {
  E.cx = 0; // Initial cursor column
  E.cy = 0; // Initial cursor row
  E.rx = 0; // Index into the render field
  E.rowoff = 0; // Initial vertical scroll
  E.coloff = 0; // Initial horizontal scroll
  E.numrows = 0; // No rows loaded initially
  E.row = NULL; // No row data allocated initially
  E.dirty = 0;
  E.filename = NULL; // No filename set initially
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
	E.syntax = NULL;

  // Get terminal dimensions
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

/*** main ***/

/*
 * Main function: Entry point of the program.
 * Enables raw mode, initializes editor state, opens file if provided,
 * and enters the main loop to process keypresses and refresh the screen.
 */
int main(int argc, char *argv[]) {
    enableRawMode(); // Switch terminal to raw mode
    initEditor();    // Initialize editor state
    // If a filename was provided as a command-line argument, open it
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    // Main event loop
    while (1) {
        editorRefreshScreen();   // Redraw the screen
        editorProcessKeypress(); // Wait for and handle user input
    }

    return 0; // Technically unreachable due to infinite loop and exit()
}
