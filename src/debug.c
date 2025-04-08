#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include "kilo.h"
#include "debug.h"

// Global variables
int debug_overlay_active = 0;
int user_dismissed_overlay = 0;
char debug_buffer[DEBUG_BUFFER_SIZE];
int debug_buffer_len = 0;

// Displayed errors state
char displayed_errors[MAX_DISPLAYED_ERRORS][MAX_ERROR_LEN];
int displayed_error_count = 0;

// Original file state (keep as is)
char original_filename[1024] = {0};
struct erow *original_rows = NULL;
int original_numrows = 0;

// --- Raw Logging (mostly unchanged) ---
int debug_printf(const char* fmt, ...) {
    // ... (implementation unchanged - logs to debug_buffer and file) ...
    va_list args;
    char message[1024];

    va_start(args, fmt);
    int result = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    appendToDebugBuffer(message); // Log to internal buffer

    // Also write to a real file for backup
    FILE* log_file = fopen("/tmp/kilo_debug.log", "a");
    if (log_file) {
        fprintf(log_file, "%s", message);
        fclose(log_file);
    }
    return result;
}

void appendToDebugBuffer(const char* text) {
    // ... (implementation unchanged - appends to debug_buffer) ...
     int len = strlen(text);

    // Ensure we don't overflow the buffer
    if (debug_buffer_len + len >= DEBUG_BUFFER_SIZE) {
        // If adding this would overflow, move everything up to make room
        int offset = (debug_buffer_len + len) - DEBUG_BUFFER_SIZE + 1; // Calculate exact overflow
        if (offset <= 0) offset = len; // Should not happen if check is correct, but safeguard
        if (offset >= debug_buffer_len) { // If new message larger than buffer
             debug_buffer_len = 0; // Just clear it
        } else {
             memmove(debug_buffer, debug_buffer + offset, debug_buffer_len - offset);
             debug_buffer_len -= offset;
        }
    }

    // Append the new text
    memcpy(debug_buffer + debug_buffer_len, text, len);
    debug_buffer_len += len;
    debug_buffer[debug_buffer_len] = '\0'; // Ensure null termination
}


// --- Displayed Error Management ---

// Clear the list of errors shown in the overlay
void clearDisplayedErrors(void) {
    displayed_error_count = 0;
    // No need to clear the actual strings, count=0 means they are ignored

}

// Check if an error message is already in our displayed list
static int isErrorDisplayed(const char* error_message) {
    for (int i = 0; i < displayed_error_count; i++) {
        if (strncmp(displayed_errors[i], error_message, MAX_ERROR_LEN - 1) == 0) {
            return 1; // Found
        }
    }
    return 0; // Not found
}

// Add a unique error to the displayed list
static void addErrorToDisplay(const char* error_message) {
    if (displayed_error_count < MAX_DISPLAYED_ERRORS) {
        strncpy(displayed_errors[displayed_error_count], error_message, MAX_ERROR_LEN - 1);
        displayed_errors[displayed_error_count][MAX_ERROR_LEN - 1] = '\0'; // Ensure null termination
        displayed_error_count++;
    } else {
        // Optional: Overwrite the oldest error (index 0) and shift others?
        // Simpler: Just log that the display buffer is full
        debug_printf("WARNING: Displayed errors buffer full. Cannot show: %s\n", error_message);
    }
}


// --- Error Handling Logic ---

void showDebugOnError(const char* error_message) {
    // 1. Always log the raw error
    // debug_printf("ERROR: %s\n", error_message);

    // 2. Check if this error should be added to the *visual* overlay list
    if (!isErrorDisplayed(error_message)) {
        addErrorToDisplay(error_message);

        // 3. Activate overlay only if it's a new error AND overlay is closed AND not user-dismissed
        if (!debug_overlay_active && !user_dismissed_overlay) {
            debug_overlay_active = 1;
            // user_dismissed_overlay = 0; // Activation implies not dismissed
            editorSetStatusMessage("Error detected - Press Ctrl-D to dismiss debug overlay");
        }
    }
    // If error was already displayed, do nothing to overlay state or display list.
}

// --- Init and Cleanup ---

void initDebug(void) {
    unlink("/tmp/kilo_debug.log"); // Clear old log file
    debug_buffer[0] = '\0';
    debug_buffer_len = 0;
    displayed_error_count = 0;
    // Decide initial state: Start with overlay OFF by default
    debug_overlay_active = 0; // << Set to 1 if you want it ON by default
    user_dismissed_overlay = 0;

    atexit(cleanupDebugLog);
     // If starting active:
     // if (debug_overlay_active) {
     //     editorSetStatusMessage("DEBUG OVERLAY - Press Ctrl-D to dismiss");
     // }
}

void cleanupDebugLog(void) {
    unlink("/tmp/kilo_debug.log"); // Keep log on exit? Or delete? Your choice.
}

// Toggle function (optional, if F12 or similar is used)
// Make sure it interacts correctly with user_dismissed_overlay and clearDisplayedErrors if kept
void toggleDebugOverlay(void) {
     // This function needs careful review if you keep it alongside the Ctrl+D logic
     // For now, recommend using only the direct Ctrl+D handler in editor.c
     debug_printf("toggleDebugOverlay called - behavior might be inconsistent with Ctrl+D handler.\n");
}