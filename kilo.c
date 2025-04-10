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
    // Initialize text area dimensions with defaults
  E.content_start_row = 2; // After tabline
  E.content_start_col = 1; // Default: no left panel
  E.content_width = E.screencols; // Full width initially
  
  memset(&E.panel_state, 0, sizeof(DirTreeState));     // Explicitly zero panel state struct
  memset(&E.navigator_state, 0, sizeof(NavigatorState)); // Explicitly zero navigator state struct
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

    // --- Initialise Panel State ---
    E.panel_visible = false;         // Or false to make it not visible by default
    E.panel_mode = PANEL_MODE_LEFT; // NONE, LEFT, RIGHT, FLOAT
    E.panel_render_width = 30; // default, for reserve_cols()
    E.panel_root_node = NULL;        // Initialise pointers to NULL
    E.panel_current_dir_node = NULL;
    E.panel_view_offset = 0;
    E.panel_selected_index = -1;   // Use -1 for no selection initially

    // --- Initialise Navigator State ---
    E.navigator_active = false;      // Start inactive
    E.navigator_base_node = NULL;    // Initialise pointer to NULL
    E.navigator_search_query = NULL; // If using a pointer, init to NULL (or empty string if static array)
    E.navigator_selected_index = 0;
    E.navigator_view_offset = 0;

    // Overlay state
    E.num_active_overlays = 0;
    memset(E.active_overlays, 0, sizeof(E.active_overlays));

    char initial_cwd[PATH_MAX];
    if (getcwd(initial_cwd, PATH_MAX) != NULL) {
        E.project_root = strdup(initial_cwd);
        if (!E.project_root) die("strdup project_root failed");
    } else {
        die("getcwd failed");
    }

    E.panel_root_node = createTreeNode(E.project_root, NULL, NODE_DIR);
    if (!E.panel_root_node) {
        // Handle error - maybe print a message, maybe proceed without panel root?
        debug_printf("Warning: Could not create root node for file panel.");
        // E.panel_current_dir_node = NULL;
    }
    // Initially, the panel might show the root
    E.panel_current_dir_node = E.panel_root_node;
    // Navigator might also default to the root initially
    E.navigator_base_node = E.panel_root_node;


  uiInitialize();

  initComponentSystem();

  setlocale(LC_CTYPE, "");

  memset(&E.theme, 0, sizeof(E.theme));
  E.syntax_defs = NULL;
  E.num_syntax_defs = 0;
  loadSyntaxFiles(); // Load definitions from files
  loadTheme("cat_frappe"); // Load theme colours from file
  editorClearStatusMessage();


      // Register default components
    // Lower order numbers are drawn first (higher up for TOP, lower down for BOTTOM)
    uiRegisterComponent("tabline",    UI_POS_TOP,    1, editorDrawTabline,    true, 10);
    uiRegisterComponent("statusbar",  UI_POS_BOTTOM, 1, editorDrawStatusBar,  true, 10);
    uiRegisterComponent("messagebar", UI_POS_BOTTOM, 1, editorDrawMessageBar, true, 20); // Drawn below status bar
    // Register with a placeholder/NULL draw function, or keep editorDrawDirTreeFixed
    // but know it won't be called via the generic loop for drawing.
    // Using NULL makes the intent clearer. The 'name' field is important now.
    uiRegisterComponent("fixeddirtree", UI_POS_LEFT, 0, NULL, /* Initially disabled? */ true, 10);

    // Get total window size
    int total_terminal_rows;
    if (getWindowSize(&total_terminal_rows, &E.screencols) == -1) die("getWindowSize");
    E.total_rows = total_terminal_rows;
    // E.screenrows will be calculated dynamically in editorRefreshScreen
}

// To cleanup resources - is passed to atexit()
void cleanupEditor(void) {
    disableRawMode();
    free(E.project_root);
    freeComponentSystem();
    freeSyntaxDefs();
    freeThemeColors();
    uiFreeComponents();
    free(E); // ?
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
        debug_overlay_active = 1;
    //     user_dismissed_overlay = 0; // Ensure not dismissed
    //     // Shift argc/argv if you consume the argument
    // }

    enableRawMode();
    atexit(cleanupEditor);

    initEditor();
    // Create an empty buffer if no file is provided
    if (argc < 2) {
        // Create default empty buffer
        editorBuffer *buf = editorCreateBuffer();
        editorAddBuffer(buf);
        editorSwitchBuffer(buf);
    } else {
        // Open each file provided as an argument
        for (int i = 1; i < argc; i++) {
            editorBuffer *buf = editorOpen(argv[i], NULL);
            if (buf) {
                editorAddBuffer(buf);
                // Make the first file the active buffer
                if (i == 1) {
                    editorSwitchBuffer(buf);
                }
            }
        }
    }

    initLua();
    // initDebug();

    // Set initial status message
    if (debug_overlay_active) {
        editorSetStatusMessage("DEBUG OVERLAY - Press Ctrl-D to dismiss");
    } else {
        editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-N/P = next/prev buffer");
    }

    // Main loop
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0; // Unreachable
}