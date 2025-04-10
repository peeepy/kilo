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
 * Updates the render buffer and highlighting buffer for a given row.
 * Expands tabs in chars into spaces in render.
 * Allocates/reallocates hl buffer based on render size.
 * Calls editorUpdateSyntax to fill the hl buffer.
 */
void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;

    // Calculate number of tabs
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    // --- Update Render Buffer ---
    free(row->render); // Free old render buffer (safe if NULL)
    // Allocate space for render: original size + space for expanded tabs + null terminator
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);
    if (row->render == NULL) die("malloc failed for row->render in editorUpdateRow"); // Added check
    if (!row->render) die("malloc failed for row->render in editorUpdateRow");

    int idx = 0; // Current index in row->render
    // Fill render buffer, expanding tabs
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' '; // Append first space for the tab
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' '; // Append spaces until tab stop
        } else {
            row->render[idx++] = row->chars[j]; // Copy normal character
        }
    }
    row->render[idx] = '\0'; // Null-terminate render string
    row->rsize = idx;        // Store final render size

    // Allocate or resize the 'hl' buffer to match the render size 'rsize'.
    // Using realloc handles both initial allocation (if row->hl is NULL)
    // and resizing if the render size changed from a previous update.
    row->hl = realloc(row->hl, row->rsize);
    // Check if realloc failed (returns NULL) but only if size > 0
    if (row->hl == NULL && row->rsize > 0) {
         die("realloc failed for row->hl in editorUpdateRow");
    }

    // IMPORTANT: Initialize the allocated/reallocated memory.
    // Set all highlight bytes to HL_NORMAL by default before syntax is applied.
    // This prevents reading uninitialized memory later if editorUpdateSyntax doesn't cover every byte.
    if (row->hl) { // Check if hl is valid (allocation succeeded or rsize was 0)
         memset(row->hl, HL_NORMAL, row->rsize);
    }

    editorUpdateSyntax(row);
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at) {
  if (!E.current_buffer || at < 0 || at >= E.numrows) return;
  
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  
  // Update indices for all affected rows
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
  
  E.numrows--;
  E.current_buffer->numrows = E.numrows;
  E.dirty++;
  E.current_buffer->dirty = E.dirty;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  
  row->chars = realloc(row->chars, row->size + 2);
  if (!row->chars) die("realloc failed in editorRowInsertChar");
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  
  E.dirty++;
  if (E.current_buffer) E.current_buffer->dirty = E.dirty;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  if (!row->chars) die("realloc failed in editorRowInsertChar");
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  
  E.dirty++;
  if (E.current_buffer) E.current_buffer->dirty = E.dirty;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  
  E.dirty++;
  if (E.current_buffer) E.current_buffer->dirty = E.dirty;
}