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
    {"print", c_kilo_lua_print},
    {"get_tabs", c_kilo_get_tabs},
    {NULL, NULL} /* Sentinel */
};

static lua_State *L = NULL;

int statusbar_callback_ref = LUA_NOREF; // Initialize reference holder

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
    printf("[Kilo C] Replaced standard Lua 'print' with debug logger.\n");
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

skip_lua_load: // Label used if config dir determination failed

    // Keep Lua state L alive for later use (e.g., statusbar callback)
    // lua_close(L) should happen on editor exit

    return 0; // Indicate Lua initialization sequence finished (even if script failed)
}


// Lua functions
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
    int n = lua_gettop(L); // Number of arguments passed to print()
    luaL_Buffer b;

    lua_getglobal(L, "tostring"); // Get Lua's built-in tostring function

    luaL_buffinit(L, &b); // Initialize buffer

    for (int i = 1; i <= n; i++) {
        lua_pushvalue(L, -1); // Duplicate tostring function
        lua_pushvalue(L, i);  // Push argument i
        lua_call(L, 1, 1);    // Call tostring(arg[i])

        // Check if tostring returned a string
        if (!lua_isstring(L, -1)) {
            // Handle error: tostring didn't return a string (shouldn't normally happen)
            // Report error via debug_printf maybe? Or push an error string?
            // For simplicity, we'll pop the non-string result and add placeholder
            lua_pop(L, 1); // Pop non-string result
            luaL_addstring(&b, "[?]");// Add placeholder for non-stringifiable value

        } else {
             // Add the resulting string to the buffer
             // luaL_addvalue pops the value from the stack after adding it
             luaL_addvalue(&b);
        }


        if (i < n) {
            luaL_addstring(&b, "\t"); // Add tab separator between arguments
        }
    }

    lua_pop(L, 1); // Pop the tostring function we initially pushed

    luaL_addstring(&b, "\n"); // Add newline at the end
    luaL_pushresult(&b);     // Push the final formatted string onto the stack
    const char *result_string = lua_tostring(L, -1); // Get C string pointer

    if (result_string) {
        // Send the result to Kilo's debug system
        debug_printf("[LUA] %s", result_string);
    }
    // else: Handle potential error getting string?

    lua_pop(L, 1); // Pop the final string
    return 0;      // Lua's print returns 0 values
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
    const char *filename = findBasename(E.filename);

    if (E.filename) {
        lua_pushstring(L, filename);
        }
    else {
        lua_pushnil(L); // Return nil if no filename
    }
    return 1;
}


static int c_kilo_get_dirname(lua_State *L) {
    if (E.dirname) {
        lua_pushstring(L, E.dirname);
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


// --- Optional: Git Branch ---
// This is more complex as it requires running an external command
// or linking a Git library. This is a basic placeholder showing how
// you *might* integrate it. Error handling and efficiency are important here.
static int c_kilo_get_git_branch(lua_State *L) {
    // --- Find Git Root Directory (.git) ---

    if (!E.dirname) {
        lua_pushnil(L); // Cannot search without a starting directory
        return 1;
    }

    char current_path[PATH_MAX];
    // Use realpath to resolve symlinks and get a canonical absolute path
    // If realpath isn't available or desired, strncpy is the minimum.
    // Using strncpy directly might fail if E.dirname is relative and complex.
    if (realpath(E.dirname, current_path) == NULL) {
         // Fallback or error handling if realpath fails
         // Using strncpy might be sufficient if E.dirname is usually absolute
         strncpy(current_path, E.dirname, sizeof(current_path) - 1);
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