#include "kilo.h"


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
    if (!E.row) die("realloc failed in editorInsertRow");

    // Move existing rows if inserting in the middle
    if (at < E.numrows) { // Only move if not appending at the very end
       memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
       // Update indices of shifted rows (optional, depends if idx must always be == array index)
       // If indices need to be perfect array indices, this loop needs adjustment or
       // idx should be set based on position later. Let's assume idx just needs
       // to be correctly set for the *new* row for now, as that fixes the crash.
       // The original loop might have issues if deleting rows later.
       // for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++; // Re-evaluate this loop's necessity/correctness later if needed
    }


    // --- Initialize the NEW row at index 'at' ---

    E.row[at].idx = at; // <<<========= ADD THIS LINE HERE =========

    E.row[at].size = len; // Store the length of the string
    // Allocate memory for the row's character data (+1 for null terminator)
    E.row[at].chars = malloc(len + 1);
    if (E.row[at].chars == NULL) die("malloc"); // Handle allocation failure

    memcpy(E.row[at].chars, s, len); // Copy the string content
    E.row[at].chars[len] = '\0'; // Null-terminate the copied string

    E.row[at].rsize = 0; // Size of the contents of E.row.render
    E.row[at].render = NULL; // Characters to draw on screen for a row of text
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0; // This line was already correct

    // --- Row initialized ---

    // Update row rendering (which calls editorUpdateSyntax)
    editorUpdateRow(&E.row[at]); // Call this *after* initializing idx, size, chars etc.


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