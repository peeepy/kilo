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
    enableRawMode(); // Switch terminal to raw mode
    atexit(freeSyntaxDefs); // Register cleanup function
    atexit(freeThemeColors);
    
    initEditor(); // Initialize editor state

    // If a filename was provided as a command-line argument, open it
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    initLua();    // Initialize Lua state

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");


    // Main event loop
    while (1) {
        editorRefreshScreen();   // Redraw the screen
        editorProcessKeypress(); // Wait for and handle user input
    }

    return 0; // Technically unreachable due to infinite loop and exit()
}
