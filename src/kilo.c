/*** includes ***/
#include "kilo.h"
#include "k_lua.h"
#include <locale.h> // Needed for setlocale()

struct editorConfig E; // Global editor state instance

/*** init ***/

/*
 * Initializes the editor state structure E.
 * Sets initial cursor position, scroll offsets, clears row data,
 * and gets the terminal window size.
 */
void initEditor() {
  E.cx = 0; // Initial cursor column
  E.cy = 0; // Initial cursor row
  E.rx = 0; // Index into the render field
  E.rowoff = 0; // Initial vertical scroll
  E.coloff = 0; // Initial horizontal scroll
  E.numrows = 0; // No rows loaded initially
  E.row = NULL; // No row data allocated initially
  E.dirty = 0;
  E.filename = NULL; // No filename set initially
  E.syntax = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.mode = MODE_NORMAL;
  E.buffer_list_head = NULL;
    E.current_buffer = NULL;
    E.num_buffers = 0;
  setlocale(LC_CTYPE, "");

  memset(&E.theme, 0, sizeof(E.theme));
  E.syntax_defs = NULL;
  E.num_syntax_defs = 0;
  loadSyntaxFiles(); // Load definitions from files
  loadTheme("cat_frappe"); // Load theme colours from file
  editorClearStatusMessage();
  
  // Get terminal dimensions
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

/*** main ***/

/*
 * Main function: Entry point of the program.
 * Enables raw mode, initializes editor state, opens file if provided,
 * and enters the main loop to process keypresses and refresh the screen.
 */

int main(int argc, char *argv[]) {
    initDebug(); // Initialize debug system first

    // --- Optional: Check for a debug flag to start with overlay active ---
    // Example: if (argc > 1 && strcmp(argv[1], "--debug-visible") == 0) {
    //     debug_overlay_active = 1;
    //     user_dismissed_overlay = 0; // Ensure not dismissed
    //     // Shift argc/argv if you consume the argument
    // }

    enableRawMode();
    atexit(freeSyntaxDefs);
    atexit(freeThemeColors);

    initEditor();

    // Open file if provided (unchanged)
    if (argc >= 2 /* Adjust if you consumed an arg */) {
        editorOpen(argv[1 /* Adjust index */]);
        // debug_printf("Opened file: %s\n", argv[1 /* Adjust index */]);
    }

    initLua();
    debug_printf("Lua initialized\n");

    // Set initial status message (unchanged or adapt if overlay starts visible)
     if (debug_overlay_active) {
         editorSetStatusMessage("DEBUG OVERLAY - Press Ctrl-D to dismiss");
     } else {
         editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-D = Debug Log");
     }


    // Main loop (unchanged)
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0; // Unreachable
}