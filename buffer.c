// buffer.c - Contains functions for buffer management

#include <stdlib.h>
#include <string.h>
// #include "buffer.h"
#include "kilo.h"

/**
 * Create a new buffer.
 * @return A pointer to a newly allocated editorBuffer with default values
 */
editorBuffer *editorCreateBuffer(void) {
    editorBuffer *buf = (editorBuffer *) malloc(sizeof(editorBuffer));
    if (!buf) die("malloc failed in editorCreateBuffer");
    
    // Initialize with default values
    buf->filename = NULL;
    buf->dirty = 0;
    buf->row = NULL;
    buf->numrows = 0;
    buf->cx = 0;
    buf->cy = 0;
    buf->rx = 0;
    buf->rowoff = 0;
    buf->coloff = 0;
    buf->syntax = NULL;
    buf->next = NULL;
    buf->prev = NULL;
    buf->parent_dir_fd = -1;
    buf->owns_parent_dir_fd = false;
    buf->dirname = NULL;
    
    return buf;
}

/**
 * Adds a new buffer to the buffer list.
 * @param buf The new buffer to add
 */
void editorAddBuffer(editorBuffer *buf) {
    if (!buf) return;
    
    // If this is the first buffer
    if (E.buffer_list_head == NULL) {
        E.buffer_list_head = buf;
        buf->next = NULL;
        buf->prev = NULL;
    } else {
        // Add at the end of the list
        editorBuffer *current = E.buffer_list_head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = buf;
        buf->prev = current;
        buf->next = NULL;
    }
    
    E.num_buffers++;
}

/**
 * Switches to the specified buffer, saving current state.
 * @param targetBuffer The buffer to switch to
 */
void editorSwitchBuffer(editorBuffer *targetBuffer) {
    if (!targetBuffer) return;
    
    // Save the current buffer's state if there is one
    if (E.current_buffer) {
        E.current_buffer->cx = E.cx;
        E.current_buffer->cy = E.cy;
        E.current_buffer->rx = E.rx;
        E.current_buffer->rowoff = E.rowoff;
        E.current_buffer->coloff = E.coloff;
    }
    
    // Set the new current buffer
    E.current_buffer = targetBuffer;
    
    // Load the new buffer's state into E
    E.cx = targetBuffer->cx;
    E.cy = targetBuffer->cy;
    E.rx = targetBuffer->rx;
    E.rowoff = targetBuffer->rowoff;
    E.coloff = targetBuffer->coloff;
    E.row = targetBuffer->row;
    E.numrows = targetBuffer->numrows;
    E.dirty = targetBuffer->dirty;
    E.filename = targetBuffer->filename;
    // E.dirname = getEditingDirname(targetBuffer->filename);
    E.syntax = targetBuffer->syntax;
    
    // Update syntax highlighting if necessary
    editorSelectSyntaxHighlight();
    editorClearStatusMessage();
}

/**
 * Find the next buffer in the list and switch to it.
 */
void editorFindNextBuffer(void) {
    if (!E.current_buffer || !E.current_buffer->next) return;
    
    editorSwitchBuffer(E.current_buffer->next);
}

/**
 * Find the previous buffer in the list and switch to it.
 */
void editorFindPrevBuffer(void) {
    if (!E.current_buffer || !E.current_buffer->prev) return;
    
    editorSwitchBuffer(E.current_buffer->prev);
}

/**
 * Free a buffer's resources and remove it from the list.
 * @param bufferToClose The buffer to close and free
 */
void editorCloseBuffer(editorBuffer *bufferToClose) {
    if (!bufferToClose) return;
    
    // If we're closing the current buffer, switch to another one if possible
    if (bufferToClose == E.current_buffer) {
        if (bufferToClose->next) {
            editorSwitchBuffer(bufferToClose->next);
        } else if (bufferToClose->prev) {
            editorSwitchBuffer(bufferToClose->prev);
        } else {
            // This was the only buffer, create a new empty one
            E.current_buffer = NULL;
            E.row = NULL;  // Important: Set to NULL to avoid dangling pointer
            E.numrows = 0;
            E.dirty = 0;
            E.filename = NULL;
            E.syntax = NULL;
            E.cx = 0;
            E.cy = 0;
            E.rx = 0;
            E.rowoff = 0;
            E.coloff = 0;
        }
    }
    
    // Remove from linked list
    if (bufferToClose->prev) {
        bufferToClose->prev->next = bufferToClose->next;
    } else {
        // If this is the head of the list
        E.buffer_list_head = bufferToClose->next;
    }
    
    if (bufferToClose->next) {
        bufferToClose->next->prev = bufferToClose->prev;
    }
    
    // Free all resources
    for (int i = 0; i < bufferToClose->numrows; i++) {
        editorFreeRow(&bufferToClose->row[i]);
    }

    if (bufferToClose->owns_parent_dir_fd && bufferToClose->parent_dir_fd != -1) {
        close(bufferToClose->parent_dir_fd);
    }

    free(bufferToClose->filename);
    free(bufferToClose->dirname);
    free(bufferToClose->row);
    free(bufferToClose);
    
    E.num_buffers--;
}

/**
 * Close the current buffer.
 */
void editorCloseCurrentBuffer(void) {
    if (!E.current_buffer) return;
    
    // Check if there are unsaved changes
    if (E.current_buffer->dirty) {
        // Prompt for confirmation (similar to Ctrl+Q logic in editorProcessKeypress)
        editorSetStatusMessage("WARNING! Buffer has unsaved changes. Press Ctrl-W again to close.");
        // This would need a way to track that the next Ctrl-W should force close
        // For now we'll assume there's another mechanism for this
        return;
    }
    
    editorBuffer *toClose = E.current_buffer;
    editorCloseBuffer(toClose);
}