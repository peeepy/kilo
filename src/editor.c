#include "kilo.h"


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