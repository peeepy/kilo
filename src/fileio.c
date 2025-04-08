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

/**
 * @brief Extracts the directory path from the given filename.
 * @param filename The full path to the file.
 * @return A dynamically allocated string containing the directory path (e.g., ".", "/home/user"),
 * or NULL if memory allocation fails. The caller must free the returned string.
 * Returns "." if filename is NULL or has no path separator.
 */
char *getEditingDirname(const char *filename) {
    char *allocated_dirname = NULL;

    if (!filename) {
        // Return "." for current directory if no filename provided
        allocated_dirname = strdup(""); // strdup allocates memory
        return allocated_dirname; // Return pointer (or NULL if strdup fails)
    }

    char *last_slash = strrchr(filename, '/');
    char *last_backslash = strrchr(filename, '\\');
    char *last_separator = NULL;

    if (last_slash && last_backslash) {
        last_separator = (last_slash > last_backslash) ? last_slash : last_backslash;
    } else {
        last_separator = last_slash ? last_slash : last_backslash;
    }

    if (last_separator) {
        int dir_len = last_separator - filename;
        allocated_dirname = malloc(dir_len + 1); // +1 for the null terminator
        if (!allocated_dirname) {
            perror("malloc failed in getEditingDirname");
            return NULL; // Allocation failed
        }
        strncpy(allocated_dirname, filename, dir_len);
        allocated_dirname[dir_len] = '\0';
    } else {
        // No separator, duplicate "." for current directory
        allocated_dirname = strdup(".");
        // No need to check return here, strdup returns NULL on failure anyway
    }

    return allocated_dirname;
}

// Function to find the basename part of a path
// Returns a pointer within the original string, or a default string
char *findBasename(const char *path) {
    if (path == NULL) {
        return "[No Name]";
    }

    char *last_slash = strrchr(path, '/');
    char *last_backslash = strrchr(path, '\\');
    char *last_separator = NULL;

    if (last_slash && last_backslash) {
        last_separator = (last_slash > last_backslash) ? last_slash : last_backslash;
    } else {
        last_separator = last_slash ? last_slash : last_backslash;
    }

    if (last_separator) {
        return last_separator + 1; // Return pointer to char after the separator
    } else {
        return path; // No separator, the whole path is the basename
    }
}


/*
 * Opens the specified file, reads its content line by line,
 * and appends each line to the editor buffer using editorAppendRow.
 */
void editorOpen(char *filename) {
    // Free old filename and dirname before getting new ones
    free(E.filename);
    free(E.dirname); // Free the old directory path
    // E.filename = NULL; // Prevent using freed memory in getEditingDirname
    // E.dirname = NULL;

    E.filename = strdup(filename); // Allocate new filename
    if (!E.filename) {
        die("Failed to allocate memory for filename");
    }

    E.dirname = getEditingDirname(E.filename); // Allocate new dirname
     if (!E.dirname) {
        // Handle allocation failure
        // Maybe free E.filename again and report error?
        free(E.filename);
        E.filename = NULL;
        die("Failed to allocate memory for dirname");
    }

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