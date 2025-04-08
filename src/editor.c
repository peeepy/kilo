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
        case H_KEY:
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--; // Move left within the line
            } else if (E.cy > 0) {
                // Move to the end of the previous line if at start of current line
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case L_KEY:
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++; // Move right within the line
            } else if (row && E.cx == row->size) {
                 // Move to the start of the next line if at end of current line
                E.cy++;
                E.cx = 0;
            }
            break;
        case K_KEY:
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--; // Move up one row
            }
            break;
        case J_KEY:
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

  int c = editorReadKey();

  // Global keys that work in both modes
  switch (c) {
    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING! File has unsaved changes. "
                               "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return; // Return early to avoid resetting quit_times
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break; // Technically unreachable but good practice

    case CTRL_KEY('s'):
      editorSave();
      quit_times = KILO_QUIT_TIMES; // Reset quit counter on successful save or attempt
      return; // Return early

    case CTRL_KEY('f'):
        editorFind();
        quit_times = KILO_QUIT_TIMES;
        return; // Return early

    case CTRL_KEY('t'):
      {
          char *theme_name = editorPrompt("Theme file name: %s", NULL);
          if (theme_name == NULL) {
              editorSetStatusMessage("Theme change cancelled");
          } else {
              if (theme_name[0] != '\0') {
                  loadTheme(theme_name);
                  // Status message might be set by loadTheme or here
                  // editorSetStatusMessage("Theme loaded: %s", theme_name); // Example
              } else {
                  editorSetStatusMessage("No theme name entered");
              }
              free(theme_name);
          }
      }
      quit_times = KILO_QUIT_TIMES;
      return; // Return early
  }

  // Mode-specific keys
  if (E.mode == MODE_NORMAL) {
    switch (c) {
      case INSERT_KEY:
        E.mode = MODE_INSERT;
        break;
      case H_KEY:
      case J_KEY:
      case K_KEY:
      case L_KEY:
        editorMoveCursor(c);
        break;
      case D_KEY: // Use D_KEY for delete
        // Ensure cursor is not past the end of the line before moving right
        if (E.cy < E.numrows && E.cx < E.row[E.cy].size) {
            editorMoveCursor(ARROW_RIGHT);
        }
        editorDelChar();
        break;
       // Add other normal mode commands here later (e.g., Home, End, Page keys if desired in normal)
       // Maybe map arrow keys here too if you want them in normal mode eventually
      case HOME_KEY:
          E.cx = 0;
          break;
      case END_KEY:
          if (E.cy < E.numrows)
              E.cx = E.row[E.cy].size;
          break;
      case PAGE_UP:
      case PAGE_DOWN:
          {
              if (c == PAGE_UP) {
                  E.cy = E.rowoff;
              } else if (c == PAGE_DOWN) {
                  E.cy = E.rowoff + E.screenrows - 1;
                  if (E.cy > E.numrows) E.cy = E.numrows;
              }
              int times = E.screenrows;
              while (times--)
                  editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
          }
          break;
      // Ignore other keys for now
      default:
        break;
    }
  } else { // MODE_INSERT
    switch (c) {
      case NORMAL_KEY: // Use NORMAL_KEY defined in kilo.h (likely mapped to Esc)
        E.mode = MODE_NORMAL;
        // Optional: Move cursor left to match Vim behavior exiting insert
        // if (E.cx > 0) editorMoveCursor(ARROW_LEFT);
        break;
      case '\r':
        editorInsertNewline();
        break;
      case BACKSPACE:
      case CTRL_KEY('h'):
      case DEL_KEY:
        if (c == DEL_KEY && E.cy < E.numrows && E.cx < E.row[E.cy].size) {
             editorMoveCursor(ARROW_RIGHT);
        }
        editorDelChar();
        break;
      case HOME_KEY:
        E.cx = 0;
        break;
      case END_KEY:
        if (E.cy < E.numrows)
           E.cx = E.row[E.cy].size;
        break;
      case PAGE_UP:
      case PAGE_DOWN:
        {
            if (c == PAGE_UP) {
                E.cy = E.rowoff;
            } else if (c == PAGE_DOWN) {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows) E.cy = E.numrows;
            }
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

      // case '\x1b': // Handle raw escape if NORMAL_KEY wasn't matched or is different
      //     // Can optionally map this to switch to normal mode too as a fallback
      //     // E.mode = MODE_NORMAL;
      //     // editorSetStatusMessage("");
      //     break; // Or just ignore if NORMAL_KEY handles it

        // Typically redraw screen, often mapped to Esc too
      case CTRL_KEY('l'):
        editorRefreshScreen();
        break;

      default:
        // Insert character if it's printable ASCII
        if (!iscntrl(c) && c < 128) {
          editorInsertChar(c);
        }
        break;
    }
  }

  // Reset quit counter if any key other than Ctrl+Q (when dirty) was pressed
  // This happens unless we returned early inside the Ctrl+Q logic
  quit_times = KILO_QUIT_TIMES;
}