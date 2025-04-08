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
// #include <libgen.h> // For dirname

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
        fprintf(stderr, "[Kilo Error] Could not create Lua state\n");
        return -1;
    }

    luaL_openlibs(L); // Load standard Lua libraries

    // Register C library module
    luaL_newlib(L, kilo_lib); // Create table, register functions
    lua_setglobal(L, "kilo"); // Make the table available globally as "kilo"

    printf("[Kilo] Lua initialized and 'kilo' module registered.\n");

    // Set up Neovim-style module paths
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    const char *current_path = lua_tostring(L, -1);
    
    char new_path[4096];
    // Get the config directory prefix
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char config_dir[1024];
    
    if (config_home) {
        snprintf(config_dir, sizeof(config_dir), "%s/kilo", config_home);
    } else if (home) {
        snprintf(config_dir, sizeof(config_dir), "%s/.config/kilo", home);
    } else {
        // Fallback if HOME isn't set
        strcpy(config_dir, "./");
    }
    
    // Create the path with all the necessary patterns
    snprintf(new_path, sizeof(new_path), 
             "%s;%s/?.lua;%s/?/init.lua;%s/lua/?.lua;%s/lua/?/init.lua", 
             current_path, config_dir, config_dir, config_dir, config_dir);
    
    // Set the new package.path
    lua_pushstring(L, new_path);
    lua_setfield(L, -3, "path");
    
    // Clean up the stack
    lua_pop(L, 2); // Pop package and path
    
    printf("[Kilo] Lua initialized with enhanced module paths\n");
    // *** END OF ADDED CODE ***

    printf("[Kilo] Lua initialized and 'kilo' module registered.\n");

    // --- Attempt to load and run user's init script ---
    char config_path[1024];
    if (config_home) {
        snprintf(config_path, sizeof(config_path), "%s/kilo/init.lua", config_home);
    } else {
        // Default to ~/.config/kilo/init.lua
        if (!home) {
             fprintf(stderr, "[Kilo/Lua Error] Cannot find HOME directory for config.\n");
             goto skip_lua_load;
        }
        snprintf(config_path, sizeof(config_path), "%s/.config/kilo/init.lua", home);
    }

    printf("[Kilo] Attempting to load Lua init script: %s\n", config_path);
    fflush(stdout);

    int result = luaL_dofile(L, config_path); // Execute the init script

    if (result != LUA_OK) {
        // Print error if loading/running fails
        const char *errorMsg = lua_tostring(L, -1);
        fprintf(stderr, "[Kilo/Lua Error] Failed to run init script (%s): %s\n", config_path, errorMsg);
        lua_pop(L, 1); // Remove error message from stack
        // Decide if this error is fatal or not. Maybe continue running Kilo?
    } else {
        printf("[Kilo] Lua init script executed successfully.\n");
        fflush(stdout);
    }

skip_lua_load: // Label for goto if HOME wasn't found

    // If initLua is the ONLY
    // interaction point, we might need to close it here, but usually
    // you keep it alive. For now, let's assume you might add more later
    // and don't call lua_close(L) here. Remember to call it on exit.

    return 0; // Success
}


// Lua functions

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