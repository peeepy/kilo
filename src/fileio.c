#include "kilo.h"
#include <fcntl.h>

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