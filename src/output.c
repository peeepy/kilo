#include "kilo.h"
#include <time.h> 
#include <stdarg.h>
#include <math.h>

/*** append buffer ***/

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

    // Adjust width dynamically based on E.numrows
    int ln_width = (E.numrows == 0) ? KILO_LINE_NUMBER_WIDTH : (int)log10(E.numrows) + 1 + 2; // Digits + padding/separator

    // Set background color and clear the entire line first
    abAppend(ab, "\x1b[48;5;236m", 11); // Set BG if needed
    abAppend(ab, "\x1b[K", 3);          // Erase line with current BG


   // --- Draw Line Number ---
    char linenum[32];

    if (filerow < E.numrows) {
        // Format the line number (right-aligned in ln_width - 1 columns, plus a space)
        snprintf(linenum, sizeof(linenum), "%*d ", ln_width - 1, filerow + 1);

        abAppend(ab, "\x1b[90m", 5); // Set line number color (dark grey)
        abAppend(ab, linenum, strlen(linenum));
        abAppend(ab, "\x1b[39m", 5); // Reset foreground color
    } else {
        // Fill the line number area with spaces or a marker for empty lines
        // Draw a colored '~' for consistency
        snprintf(linenum, sizeof(linenum), "%*s", ln_width, ""); // Fill with spaces
        // Or draw a tilde like the original code:
        // lineno[0] = '~';
        // snprintf(&lineno[1], sizeof(lineno)-1, "%*s", ln_width - 1, "");
        // abAppend(ab, linenum, strlen(linenum)); // Append spaces
    }
    // --- End Draw Line Number ---


    // --- Draw Row Content / Welcome / Tilde ---

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

          abAppend(ab, "\x1b[7m", 4);   // Invert swaps foreground and background

          abAppend(ab, &sym, 1);

          abAppend(ab, "\x1b[27m", 4);  // Turn OFF invert specifically
        
          // If the next character isn't highlighted, we need default foreground.
        // If it IS highlighted, the highlighting logic below will set the color.
        // Check if the *next* character's highlight is HL_NORMAL or if we are at the end
        if (j + 1 == len || hl[j+1] == HL_NORMAL) {
            abAppend(ab, "\x1b[39m", 5); // Reset foreground only if needed
            current_color = -1; // Update state tracking
        }
        // The background remains the dark grey we set earlier.
        // The original syntax highlighting code might need current_color adjusted here.
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
    abAppend(ab, "\x1b[48;5;236m", 11); // Set background to dark grey (color 236)
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;

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

    abAppend(&ab, "\x1b[48;5;236m", 11); // Set background to dark grey (color 236)

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