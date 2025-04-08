#ifndef DEBUG_H
#define DEBUG_H

// Add these back if they were removed:
#define DEBUG_OVERLAY_WIDTH 60   // Or your preferred width
#define DEBUG_OVERLAY_HEIGHT 10  // Or your preferred height

#include "kilo.h" // Include kilo.h for erow struct if needed here, or forward declare

#define DEBUG_BUFFER_SIZE (1024 * 16) // Raw log buffer
#define MAX_DISPLAYED_ERRORS 8       // Max unique errors shown in overlay (adjust as needed)
#define MAX_ERROR_LEN 256           // Max length of a single error message to display
#define MAX_OVERLAY_RAW_LINES 256 // Adjust if needed, max lines shown from raw buffer


extern int debug_overlay_active;
extern int user_dismissed_overlay; // Keep this flag

// Raw log buffer (unchanged)
extern char debug_buffer[DEBUG_BUFFER_SIZE];
extern int debug_buffer_len;

// Buffer for unique errors displayed in the overlay
extern char displayed_errors[MAX_DISPLAYED_ERRORS][MAX_ERROR_LEN];
extern int displayed_error_count;

// Original file state (seems unrelated to overlay logic, keep as is)
extern char original_filename[1024];
extern struct erow *original_rows;
extern int original_numrows;

// Function Prototypes
int debug_printf(const char* fmt, ...);
void appendToDebugBuffer(const char* text); // Keep this for raw logging if used directly
void showDebugOnError(const char* error_message);
void toggleDebugOverlay(void); // May not be needed if Ctrl+D handles directly
void cleanupDebugLog(void);
void initDebug(void);
void clearDisplayedErrors(void); // New function

#endif