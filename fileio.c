#include "kilo.h"
#include <fcntl.h>
#include "dirtree.h"

char *editorRowsToString(editorBuffer *buf, int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < buf->numrows; j++)
    totlen += buf->row[j].size + 1;
  *buflen = totlen;

  char *buffer = malloc(totlen);
  char *p = buffer;
  for (j = 0; j < buf->numrows; j++) {
    memcpy(p, buf->row[j].chars, buf->row[j].size);
    p += buf->row[j].size;
    *p = '\n';
    p++;
  }

  return buffer;
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
const char *findBasename(const char *path) {
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

// Helper function to insert a row into a specific buffer
// void editorInsertRowToBuffer(editorBuffer *buf, int at, char *s, size_t len) {
//     if (!buf || at < 0 || at > buf->numrows) return;

//     buf->row = realloc(buf->row, sizeof(erow) * (buf->numrows + 1));
//     if (!buf->row) die("realloc failed in editorInsertRowToBuffer");

//     // Move existing rows if inserting in the middle
//     if (at < buf->numrows) {
//        memmove(&buf->row[at + 1], &buf->row[at], sizeof(erow) * (buf->numrows - at));
//     }

//     // Initialize the new row
//     buf->row[at].idx = at;
//     buf->row[at].size = len;
//     buf->row[at].chars = malloc(len + 1);
//     if (buf->row[at].chars == NULL) die("malloc");

//     memcpy(buf->row[at].chars, s, len);
//     buf->row[at].chars[len] = '\0';

//     buf->row[at].rsize = 0;
//     buf->row[at].render = NULL;
//     buf->row[at].hl = NULL;
//     buf->row[at].hl_open_comment = 0;

//     // Update row rendering
//     editorUpdateRow(&buf->row[at]);

//     buf->numrows++;
//     buf->dirty++;
    
//     // Update global dirty flag if this is the current buffer
//     if (buf == E.current_buffer) {
//         E.dirty = buf->dirty;
//     }

// }

// "fixed" function:
void editorInsertRowToBuffer(editorBuffer *buf, int at, char *s, size_t len) {
    if (!buf || at < 0 || at > buf->numrows) return;

    buf->row = realloc(buf->row, sizeof(erow) * (buf->numrows + 1));
    if (!buf->row) die("realloc failed in editorInsertRowToBuffer");

    // Move existing rows if inserting in the middle
    if (at < buf->numrows) {
       memmove(&buf->row[at + 1], &buf->row[at], sizeof(erow) * (buf->numrows - at));
    }

    // Initialize the new row
    buf->row[at].idx = at;
    buf->row[at].size = len;
    buf->row[at].chars = malloc(len + 1);
    if (buf->row[at].chars == NULL) die("malloc");

    memcpy(buf->row[at].chars, s, len);
    buf->row[at].chars[len] = '\0';

    buf->row[at].rsize = 0;
    buf->row[at].render = NULL;
    buf->row[at].hl = NULL;
    buf->row[at].hl_open_comment = 0;

    buf->numrows++;
    buf->dirty++;

    // Update row rendering
    editorUpdateRow(&buf->row[at]);
    
    // Update global dirty flag if this is the current buffer
    if (buf == E.current_buffer) {
        E.dirty = buf->dirty;
        E.row = buf->row;  // Only update global row pointer if this is the current buffer
        E.numrows = buf->numrows;
    }
}



/*
 * Opens the specified file, reads its content line by line,
 * and creates a new buffer with the file content.
 * @return A pointer to the newly created buffer, or NULL on failure.
 */
editorBuffer *editorOpen(char *filename, DirTreeNode *node) {
    // Create a new buffer
    editorBuffer *buf = editorCreateBuffer();
    if (!buf) die("Failed to create buffer");

    buf->owns_parent_dir_fd = false;
    buf->dirname = NULL;
    buf->parent_dir_fd = -1;

    // Set filename and dirname
    buf->filename = strdup(filename);
    if (!buf->filename) {
        free(buf);
        die("Failed to allocate memory for filename");
    }
    
    // Get directory name
    char *dirname = getEditingDirname(buf->filename);
    buf->dirname = dirname;
    if (!dirname) {
        free(buf->filename);
        free(buf);
        die("Failed to allocate memory for dirname");
    }
    
    buf->tree_node = node;

    if (node && node->parent) {
        buf->parent_dir_fd = node->parent->fd;
    } else {
        buf->parent_dir_fd = open(buf->dirname, O_RDONLY);
        if (buf->parent_dir_fd != -1) {
            buf->owns_parent_dir_fd = true;
        } else {
            debug_printf("Could not open parent directory.");
            perror("Parent directory failed to open");
        }
    }


    // Select syntax highlighting
    buf->syntax = NULL; // Clear it first
    // We'll need to modify editorSelectSyntaxHighlight to work with a buffer
    // For now, we'll set syntax after switching to this buffer
    
    // Read the file
    // TODO: Use fdopen() instead; replace filename with filedescriptor from DirTreeNode->fd?
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        // File doesn't exist - return the empty buffer
        // Set dirty to 0 since the buffer is empty but doesn't need saving yet
        return buf;
    }
    
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    // Read lines
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        // Strip trailing newline and carriage return
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        
        // Insert into buffer's row array
        editorInsertRowToBuffer(buf, buf->numrows, line, linelen);
    }
    
    // Clean up
    free(line);
    fclose(fp);
    buf->dirty = 0;
    return buf;
}

void editorSave() {
    if (!E.current_buffer) return;
    
    // Check if filename exists
    if (E.current_buffer->filename == NULL) {
        // 1. Get the directory name for the prompt (use "." if none)
        const char *current_dir = "."; // Default
        // Ensure current_buffer and its dirname are valid
        if (E.current_buffer && E.current_buffer->dirname) {
            current_dir = E.current_buffer->dirname;
        }

        // 2. Create the format string for editorPrompt.
        // We want "Save as: /path/to/dir/%s" where %s will be filled by user input.
        // Calculate needed size: "Save as: " + dir + "/" + "%s" + null terminator
        size_t needed = snprintf(NULL, 0, "Save as: %s/%%s", current_dir) + 1; // Use %%s for literal %, +1 for null
        char *prompt_format = malloc(needed);
        if (!prompt_format) {
             editorSetStatusMessage("Error: Memory allocation failed for prompt");
             return; // Or handle error appropriately
        }
        snprintf(prompt_format, needed, "Save as: %s/%%s", current_dir);

        // 3. Call editorPrompt with the generated format string
        char *filename_part = editorPrompt(prompt_format, NULL); // User types only the filename part

        // 4. Clean up the format string (no longer needed)
        free(prompt_format);

        // 5. Handle cancellation
        if (filename_part == NULL) {
            editorSetStatusMessage("Save cancelled");
            return;
        }

        // 6. Construct the full path from the directory and the user's input
        // Calculate needed size: dir + "/" + filename_part + null terminator
        needed = snprintf(NULL, 0, "%s/%s", current_dir, filename_part) + 1;
        char *full_save_path = malloc(needed);
        if (!full_save_path) {
             editorSetStatusMessage("Error: Memory allocation failed for full path");
             free(filename_part); // Free the buffer returned by editorPrompt
             return;
        }
        snprintf(full_save_path, needed, "%s/%s", current_dir, filename_part);

        // 7. Clean up the relative filename part returned by editorPrompt
        free(filename_part);

        // 8. Assign the newly constructed full path to the buffer
        // (Free existing filename if overwriting - though it should be NULL here)
        free(E.current_buffer->filename); // Free old name if any (should be NULL)
        E.current_buffer->filename = full_save_path; // Buffer now owns the full path string


        // E.filename = E.current_buffer->filename;

        // 9. Update syntax highlighting etc. for the new name
        editorSelectSyntaxHighlight();
    }
    
    // Get buffer content
    int len;
    char *buf = editorRowsToString(E.current_buffer, &len);
    
    int fd = -1;
    if (E.current_buffer->parent_dir_fd != -1) {
        const char *base = findBasename(E.current_buffer->filename);
        fd = openat(E.current_buffer->parent_dir_fd, base, O_RDWR | O_CREAT | O_TRUNC, 0644);
    }
    // Fallback or if fd is still -1
    if (fd == -1) {
        fd = open(E.current_buffer->filename, O_RDWR | O_CREAT | O_TRUNC, 0644); // Added O_TRUNC; standard for overwriting saves
    }

    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                
                // Update dirty flags
                E.current_buffer->dirty = 0;
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