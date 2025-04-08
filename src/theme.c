#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include "kilo.h"

// --- Helper Functions ---
extern char *trimWhitespace(char *str);


// --- Core Theme Parsing and Loading ---

// Parses a single .theme file and populates E.theme
static int parseThemeFile(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "parseThemeFile: Cannot open theme file '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    int success = 0; // Assume success until error
    int linenum = 0;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        linenum++;
        char *trimmed_line = trimWhitespace(line);
        if (trimmed_line[0] == '#' || trimmed_line[0] == '\0') continue; // Skip comments/empty

        char *colon = strchr(trimmed_line, ':');
        if (!colon) {
            fprintf(stderr, "parseThemeFile: Invalid line %d in %s: %s\n", linenum, filepath, trimmed_line);
            continue; // Skip invalid line
        }

        *colon = '\0'; // Split key and value
        char *key = trimWhitespace(trimmed_line);
        char *value = trimWhitespace(colon + 1);
        char **target_ptr = NULL;

        // --- Big mapping from key string to E.theme field pointer ---
        // Syntax Highlighting
        if (strcmp(key, "HL_NORMAL_FG") == 0) target_ptr = &E.theme.hl_normal_fg;
        else if (strcmp(key, "HL_NORMAL_BG") == 0) target_ptr = &E.theme.hl_normal_bg;
        else if (strcmp(key, "HL_COMMENT_FG") == 0) target_ptr = &E.theme.hl_comment_fg;
        else if (strcmp(key, "HL_COMMENT_BG") == 0) target_ptr = &E.theme.hl_comment_bg;
        else if (strcmp(key, "HL_MLCOMMENT_FG") == 0) target_ptr = &E.theme.hl_mlcomment_fg;
        else if (strcmp(key, "HL_MLCOMMENT_BG") == 0) target_ptr = &E.theme.hl_mlcomment_bg;
        else if (strcmp(key, "HL_KEYWORD1_FG") == 0) target_ptr = &E.theme.hl_keyword1_fg;
        else if (strcmp(key, "HL_KEYWORD1_BG") == 0) target_ptr = &E.theme.hl_keyword1_bg;
        else if (strcmp(key, "HL_KEYWORD2_FG") == 0) target_ptr = &E.theme.hl_keyword2_fg;
        else if (strcmp(key, "HL_KEYWORD2_BG") == 0) target_ptr = &E.theme.hl_keyword2_bg;
        else if (strcmp(key, "HL_KEYWORD3_FG") == 0) target_ptr = &E.theme.hl_keyword3_fg;
        else if (strcmp(key, "HL_KEYWORD3_BG") == 0) target_ptr = &E.theme.hl_keyword3_bg;
        else if (strcmp(key, "HL_TYPE_FG") == 0) target_ptr = &E.theme.hl_type_fg;
        else if (strcmp(key, "HL_TYPE_BG") == 0) target_ptr = &E.theme.hl_type_bg;
        else if (strcmp(key, "HL_BUILTIN_FG") == 0) target_ptr = &E.theme.hl_builtin_fg;
        else if (strcmp(key, "HL_BUILTIN_BG") == 0) target_ptr = &E.theme.hl_builtin_bg;
        else if (strcmp(key, "HL_STRING_FG") == 0) target_ptr = &E.theme.hl_string_fg;
        else if (strcmp(key, "HL_STRING_BG") == 0) target_ptr = &E.theme.hl_string_bg;
        else if (strcmp(key, "HL_NUMBER_FG") == 0) target_ptr = &E.theme.hl_number_fg;
        else if (strcmp(key, "HL_NUMBER_BG") == 0) target_ptr = &E.theme.hl_number_bg;
        else if (strcmp(key, "HL_MATCH_FG") == 0) target_ptr = &E.theme.hl_match_fg;
        else if (strcmp(key, "HL_MATCH_BG") == 0) target_ptr = &E.theme.hl_match_bg;
        // UI Elements
        else if (strcmp(key, "UI_BACKGROUND_BG") == 0) target_ptr = &E.theme.ui_background_bg;
        else if (strcmp(key, "UI_LINENO_FG") == 0) target_ptr = &E.theme.ui_lineno_fg;
        else if (strcmp(key, "UI_LINENO_BG") == 0) target_ptr = &E.theme.ui_lineno_bg;
        else if (strcmp(key, "UI_STATUS_FG") == 0) target_ptr = &E.theme.ui_status_fg;
        else if (strcmp(key, "UI_STATUS_BG") == 0) target_ptr = &E.theme.ui_status_bg;
        else if (strcmp(key, "UI_MESSAGE_FG") == 0) target_ptr = &E.theme.ui_message_fg;
        else if (strcmp(key, "UI_MESSAGE_BG") == 0) target_ptr = &E.theme.ui_message_bg;
        else if (strcmp(key, "UI_TILDE_FG") == 0) target_ptr = &E.theme.ui_tilde_fg;
        else if (strcmp(key, "UI_TILDE_BG") == 0) target_ptr = &E.theme.ui_tilde_bg;
        else if (strcmp(key, "name") == 0) target_ptr = &E.theme.name; // Added theme name

        // Status bar theme
        else if (strcmp(key, "UI_STATUS_MODE_FG") == 0) target_ptr = &E.theme.ui_status_mode_fg;
        else if (strcmp(key, "UI_STATUS_MODE_BG") == 0) target_ptr = &E.theme.ui_status_mode_bg;
        else if (strcmp(key, "UI_STATUS_FILE_FG") == 0) target_ptr = &E.theme.ui_status_file_fg;
        else if (strcmp(key, "UI_STATUS_FILE_BG") == 0) target_ptr = &E.theme.ui_status_file_bg;
        else if (strcmp(key, "UI_STATUS_INFO_FG") == 0) target_ptr = &E.theme.ui_status_info_fg;
        else if (strcmp(key, "UI_STATUS_INFO_BG") == 0) target_ptr = &E.theme.ui_status_info_bg;
        else if (strcmp(key, "UI_STATUS_FT_FG") == 0) target_ptr = &E.theme.ui_status_ft_fg;
        else if (strcmp(key, "UI_STATUS_FT_BG") == 0) target_ptr = &E.theme.ui_status_ft_bg;
        else if (strcmp(key, "UI_STATUS_POS_FG") == 0) target_ptr = &E.theme.ui_status_pos_fg;
        else if (strcmp(key, "UI_STATUS_POS_BG") == 0) target_ptr = &E.theme.ui_status_pos_bg;
        
        else {
             fprintf(stderr, "parseThemeFile: Unknown theme key '%s' in %s:%d\n", key, filepath, linenum);
        }

        // Assign value if target found
        if (target_ptr) {
            free(*target_ptr); // Free existing value if any (for theme reloading)
            *target_ptr = strdup(value);
            if (!*target_ptr) {
                perror("parseThemeFile: strdup failed");
                success = -1;
                // Optional: could break here on critical failure
            }
        }
    } // End while getline

    free(line);
    fclose(fp);

    // Basic check: Ensure at least normal colors are set, provide fallback
    if (!E.theme.hl_normal_fg) E.theme.hl_normal_fg = strdup("default");
    if (!E.theme.hl_normal_bg) E.theme.hl_normal_bg = strdup("default");
    // Add more checks/fallbacks as needed

    return success;
}


// Loads the specified theme file
void loadTheme(const char *theme_name) {
    char filepath[PATH_MAX]; // Include <limits.h> or define PATH_MAX
    int len = snprintf(filepath, sizeof(filepath), "src/themes/%s.theme", theme_name);

    if (len < 0 || (size_t)len >= sizeof(filepath)) {
         fprintf(stderr, "loadTheme: Theme name too long or error creating path: %s\n", theme_name);
         return;
    }

        // editorSetStatusMessage("Loaded theme: %s", theme_name);


    // Free existing theme colors before loading new ones
    // This assumes freeThemeColors correctly handles NULLs
    // If this is the first load, all E.theme pointers should be NULL
    // freeThemeColors(); // Call this *before* parseThemeFile

    if (parseThemeFile(filepath) != 0) {
        fprintf(stderr, "loadTheme: Failed to load theme '%s'. Falling back?\n", theme_name);
        // Optionally: Try loading a guaranteed "default.theme" here as fallback
        // freeThemeColors(); // Clear potentially partially loaded theme
        // parseThemeFile("themes/kilo_dark.theme");
    }
     // After loading, maybe trigger a screen refresh?
     // editorRefreshScreen(); // Needs careful thought about where this is called from
}

// Frees memory allocated for theme color strings
void freeThemeColors(void) {
    // Helper macro to free if not NULL
    #define FREE_THEME_COLOR(field) do { free(E.theme.field); E.theme.field = NULL; } while (0)

    FREE_THEME_COLOR(name);
    FREE_THEME_COLOR(hl_normal_fg); FREE_THEME_COLOR(hl_normal_bg);
    FREE_THEME_COLOR(hl_comment_fg); FREE_THEME_COLOR(hl_comment_bg);
    FREE_THEME_COLOR(hl_mlcomment_fg); FREE_THEME_COLOR(hl_mlcomment_bg);
    FREE_THEME_COLOR(hl_keyword1_fg); FREE_THEME_COLOR(hl_keyword1_bg);
    FREE_THEME_COLOR(hl_keyword2_fg); FREE_THEME_COLOR(hl_keyword2_bg);
    FREE_THEME_COLOR(hl_keyword3_fg); FREE_THEME_COLOR(hl_keyword3_bg);
    FREE_THEME_COLOR(hl_type_fg); FREE_THEME_COLOR(hl_type_bg);
    FREE_THEME_COLOR(hl_builtin_fg); FREE_THEME_COLOR(hl_builtin_bg);
    FREE_THEME_COLOR(hl_string_fg); FREE_THEME_COLOR(hl_string_bg);
    FREE_THEME_COLOR(hl_number_fg); FREE_THEME_COLOR(hl_number_bg);
    FREE_THEME_COLOR(hl_match_fg); FREE_THEME_COLOR(hl_match_bg);

    FREE_THEME_COLOR(ui_background_bg);
    FREE_THEME_COLOR(ui_lineno_fg); FREE_THEME_COLOR(ui_lineno_bg);
    FREE_THEME_COLOR(ui_status_fg); FREE_THEME_COLOR(ui_status_bg);
    FREE_THEME_COLOR(ui_message_fg); FREE_THEME_COLOR(ui_message_bg);
    FREE_THEME_COLOR(ui_tilde_fg);   FREE_THEME_COLOR(ui_tilde_bg);

    #undef FREE_THEME_COLOR
}


// Helper to parse "r,g,b" string and generate ANSI code part
// Returns 1 on success, 0 on failure
static int parse_rgb(const char *color_str, int *r, int *g, int *b) {
    if (!color_str || strcmp(color_str, "default") == 0) 
        return 0; // Indicate default needed
    
    // Check if it's a hex code (starts with #)
    if (color_str[0] == '#') {
        // Skip the # character
        const char *hex = color_str + 1;
        int len = strlen(hex);
        
        // Handle #RGB format (short form)
        if (len == 3) {
            unsigned int values[3];
            if (sscanf(hex, "%1x%1x%1x", &values[0], &values[1], &values[2]) == 3) {
                // Convert from #RGB to #RRGGBB by duplicating each digit
                *r = values[0] * 17; // 0xR -> 0xRR (multiply by 17 or 0x11)
                *g = values[1] * 17;
                *b = values[2] * 17;
                return 1;
            }
        }
        // Handle #RRGGBB format
        else if (len == 6) {
            unsigned int ur, ug, ub;
            if (sscanf(hex, "%2x%2x%2x", &ur, &ug, &ub) == 3) {
                *r = (int)ur;
                *g = (int)ug;
                *b = (int)ub;
                return 1;
            }
        }
        // Handle #RRGGBBAA format (ignoring alpha)
        else if (len == 8) {
            unsigned int ur, ug, ub, ua;
            if (sscanf(hex, "%2x%2x%2x%2x", &ur, &ug, &ub, &ua) >= 3) {
                *r = (int)ur;
                *g = (int)ug;
                *b = (int)ub;
                return 1;
            }
        }
        
        fprintf(stderr, "Warning: Invalid hex colour code '%s'\n", color_str);
        return 0;
    } 
    // Otherwise, try parsing as R,G,B
    else {
        if (sscanf(color_str, "%d,%d,%d", r, g, b) == 3) {
            // Ensure values are in valid range (0-255)
            *r = (*r < 0) ? 0 : (*r > 255) ? 255 : *r;
            *g = (*g < 0) ? 0 : (*g > 255) ? 255 : *g;
            *b = (*b < 0) ? 0 : (*b > 255) ? 255 : *b;
            return 1;
        }
        
        fprintf(stderr, "Warning: Invalid RGB string '%s'\n", color_str);
        return 0;
    }
}

// Appends ANSI truecolor escape codes to the buffer
void applyTrueColor(struct abuf *ab, const char *fg_rgb_str, const char *bg_rgb_str) {
    char buf[64];
    int r, g, b;

    // Apply Foreground
    if (parse_rgb(fg_rgb_str, &r, &g, &b)) {
        snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
        abAppend(ab, buf, strlen(buf));
    } else if (fg_rgb_str && strcmp(fg_rgb_str, "default") == 0) {
        abAppend(ab, "\x1b[39m", 5); // Default FG
    } // Else: If NULL or parse failed, do nothing to FG

    // Apply Background
    if (parse_rgb(bg_rgb_str, &r, &g, &b)) {
        snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", r, g, b);
        abAppend(ab, buf, strlen(buf));
    } else if (bg_rgb_str && strcmp(bg_rgb_str, "default") == 0) {
        abAppend(ab, "\x1b[49m", 5); // Default BG
    } // Else: If NULL or parse failed, do nothing to BG
}

// Helper to reset colors to theme's normal/default
void applyThemeDefaultColor(struct abuf *ab) {
    applyTrueColor(ab, E.theme.hl_normal_fg, E.theme.hl_normal_bg);
}