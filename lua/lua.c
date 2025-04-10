#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h> // For fprintf, stderr
#include <stdlib.h> // For getenv - needed for config path example
#include <string.h> // For strcat, strcpy - needed for config path example
#include <stdbool.h>
#include <unistd.h>   // For stat, access
#include <sys/stat.h> // For stat, S_ISDIR
#include <limits.h>   // For PATH_MAX

#include "k_lua.h"
#include "kilo.h"
#include "debug.h"
#include "dirtree.h"

// Forward declaration
static int c_lua_log_message(lua_State *L);
static int c_kilo_get_mode(lua_State *L);
static int c_kilo_get_filename(lua_State *L);
static int c_kilo_get_dirname(lua_State *L);
static int c_kilo_is_modified(lua_State *L);
static int c_kilo_get_cursor_line(lua_State *L);
static int c_kilo_get_cursor_col(lua_State *L);
static int c_kilo_set_statusbar_callback(lua_State *L);
static int c_kilo_get_scroll_percent(lua_State *L);
static int c_kilo_get_filetype_icon(lua_State *L);
static int c_kilo_get_total_lines(lua_State *L);
static int c_kilo_get_filetype_name(lua_State *L);
static int c_kilo_get_git_branch(lua_State *L);
static int c_kilo_lua_print(lua_State *L);
static int c_kilo_get_tabs(lua_State *L);
static int c_kilo_set_tabline_callback(lua_State *L);
static int c_kilo_open_file_buffer(lua_State *L);
static DirTreeNode* get_current_panel_node();
static int compareDirTreeNodes(const void *a, const void *b);

static int c_kilo_get_panel_contents(lua_State *L);
static int c_kilo_get_panel_state(lua_State *L);
static int c_kilo_panel_scroll(lua_State *L);
static int c_kilo_set_dirtree_callback(lua_State *L);
static int c_kilo_navigator_open(lua_State *L);
static int c_kilo_navigator_close(lua_State *L);
static int c_kilo_navigator_get_results(lua_State *L);
static int c_kilo_navigator_get_state(lua_State *L);
static int c_kilo_navigator_navigate(lua_State *L);
static int c_kilo_navigator_select(lua_State *L);
static int c_kilo_set_navigator_callback(lua_State *L);

static int c_kilo_get_screen_size(lua_State *L);
static int c_kilo_set_layout_callback(lua_State *L);
static int c_kilo_set_ui_element_callback(lua_State *L);
static int c_kilo_set_text_area_callback(lua_State *L);
static int c_kilo_get_text_area_content(lua_State *L);

// COMPONENT SYSTEM FUNCTIONS
static int c_kilo_create_component(lua_State *L);
static int c_kilo_set_panel_position(lua_State *L);
static int c_kilo_get_component_layout(lua_State *L);
static int c_kilo_set_component_visible(lua_State *L);
static int c_kilo_register_component(lua_State *L);


// Lua module definition
static const struct luaL_Reg kilo_lib[] = { // Renamed for clarity
    {"log_message", c_lua_log_message},
    {"get_mode", c_kilo_get_mode},
    {"get_filename", c_kilo_get_filename},
    {"get_dirname", c_kilo_get_dirname},
    {"is_modified", c_kilo_is_modified},
    {"get_cursor_line", c_kilo_get_cursor_line},
    {"get_cursor_col", c_kilo_get_cursor_col},
    {"register_statusbar_config", c_kilo_set_statusbar_callback},
    {"get_scroll_percent", c_kilo_get_scroll_percent},
    {"get_filetype_icon", c_kilo_get_filetype_icon},
    {"get_total_lines", c_kilo_get_total_lines},
    {"get_filetype_name", c_kilo_get_filetype_name},
    {"get_git_branch", c_kilo_get_git_branch},
    // {"debug", c_kilo_lua_print},
    {"get_tabs", c_kilo_get_tabs},
    {"register_tabline_config", c_kilo_set_tabline_callback},

    {"open_file_buffer", c_kilo_open_file_buffer},
    {"get_panel_contents", c_kilo_get_panel_contents},
    {"get_panel_state", c_kilo_get_panel_state},
    {"panel_scroll", c_kilo_panel_scroll},
    {"register_dirtree_config", c_kilo_set_dirtree_callback},

    {"navigator_open", c_kilo_navigator_open},
    {"navigator_close", c_kilo_navigator_close},
    {"navigator_get_results", c_kilo_navigator_get_results},
    {"get_navigator_state", c_kilo_navigator_get_state},
    {"navigator_navigate", c_kilo_navigator_navigate},
    {"navigator_select", c_kilo_navigator_select},
    {"register_navigator_config", c_kilo_set_navigator_callback},

    {"get_screen_size", c_kilo_get_screen_size},
    {"register_layout_config", c_kilo_set_layout_callback},
    {"register_ui_element", c_kilo_set_ui_element_callback},
    {"get_text_area_content", c_kilo_get_text_area_content},
    {"register_text_area_config", c_kilo_set_text_area_callback},

    {"create_component", c_kilo_create_component},
    {"set_panel_position", c_kilo_set_panel_position},
    {"get_component_layout", c_kilo_get_component_layout},
    {"set_component_visible", c_kilo_set_component_visible},
    {"register_component", c_kilo_register_component},

    {NULL, NULL} /* Sentinel */
};

static lua_State *L = NULL;

int statusbar_callback_ref = LUA_NOREF; // Initialize reference holder
int tabline_callback_ref = LUA_NOREF;
int dirtree_callback_ref = LUA_NOREF;
int navigator_callback_ref = LUA_NOREF;
int layout_callback_ref = LUA_NOREF;
int textarea_callback_ref = LUA_NOREF;


lua_State* getLuaState(void) {
    return L;
}

int initLua() {
    L = luaL_newstate();
    if (L == NULL) {
        // Cannot use debug_printf here reliably yet
        fprintf(stderr, "[Kilo C Error] Could not create Lua state\n");
        return -1;
    }

    luaL_openlibs(L); // Load standard Lua libraries

    // --- Replace standard Lua print function ---
    lua_pushcfunction(L, c_kilo_lua_print);
    lua_setglobal(L, "print");
    // --- End replacement ---

    // Register C library module ("kilo")
    luaL_newlib(L, kilo_lib); // Create table, register functions from kilo_lib array
    lua_setglobal(L, "kilo"); // Make the table available globally as "kilo"
    printf("[Kilo C] 'kilo' module registered.\n");

    // --- Set up Neovim-style module paths ---
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path"); // Get current package.path
    const char *current_path = lua_tostring(L, -1); // Note: Handles NULL if path isn't string

    char new_path[4096];
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char config_dir[1024];
    int config_dir_set = 0; // Flag to track if we found a valid config dir

    // Determine config directory path
    if (config_home && config_home[0] != '\0') {
        snprintf(config_dir, sizeof(config_dir), "%s/kilo", config_home);
        config_dir_set = 1;
    } else if (home && home[0] != '\0') {
        snprintf(config_dir, sizeof(config_dir), "%s/.config/kilo", home);
        config_dir_set = 1;
    } else {
        fprintf(stderr, "[Kilo C Warning] Could not determine config directory (XDG_CONFIG_HOME or HOME not set/empty).\n");
        // Do not attempt to load Lua config if directory is unknown
    }

    if(config_dir_set) {
        // Create the enhanced path string only if config_dir is valid
        if (current_path == NULL) current_path = ""; // Default to empty string if package.path was nil
        snprintf(new_path, sizeof(new_path),
                "%s;%s/?.lua;%s/?/init.lua;%s/lua/?.lua;%s/lua/?/init.lua",
                current_path, config_dir, config_dir, config_dir, config_dir);

        // Set the new package.path
        lua_pushstring(L, new_path);
        lua_setfield(L, -3, "path"); // Set field 'path' in table at index -3 (package table)
        printf("[Kilo C] Lua package.path configured: %s\n", new_path);
    } else {
         printf("[Kilo C] Lua package.path not modified (no config directory found).\n");
    }

    lua_pop(L, 2); // Pop package table and old path string (or nil)
    // --- End path setup ---


    // --- Attempt to load and run user's init script ---
    if (!config_dir_set) {
        printf("[Kilo C] Skipping Lua init script load (config directory unknown).\n");
        goto skip_lua_load; // Skip if we couldn't determine where to load from
    }

    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/init.lua", config_dir);

    printf("[Kilo C] Attempting to load Lua init script: %s\n", config_path);
    fflush(stdout); // Ensure this message prints before potential errors

    int result = luaL_dofile(L, config_path); // Load and execute the init script

    if (result != LUA_OK) {
        // Error occurred during loading or execution of init.lua
        const char *errorMsg = lua_tostring(L, -1); // Get error from Lua stack top
        if (errorMsg == NULL) {
             errorMsg = "Unknown Lua Error (error object not a string?)";
        }
        // Log error to both stderr (for immediate visibility) and debug overlay
        fprintf(stderr, "[Kilo C Error] Failed to run init script '%s': %s\n", config_path, errorMsg);
        debug_printf("[LUA ERROR] Failed init ('%s'): %s\n", config_path, errorMsg);

        lua_pop(L, 1); // Remove error object from Lua stack
        // Decide how to proceed - maybe disable Lua features? For now, continue.
    } else {
        // init.lua executed successfully
        printf("[Kilo C] Lua init script executed successfully.\n");
        fflush(stdout);
        debug_printf("[LUA] init.lua executed successfully.\n"); // Log success to overlay
    }

    if (luaL_dofile(L, "test.lua") != LUA_OK) {
          // Handle error, maybe push error message to status bar
          const char *err = lua_tostring(L, -1);
          debug_printf("Lua test script error: %s", err ? err : "unknown");
          lua_pop(L, 1); // Pop error message from stack
     } else {
           debug_printf("Lua test script executed.");
     }

skip_lua_load: // Label used if config dir determination failed

    // Keep Lua state L alive for later use (e.g., statusbar callback)
    // lua_close(L) should happen on editor exit

    return 0; // Indicate Lua initialization sequence finished (even if script failed)
}


// Lua functions
static int c_kilo_set_text_area_callback(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  
  if (textarea_callback_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, textarea_callback_ref);
  }
  
  lua_pushvalue(L, 1);
  textarea_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  
  return 0;
}


/**
 * Lua API function: kilo.get_screen_size()
 * Returns the total screen dimensions.
 */
static int c_kilo_get_screen_size(lua_State *L) {
    lua_newtable(L);
    
    lua_pushinteger(L, E.total_rows);
    lua_setfield(L, -2, "height");
    
    lua_pushinteger(L, E.screencols);
    lua_setfield(L, -2, "width");
    
    return 1;
}

/**
 * Lua API function: kilo.register_layout_callback(callback_function)
 * Registers the function that calculates the overall UI layout.
 */
static int c_kilo_set_layout_callback(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    
    // If we already have a callback registered, unreference it first
    if (layout_callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, layout_callback_ref);
    }
    
    // Push a copy of the function to the top of the stack
    lua_pushvalue(L, 1);
    
    // Create a reference to the function in the registry
    layout_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    
    return 0;
}

/**
 * Lua API function: kilo.register_ui_element(name, callback_function)
 * Registers a UI element with a callback function.
 */
static int c_kilo_set_ui_element_callback(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    
    // Store callback reference in a table
    // First, ensure we have a global ui_elements table
    lua_getglobal(L, "kilo");
    lua_getfield(L, -1, "ui_elements");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); // Pop nil
        lua_newtable(L); // Create new table
        lua_pushvalue(L, -1); // Duplicate it on stack
        lua_setfield(L, -3, "ui_elements"); // kilo.ui_elements = table
    }
    
    // Now store the callback
    lua_pushvalue(L, 2); // Copy callback function to top
    lua_setfield(L, -2, name); // ui_elements[name] = callback
    
    lua_pop(L, 2); // Pop ui_elements table and kilo table
    
    return 0;
}

/**
 * Lua API function: kilo.get_text_area_content()
 * Returns the content visible in the text area for rendering.
 */
static int c_kilo_get_text_area_content(lua_State *L) {
    lua_newtable(L); // Main content table
    
    // Add current buffer information
    lua_pushstring(L, E.current_buffer && E.current_buffer->filename ? 
                  E.current_buffer->filename : "[No Name]");
    lua_setfield(L, -2, "filename");
    
    lua_pushinteger(L, E.numrows);
    lua_setfield(L, -2, "total_rows");
    
    lua_pushinteger(L, E.rowoff);
    lua_setfield(L, -2, "row_offset");
    
    lua_pushinteger(L, E.coloff);
    lua_setfield(L, -2, "col_offset");
    
    // Create rows array
    lua_newtable(L);
    int rows_to_return = E.numrows - E.rowoff;
    if (rows_to_return > E.screenrows) rows_to_return = E.screenrows;
    if (rows_to_return < 0) rows_to_return = 0;
    
    for (int i = 0; i < rows_to_return; i++) {
        int filerow = i + E.rowoff;
        if (filerow < E.numrows) {
            lua_newtable(L);
            
            erow *row = &E.row[filerow];
            lua_pushstring(L, row->render);
            lua_setfield(L, -2, "text");
            
            lua_pushinteger(L, filerow + 1); // 1-based for Lua
            lua_setfield(L, -2, "line_number");
            
            // Optional: Add highlighting information
            // Would need a new function to convert hl array to color names
            
            lua_rawseti(L, -2, i + 1);
        }
    }
    lua_setfield(L, -2, "rows");
    
    return 1;
}


/**
 * @brief Lua binding to get a list of open tabs/buffers.
 * Iterates through the editor's internal buffer list (assuming a linked list
 * via E.buffer_list_head and E.current_buffer) and returns a Lua list table.
 * Each element in the Lua list is a table representing a tab, containing:
 * - filename (string): Base name of the file.
 * - is_active (boolean): True if this is the current buffer.
 * - is_mod (boolean): True if the buffer is modified (dirty).
 * @param L Lua state.
 * @return int Number of return values (1: the list table, or nil).
 */
static int c_kilo_get_tabs(lua_State *L) {
    // Check if the necessary buffer management fields exist in E
    if (!E.buffer_list_head || !E.current_buffer) {
        // Return nil or an empty table if multi-buffer isn't initialized
        // lua_pushnil(L);
        lua_newtable(L); // Return empty table might be friendlier for Lua loops
        return 1;
    }

    // 1. Create the main Lua table that will act as a list
    lua_newtable(L);
    int list_table_idx = lua_gettop(L); // Get stack index of this list table
    int list_lua_idx = 0;               // 1-based index for the Lua list

    // 2. Iterate through the linked list of editor buffers
    editorBuffer *buffer_node = E.buffer_list_head;
    while (buffer_node != NULL) {
        // 3. Create a new Lua table for this specific tab/buffer
        lua_newtable(L);
        int tab_table_idx = lua_gettop(L); // Stack index of the tab table

        // 4a. Set 'filename' field
        // Use the findBasename helper to get just the file part
        const char *display_name = findBasename(buffer_node->filename);
        if (display_name) {
            lua_pushstring(L, display_name);
        } else {
            lua_pushstring(L, "[No Name]"); // Fallback if filename is NULL
        }
        // lua_setfield(table_idx, key_name) - sets field in table at specified index
        lua_setfield(L, tab_table_idx, "filename");

        // 4b. Set 'is_active' field
        lua_pushboolean(L, (buffer_node == E.current_buffer)); // Compare pointers
        lua_setfield(L, tab_table_idx, "is_active");

        // 4c. Set 'is_mod' field (assuming 'dirty' field in editorBuffer)
        lua_pushboolean(L, buffer_node->dirty);
        lua_setfield(L, tab_table_idx, "is_mod");

        // --- OPTIONAL: Add more fields if needed ---
        // lua_pushstring(L, buffer_node->filename ? buffer_node->filename : "");
        // lua_setfield(L, tab_table_idx, "full_path");
        // if (buffer_node->syntax && buffer_node->syntax->filetype) {
        //     lua_pushstring(L, buffer_node->syntax->filetype);
        // } else {
        //     lua_pushnil(L);
        // }
        // lua_setfield(L, tab_table_idx, "filetype");
        // -------------------------------------------

        // 5. Add the completed tab table to the main list table
        // lua_rawseti(list_table_idx, list_lua_idx, value) - sets list[idx] = value (pops value)
        lua_rawseti(L, list_table_idx, ++list_lua_idx);

        // Move to the next buffer in the linked list
        buffer_node = buffer_node->next;
    }

    // The main list table (at list_table_idx) is now at the top of the stack
    return 1; // Return 1 value (the list table)
}



static int c_kilo_lua_print(lua_State *L) {
    int n = lua_gettop(L);
    char buffer[1024] = "[LUA] ";
    size_t len = 6; // Length of "[LUA] "
    
    for (int i = 1; i <= n; i++) {
        const char *s = lua_tostring(L, i);
        if (s == NULL) s = "(nil)";
        
        // Add to buffer with separator
        if (i > 1) {
            buffer[len++] = '\t';
            buffer[len] = '\0';
        }
        
        // Append string, ensuring we don't overflow
        strncat(buffer, s, sizeof(buffer) - len - 1);
        len = strlen(buffer);
    }
    
    // Call fprintf directly instead of debug_printf
    printf("%s\n", buffer);
    
    return 0;
}


static int c_lua_log_message(lua_State *L) {
    const char *message = luaL_checkstring(L, 1); // Get 1st argument
    // Assuming editorSetStatusMessage is globally accessible or passed in
    editorSetStatusMessage("[init.lua]: %s", message);
    return 0; // No return values
}

static int c_kilo_get_mode(lua_State *L) {
    // Assuming E.mode holds MODE_NORMAL, MODE_INSERT etc.
    // You might want to convert the enum/int to a string for Lua
    const char *mode_str = "UNKNOWN";
    switch (E.mode) {
        case MODE_NORMAL: mode_str = "NORMAL"; break;
        case MODE_INSERT: mode_str = "INSERT"; break;
        // Add other modes if you have them
    }
    lua_pushstring(L, mode_str);
    return 1; // Return 1 value (the string)
}

// directory functions
static int c_kilo_get_filename(lua_State *L) {
    const char *full_filename = NULL;

    // Get filename directly from the current buffer struct
    if (E.current_buffer) {
        full_filename = E.current_buffer->filename;
    }

    if (full_filename) {
        // Extract the basename part for display
        const char *basename = findBasename(full_filename);
        lua_pushstring(L, basename);
    } else {
        lua_pushnil(L); // Return nil if no current buffer or no filename
    }
    return 1;
}


static int c_kilo_get_dirname(lua_State *L) {
    if (E.current_buffer && E.current_buffer->dirname) {
        lua_pushstring(L, E.current_buffer->dirname);
    } else {
        lua_pushnil(L); // Return nil if no dirname
    }
    return 1;
}

static int c_kilo_is_modified(lua_State *L) {
    lua_pushboolean(L, E.dirty); // E.dirty is likely 0 or 1 already
    return 1;
}


static int c_kilo_get_cursor_line(lua_State *L) {
    lua_pushinteger(L, E.cy + 1); // Lua usually uses 1-based indexing
    return 1;
}


static int c_kilo_get_cursor_col(lua_State *L) {
    lua_pushinteger(L, E.rx + 1); // Use render column (rx), 1-based
    return 1;
}
// ... add more getters for E.numrows, E.rowoff etc. as needed ...


// customisation functions
static int c_kilo_set_statusbar_callback(lua_State *L) {
    // 1. Check if the first argument is a function
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // 2. If we already have a callback registered, unreference it first
    if (statusbar_callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, statusbar_callback_ref);
    }

    // 3. luaL_ref expects the value at the top of the stack.
    //    The function argument is at index 1. Push a copy to the top.
    lua_pushvalue(L, 1);

    // 4. Create a reference to the function (now on top) in the registry
    //    This also pops the value from the stack.
    statusbar_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0; // No return values to Lua
}


// customisation functions
static int c_kilo_set_tabline_callback(lua_State *L) {
    // 1. Check if the first argument is a function
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // 2. If we already have a callback registered, unreference it first
    if (tabline_callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, tabline_callback_ref);
    }

    // 3. luaL_ref expects the value at the top of the stack.
    //    The function argument is at index 1. Push a copy to the top.
    lua_pushvalue(L, 1);

    // 4. Create a reference to the function (now on top) in the registry
    //    This also pops the value from the stack.
    tabline_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0; // No return values to Lua
}

// C function callable from Lua: kilo.register_dirtree_config(draw_function)
static int c_kilo_set_dirtree_callback(lua_State *L) {
    // 1. Check if the first argument is a function
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // 2. If we already have a callback registered, unreference it first
    if (dirtree_callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, dirtree_callback_ref);
    }

    // 3. luaL_ref expects the value at the top of the stack.
    //    The function argument is at index 1. Push a copy to the top.
    lua_pushvalue(L, 1);

    // 4. Create a reference to the function (now on top) in the registry
    //    This also pops the value from the stack.
    dirtree_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0; // No return values to Lua
}

static int c_kilo_set_navigator_callback(lua_State *L) {
    // 1. Check if the first argument is a function
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // 2. If we already have a callback registered, unreference it first
    if (navigator_callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, navigator_callback_ref);
    }

    // 3. luaL_ref expects the value at the top of the stack.
    //    The function argument is at index 1. Push a copy to the top.
    lua_pushvalue(L, 1);

    // 4. Create a reference to the function (now on top) in the registry
    //    This also pops the value from the stack.
    navigator_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0; // No return values to Lua
}


static int c_kilo_get_scroll_percent(lua_State *L) {
    int percentage = (E.numrows > 0) ? ((E.cy + 1) * 100 / E.numrows) : 100;
    lua_pushinteger(L, percentage);
    return 1;
}

static int c_kilo_get_filetype_icon(lua_State *L) {
    char lang_icon[10] = "\xEE\x81\x99"; // Default file icon U+E159
    
    if (E.syntax && E.syntax->status_icon) {
        if (strncmp(E.syntax->status_icon, "\\u", 2) == 0 && strlen(E.syntax->status_icon) >= 6) {
            unsigned int code;
            if (sscanf(E.syntax->status_icon + 2, "%x", &code) == 1) {
                // Simple UTF-8 encoding (assuming valid Unicode code points)
                if (code <= 0x7F) {
                    lang_icon[0] = (char)code; lang_icon[1] = '\0';
                } else if (code <= 0x7FF) {
                    lang_icon[0] = 0xC0 | (code >> 6); lang_icon[1] = 0x80 | (code & 0x3F); lang_icon[2] = '\0';
                } else if (code <= 0xFFFF) {
                    lang_icon[0] = 0xE0 | (code >> 12); lang_icon[1] = 0x80 | ((code >> 6) & 0x3F); lang_icon[2] = 0x80 | (code & 0x3F); lang_icon[3] = '\0';
                } else if (code <= 0x10FFFF) {
                    lang_icon[0] = 0xF0 | (code >> 18); lang_icon[1] = 0x80 | ((code >> 12) & 0x3F); lang_icon[2] = 0x80 | ((code >> 6) & 0x3F); lang_icon[3] = 0x80 | (code & 0x3F); lang_icon[4] = '\0';
                } else {
                    // Invalid code point, use default
                    strcpy(lang_icon, "\xEF\xBF\xBD"); // U+FFFD REPLACEMENT CHARACTER
                }
            }
        } else {
            strncpy(lang_icon, E.syntax->status_icon, sizeof(lang_icon) - 1);
            lang_icon[sizeof(lang_icon) - 1] = '\0';
        }
        
        lua_pushstring(L, lang_icon);
    } else {
        lua_pushstring(L, lang_icon);
    }
    
    return 1;
}


static int c_kilo_get_total_lines(lua_State *L) {
    lua_pushinteger(L, E.numrows); // Assuming E.numrows holds the total line count
    return 1;
}

static int c_kilo_get_filetype_name(lua_State *L) {
    if (E.syntax && E.syntax->filetype) {
        lua_pushstring(L, E.syntax->filetype);
    } else {
        lua_pushnil(L); // Return nil if no syntax or filetype name defined
    }
    return 1;
}



// Shared Navigator and Panel functions
// Lua: kilo.open_file_buffer(filepath_string)
static int c_kilo_open_file_buffer(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    editorBuffer *buf = NULL;

    // --- Find existing buffer first ---
    editorBuffer *existing = E.buffer_list_head;
    while (existing) {
        if (existing->filename && strcmp(existing->filename, path) == 0) {
            buf = existing;
            break;
        }
        existing = existing->next;
    }

    if (buf) {
        // Buffer already exists, just switch to it
        editorSwitchBuffer(buf);
    } else {
        // Buffer doesn't exist, create it
        // Try to find the corresponding DirTreeNode to link
        DirTreeNode *node = findOrCreateDirTreeNode(path); // Use the helper

        // editorOpen needs non-const char*. Casting is generally okay if
        // editorOpen uses strdup internally, which yours does.
        buf = editorOpen((char*)path, node);

        if (buf) {
            editorAddBuffer(buf);
            editorSwitchBuffer(buf);
        } else {
            // editorOpen failed (shouldn't happen unless strdup fails / file read issue?)
            editorSetStatusMessage("Error opening file: %s", path); // Inform user
            lua_pushboolean(L, 0); // Indicate failure to Lua
            return 1;
        }
    }

    // If we reached here, we successfully opened or switched
    E.navigator_active = false; // Close navigator if it was open
    lua_pushboolean(L, 1);      // Indicate success to Lua
    return 1;
}


// --- Panel API ---

static DirTreeNode* get_current_panel_node() {
    // 1. Ensure Root Node Exists
    if (!E.panel_root_node) {
        E.panel_root_node = createTreeNode(E.project_root, NULL, NODE_DIR);
        if (!E.panel_root_node) {
             fprintf(stderr, "Error: Cannot create panel root node for %s\n", E.project_root);
             return NULL; // Cannot proceed
        }
        // Maybe expand it immediately?
        // expandDirNode(E.panel_root_node);
    }

    // 2. Get Current Buffer's Directory Path
    const char *target_dir_path = NULL;
    if (E.current_buffer && E.current_buffer->dirname) {
        target_dir_path = E.current_buffer->dirname;
    } else {
        // No current buffer or dirname, default to project root
        target_dir_path = E.project_root;
    }

    // 3. Find/Create the Node for the Target Path
    DirTreeNode* target_node = findOrCreateDirTreeNode(target_dir_path);

    // 4. Update Panel State and Return
    if (target_node) {
         E.panel_current_dir_node = target_node;
    } else {
         // Couldn't find/create node for buffer's dir, fallback to root
         fprintf(stderr, "Warning: Could not find/create node for %s, using root.\n", target_dir_path);
         E.panel_current_dir_node = E.panel_root_node;
    }

    // Reset view offset if the directory node changed? Maybe.
    // static DirTreeNode* last_node = NULL;
    // if (last_node != E.panel_current_dir_node) {
    //     E.panel_view_offset = 0;
    //     E.panel_selected_index = -1; // Reset selection too
    //     last_node = E.panel_current_dir_node;
    // }

    return E.panel_current_dir_node;
}

// Comparison function for qsort (DirTreeNode**)
static int compareDirTreeNodes(const void *a, const void *b) {
    DirTreeNode *nodeA = *(DirTreeNode**)a;
    DirTreeNode *nodeB = *(DirTreeNode**)b;

    // Optional: Directories first
    if (nodeA->type != nodeB->type) {
        return (nodeA->type == NODE_DIR) ? -1 : 1; // Directories before files
    }

    // Sort by name
    return strcmp(nodeA->name, nodeB->name);
}



// Lua: kilo.get_panel_contents() -> { {name="...", type="dir/file"}, ... }
static int c_kilo_get_panel_contents(lua_State *L) {
    DirTreeNode *node = get_current_panel_node();
    if (!node) {
        lua_newtable(L);
        return 1;
    }

    if (!node->is_expanded) {
        if (expandDirNode(node) != 0) {
            lua_newtable(L);
            return 1;
        }
        // // Sort *after* expanding, only if expansion happened
        // if (node->num_children > 0) {
        //      qsort(node->children, node->num_children, sizeof(DirTreeNode*), compareDirTreeNodes);
        // }
    }
    // Note: If you want sorting every time, move qsort outside the if(!is_expanded) block
    // Sort *after* expanding, only if expansion happened
        if (node->num_children > 0) {
             qsort(node->children, node->num_children, sizeof(DirTreeNode*), compareDirTreeNodes);
        }

    lua_newtable(L); // Create the main list table
    for (size_t i = 0; i < node->num_children; i++) {
        DirTreeNode *child = node->children[i];
        lua_newtable(L);

        lua_pushstring(L, child->name);
        lua_setfield(L, -2, "name");

        lua_pushstring(L, (child->type == NODE_DIR ? "dir" : "file"));
        lua_setfield(L, -2, "type");

        // Also push full_path for potential use in Lua (e.g., selection)
        if (child->full_path) {
            lua_pushstring(L, child->full_path);
            lua_setfield(L, -2, "full_path");
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


// Lua: kilo.get_panel_state() -> { view_offset=..., current_path=..., selected_index=... }
static int c_kilo_get_panel_state(lua_State *L) {
    DirTreeNode *node = get_current_panel_node(); // Use the helper
    lua_newtable(L); // Create state table

    // View Offset
    lua_pushinteger(L, E.panel_view_offset);
    lua_setfield(L, -2, "view_offset");

    // Selection Index (-1 means no selection, Lua might use 1-based index or handle -1)
    lua_pushinteger(L, E.panel_selected_index);
    lua_setfield(L, -2, "selected_index");

    // Current Path (using full_path if available)
    const char *path_to_push = NULL;
    if (node && node->full_path) {
        path_to_push = node->full_path;
    } else if (node && node->name) { // Fallback for root/error?
        path_to_push = node->name;
    }
     if(path_to_push){
        lua_pushstring(L, path_to_push);
        lua_setfield(L, -2, "current_path");
     }

    // Add currently open buffer's node info (for highlighting)
    if (E.current_buffer && E.current_buffer->tree_node && E.current_buffer->tree_node->full_path) {
         lua_pushstring(L, E.current_buffer->tree_node->full_path);
         lua_setfield(L, -2, "active_buffer_path");
    } // Lua can compare active_buffer_path with panel entry paths

    return 1; // Return the state table
}


// Lua: kilo.panel_scroll(direction_string)
static int c_kilo_panel_scroll(lua_State *L) {
    const char *direction = luaL_checkstring(L, 1); // Get direction arg
    DirTreeNode *node = get_current_panel_node();
    if (!node || node->num_children == 0) return 0; // No scrolling possible

    int scroll_amount = 1; // Or page amount for pageup/down

    if (strcmp(direction, "up") == 0) {
        E.panel_view_offset -= scroll_amount;
    } else if (strcmp(direction, "down") == 0) {
        E.panel_view_offset += scroll_amount;
    } // Add pageup/pagedown if needed

    // Clamp view offset
    if (E.panel_view_offset < 0) E.panel_view_offset = 0;
    // Ensure offset doesn't go too far (e.g., keep at least one item visible)
    if (E.panel_view_offset >= (int)node->num_children) {
         E.panel_view_offset = node->num_children > 0 ? node->num_children - 1 : 0;
         // Adjust clamping based on desired behaviour and panel height (known only to Lua?)
    }

    return 0; // No return value
}


// --- Navigator API ---

// Lua: kilo.navigator_open()
static int c_kilo_navigator_open(lua_State *L) {
    E.navigator_active = true;
    // Set base node, e.g., to current buffer's dir or project root
    if (E.current_buffer && E.current_buffer->dirname) {
         // Find/create node for dirname. Handle errors.
         E.navigator_base_node = findOrCreateDirTreeNode(E.current_buffer->dirname);
    } else {
         // Find/create node for project root. Handle errors.
         E.navigator_base_node = findOrCreateDirTreeNode(E.project_root);
    }

    if (E.navigator_base_node && !E.navigator_base_node->is_expanded) {
        expandDirNode(E.navigator_base_node); // Expand initial view
        // TODO: Sort children?
    }

    E.navigator_selected_index = 0;
    E.navigator_view_offset = 0;
    // Clear search query if you add one: E.navigator_search_query = "";
    return 0;
}

// Lua: kilo.navigator_close()
static int c_kilo_navigator_close(lua_State *L) {
    E.navigator_active = false;
    // Optional: free search results if they are dynamically allocated
    return 0;
}

// Lua: kilo.navigator_get_results(query_or_nil) -> { {name=..., type=...}, ... }
static int c_kilo_navigator_get_results(lua_State *L) {
    // const char *query = lua_isnoneornil(L, 1) ? NULL : luaL_checkstring(L, 1);
    // TODO: Implement filtering based on query if needed.

    DirTreeNode *node = E.navigator_base_node;
    if (!E.navigator_active || !node) {
        lua_newtable(L); // Return empty table if inactive or no base node
        return 1;
    }

    // Ensure node children are loaded (might have changed via select)
    if (!node->is_expanded) {
        if (expandDirNode(node) != 0) {
             lua_newtable(L);
             return 1;
        }
         // Sort?
    }

    lua_newtable(L); // Create results table
    // Simple version: return all children, filtering done in Lua?
    // Or filter here based on 'query'
    int result_index = 1;
    for (size_t i = 0; i < node->num_children; i++) {
        DirTreeNode *child = node->children[i];
        // if (query == NULL || strstr(child->name, query) != NULL) { // Simple filter
            lua_newtable(L);
            lua_pushstring(L, child->name);
            lua_setfield(L, -2, "name");
            lua_pushstring(L, (child->type == NODE_DIR ? "dir" : "file"));
            lua_setfield(L, -2, "type");
            lua_rawseti(L, -2, result_index++); // Add to results table
        // }
    }
    return 1; // Return results table
}

// Lua: kilo.get_navigator_state() -> { ..., selected_index=..., view_offset=... }
static int c_kilo_navigator_get_state(lua_State *L) {
     if (!E.navigator_active || !E.navigator_base_node) {
         lua_pushnil(L); // Indicate inactive/invalid state
         return 1;
     }
     lua_newtable(L); // Create state table

     lua_pushinteger(L, E.navigator_selected_index);
     lua_setfield(L, -2, "selected_index");

     lua_pushinteger(L, E.navigator_view_offset);
     lua_setfield(L, -2, "view_offset");

     // TODO: Calculate actual number of results if filtering is done in C
     lua_pushinteger(L, E.navigator_base_node->num_children); // Placeholder count
     lua_setfield(L, -2, "entry_count");

     if (E.navigator_base_node->full_path) {
         lua_pushstring(L, E.navigator_base_node->full_path);
         lua_setfield(L, -2, "current_path");
     }
     // Add query if implemented

     return 1;
}

// Lua: kilo.navigator_navigate(direction_string)
static int c_kilo_navigator_navigate(lua_State *L) {
    if (!E.navigator_active || !E.navigator_base_node) return 0;
    const char *direction = luaL_checkstring(L, 1);
    int result_count = E.navigator_base_node->num_children; // Adjust if filtering
    if (result_count == 0) return 0;

    int scroll_amount = 1; // Add pageup/down logic

    if (strcmp(direction, "up") == 0) {
        E.navigator_selected_index--;
    } else if (strcmp(direction, "down") == 0) {
        E.navigator_selected_index++;
    }

    // Clamp selection
    if (E.navigator_selected_index < 0) E.navigator_selected_index = 0;
    if (E.navigator_selected_index >= result_count) E.navigator_selected_index = result_count - 1;

    // TODO: Adjust E.navigator_view_offset based on selection and overlay height (from Lua?)

    return 0;
}


// Lua: kilo.navigator_select()
static int c_kilo_navigator_select(lua_State *L) {
     if (!E.navigator_active || !E.navigator_base_node ||
         E.navigator_selected_index < 0 ||
         E.navigator_selected_index >= (int)E.navigator_base_node->num_children) {
         return 0; // No valid selection
     }

     DirTreeNode *selected_node = E.navigator_base_node->children[E.navigator_selected_index];

     if (selected_node->type == NODE_FILE) {
         // Need full path
         if (selected_node->full_path) {
            // Call existing editorOpen/Add/Switch logic
            editorBuffer *buf = editorOpen(selected_node->full_path, selected_node);
            if (buf) {
                editorAddBuffer(buf);
                editorSwitchBuffer(buf);
                E.navigator_active = false; // Close navigator on file open
            } else {
                editorSetStatusMessage("Error opening file: %s", selected_node->full_path);
            }
         } else {
              editorSetStatusMessage("Error: Selected node has no full path!");
         }
     } else { // NODE_DIR
         if (strcmp(selected_node->name, "..") == 0) {
             if (E.navigator_base_node->parent) {
                 E.navigator_base_node = E.navigator_base_node->parent;
             } // Else: already at root, do nothing?
         } else {
             E.navigator_base_node = selected_node; // Navigate into selected directory
         }
         // Expand the new directory if needed
         if (!E.navigator_base_node->is_expanded) {
             expandDirNode(E.navigator_base_node);
             // Sort?
         }
         // Reset state for the new directory view
         E.navigator_selected_index = 0;
         E.navigator_view_offset = 0;
         // Clear search query?
     }
     return 0;
}

// --- Lua API for Component System ---

// Register a component callback
static int c_kilo_register_component(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    
    ComponentLayout *component = findComponent(name);
    if (!component) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    // If we already have a callback registered, unreference it first
    if (component->lua_callback_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, component->lua_callback_ref);
    }
    
    // Push a copy of the function to the top of the stack
    lua_pushvalue(L, 2);
    
    // Create a reference to the function in the registry
    component->lua_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    
    lua_pushboolean(L, 1);
    return 1;
}

// Set component visibility
static int c_kilo_set_component_visible(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    bool visible = lua_toboolean(L, 2);
    
    ComponentLayout *component = findComponent(name);
    if (!component) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    component->visible = visible;
    
    lua_pushboolean(L, 1);
    return 1;
}

// Get current component layout
static int c_kilo_get_component_layout(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    
    ComponentLayout *component = findComponent(name);
    if (!component) {
        lua_pushnil(L);
        return 1;
    }
    
    lua_newtable(L);
    
    lua_pushinteger(L, component->x);
    lua_setfield(L, -2, "x");
    
    lua_pushinteger(L, component->y);
    lua_setfield(L, -2, "y");
    
    lua_pushinteger(L, component->width);
    lua_setfield(L, -2, "width");
    
    lua_pushinteger(L, component->height);
    lua_setfield(L, -2, "height");
    
    lua_pushboolean(L, component->visible);
    lua_setfield(L, -2, "visible");
    
    // Add position as string for convenience
    const char *position_str = "unknown";
    switch (component->position) {
        case POSITION_TOP: position_str = "top"; break;
        case POSITION_BOTTOM: position_str = "bottom"; break;
        case POSITION_LEFT: position_str = "left"; break;
        case POSITION_RIGHT: position_str = "right"; break;
        case POSITION_CENTER: position_str = "center"; break;
        case POSITION_FLOATING: position_str = "floating"; break;
    }
    lua_pushstring(L, position_str);
    lua_setfield(L, -2, "position");
    
    return 1;
}

// Set panel position
static int c_kilo_set_panel_position(lua_State *L) {
    const char *position_str = luaL_checkstring(L, 1);
    
    ComponentLayout *panel = component_system.panel;
    if (!panel) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    if (strcmp(position_str, "left") == 0) {
        panel->position = POSITION_LEFT;
        panel->x = 1;
    } else if (strcmp(position_str, "right") == 0) {
        panel->position = POSITION_RIGHT;
    } else if (strcmp(position_str, "float") == 0) {
        panel->position = POSITION_FLOATING;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    // Make panel visible
    panel->visible = true;
    
    lua_pushboolean(L, 1);
    return 1;
}

// Create a new custom component
static int c_kilo_create_component(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    // Check if component already exists
    if (findComponent(name)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    // Get position
    lua_getfield(L, 2, "position");
    const char *position_str = lua_isstring(L, -1) ? lua_tostring(L, -1) : "floating";
    lua_pop(L, 1);
    
    ComponentPosition position = POSITION_FLOATING;
    if (strcmp(position_str, "top") == 0) {
        position = POSITION_TOP;
    } else if (strcmp(position_str, "bottom") == 0) {
        position = POSITION_BOTTOM;
    } else if (strcmp(position_str, "left") == 0) {
        position = POSITION_LEFT;
    } else if (strcmp(position_str, "right") == 0) {
        position = POSITION_RIGHT;
    } else if (strcmp(position_str, "center") == 0) {
        position = POSITION_CENTER;
    }
    
    // Create the component
    ComponentLayout *component = registerComponent(name, COMPONENT_CUSTOM, position);
    if (!component) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    // Get dimensions if provided
    lua_getfield(L, 2, "x");
    if (lua_isnumber(L, -1)) component->x = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 2, "y");
    if (lua_isnumber(L, -1)) component->y = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 2, "width");
    if (lua_isnumber(L, -1)) component->width = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 2, "height");
    if (lua_isnumber(L, -1)) component->height = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // Get z-index if provided
    lua_getfield(L, 2, "z_index");
    if (lua_isnumber(L, -1)) component->z_index = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // Check for initial visibility
    lua_getfield(L, 2, "visible");
    if (lua_isboolean(L, -1)) component->visible = lua_toboolean(L, -1);
    lua_pop(L, 1);
    
    // Get callback function if provided
    lua_getfield(L, 2, "callback");
    if (lua_isfunction(L, -1)) {
        // Create a reference to the function
        component->lua_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    
    lua_pushboolean(L, 1);
    return 1;
}



// --- Optional: Git Branch ---
// This is more complex as it requires running an external command
// or linking a Git library. This is a basic placeholder showing how
// you *might* integrate it. Error handling and efficiency are important here.
static int c_kilo_get_git_branch(lua_State *L) {
    return 0;
    // --- Find Git Root Directory (.git) ---

    if (!E.current_buffer->dirname) {
        lua_pushnil(L); // Cannot search without a starting directory
        return 1;
    }

    char current_path[PATH_MAX];
    // Use realpath to resolve symlinks and get a canonical absolute path
    // If realpath isn't available or desired, strncpy is the minimum.
    // Using strncpy directly might fail if E.dirname is relative and complex.
    if (realpath(!E.current_buffer->dirname, current_path) == NULL) {
         // Fallback or error handling if realpath fails
         // Using strncpy might be sufficient if E.dirname is usually absolute
         strncpy(current_path, !E.current_buffer->dirname, sizeof(current_path) - 1);
         current_path[sizeof(current_path) - 1] = '\0'; // Ensure null termination
         // Optionally, log an error here about realpath failing
    }


    char git_root_path[PATH_MAX] = ""; // To store the found root path
    int found_root = 0;

    // Loop searching upwards for .git
    while (strlen(current_path) > 0 && strcmp(current_path, "/") != 0) {
        char dot_git_path[PATH_MAX];
        snprintf(dot_git_path, sizeof(dot_git_path), "%s/.git", current_path);

        // Check if '.git' exists in the current_path
        // Using access(F_OK) is simpler than stat if you only need existence check,
        // but stat confirms it's a directory, which is more robust.
        struct stat st;
        if (stat(dot_git_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            // Found the .git directory!
            strncpy(git_root_path, current_path, sizeof(git_root_path) -1);
            git_root_path[sizeof(git_root_path) - 1] = '\0';
            found_root = 1;
            break; // Exit the loop
        }

        // Go up one directory level
        char *last_slash = strrchr(current_path, '/');
        if (last_slash == current_path) { // Handling the root directory case "/"
             if (strlen(current_path) > 1) { // Path was like "/foo", now becomes "/"
                 current_path[1] = '\0';
             } else {
                 break; // Already at "/", stop searching
             }
        } else if (last_slash != NULL) {
            *last_slash = '\0'; // Truncate the string at the last slash
        } else {
            break; // Should not happen with absolute paths, but break just in case
        }
    }

    // Check root ("/") itself if not found yet and we ended there
    if (!found_root && strcmp(current_path, "/") == 0) {
         char dot_git_path[PATH_MAX];
         snprintf(dot_git_path, sizeof(dot_git_path), "%s/.git", current_path);
         struct stat st;
         if (stat(dot_git_path, &st) == 0 && S_ISDIR(st.st_mode)) {
              strncpy(git_root_path, current_path, sizeof(git_root_path) -1);
              git_root_path[sizeof(git_root_path) - 1] = '\0';
              found_root = 1;
         }
    }


    // --- If root found, run git command ---

    if (!found_root) {
        lua_pushnil(L); // Not inside a git repository
        return 1;
    }

    // Now, run the git command using the found git_root_path
    char command[PATH_MAX + 100]; // Make buffer large enough
    snprintf(command, sizeof(command), "git -C \"%s\" rev-parse --abbrev-ref HEAD 2>/dev/null", git_root_path);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        // Failed to run command (maybe git isn't installed or other popen error)
        // You might want to log this error using editorSetStatusMessage or similar
        lua_pushnil(L);
        return 1;
    }

    char branch_name[128]; // Buffer for branch name
    if (fgets(branch_name, sizeof(branch_name), fp) != NULL) {
        // Remove trailing newline
        branch_name[strcspn(branch_name, "\n")] = 0;

        // Handle detached HEAD state where the output is "HEAD"
        if (strcmp(branch_name, "HEAD") == 0) {
             // Optionally get the short commit hash instead for detached HEAD
             pclose(fp); // Close the previous pipe first!

             // Construct command to get short hash
             snprintf(command, sizeof(command), "git -C \"%s\" rev-parse --short HEAD 2>/dev/null", git_root_path);
             fp = popen(command, "r");
             if (fp != NULL && fgets(branch_name, sizeof(branch_name), fp) != NULL) {
                 branch_name[strcspn(branch_name, "\n")] = 0;
                 // Maybe prefix or suffix it to indicate it's a commit hash?
                 char detached_str[140];
                 snprintf(detached_str, sizeof(detached_str), "(%.*s)", (int)sizeof(branch_name) - 1, branch_name); // e.g., "(a1b2c3d)"
                 lua_pushstring(L, detached_str);
             } else {
                 // Fallback if getting hash fails
                 lua_pushstring(L, "(detached)");
             }
        } else {
            // It's a regular branch name
             lua_pushstring(L, branch_name);
        }

    } else {
        // Command ran but produced no output (unexpected for rev-parse in a repo)
        // Could happen briefly during rebase or complex operations?
        lua_pushnil(L);
    }

    // Important: Close the pipe resource
    // If fp is NULL from the second popen call, pclose handles NULL gracefully.
    if (fp) {
         pclose(fp);
    }


    return 1; // Return 1 value (string or nil)
}
