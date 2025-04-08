#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <math.h> // Kept from original, check if needed
#include <limits.h> // Kept from original, check if needed
#include <wchar.h>  // Needed for wcwidth() and mbtowc()

// Kilo Project Headers
#include "kilo.h"
#include "k_lua.h"

// Lua Headers
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

// Macro to initialize an append buffer
#define ABUF_INIT {NULL, 0}

// --- Append Buffer Functions ---
// (abAppend and abFree remain the same as before)
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new_buf = realloc(ab->b, ab->len + len);
    if (new_buf == NULL) {
        perror("abAppend: realloc failed");
        // Consider more robust error handling, maybe exit?
        return;
    }
    memcpy(new_buf + ab->len, s, len);
    ab->b = new_buf;
    ab->len += len;
}
void abFree(struct abuf *ab) {
    free(ab->b);
}


// --- Screen Update Logic ---
// (editorScroll remains the same)
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) E.rowoff = E.cy;
  if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
  if (E.rx < E.coloff) E.coloff = E.rx;
  if (E.rx >= E.coloff + E.screencols) E.coloff = E.rx - E.screencols + 1;
}

// (editorDrawRows remains the same)
void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        int ln_width = KILO_LINE_NUMBER_WIDTH; // Assuming KILO_LINE_NUMBER_WIDTH is defined in kilo.h

        applyTrueColor(ab, NULL, E.theme.ui_background_bg);
        abAppend(ab, "\x1b[K", 3); // Clear line with background color

        // Draw Line Numbers
        char linenum[32];
        if (filerow < E.numrows) {
            snprintf(linenum, sizeof(linenum), "%*d ", ln_width - 1, filerow + 1);
            applyTrueColor(ab, E.theme.ui_lineno_fg, NULL); // Use line number foreground
            abAppend(ab, linenum, strlen(linenum));
            applyThemeDefaultColor(ab); // Reset color
        } else {
            // Draw tildes for empty lines or welcome message lines
            snprintf(linenum, sizeof(linenum), "%*s", ln_width, ""); // Create padding
            if (ln_width > 0) { // Only add tilde if there's space
                if (E.numrows == 0 && y == 0) linenum[0] = '~'; // Tilde only on first line if empty file? Or centered? Adjust as needed.
                else if (E.numrows > 0) linenum[0] = ' '; // Or keep it blank past end of file
                else linenum[0] = '~'; // Default tilde for welcome screen area
            }
            applyTrueColor(ab, E.theme.ui_tilde_fg, NULL); // Use tilde foreground
            abAppend(ab, linenum, strlen(linenum));
            applyThemeDefaultColor(ab); // Reset color
        }

        // Calculate available space for row content
        int available_cols = E.screencols - ln_width;
        if (available_cols < 0) available_cols = 0;

        // Draw Row Content or Welcome Message
        if (filerow >= E.numrows) {
             // Draw Welcome Message (only if file is empty)
             if (E.numrows == 0 && y == E.screenrows / 3) {
                 char welcome[80];
                 // Assuming KILO_VERSION is defined in kilo.h
                 int welcomelen = snprintf(welcome, sizeof(welcome),
                                       "Kilo editor -- version %s", KILO_VERSION);
                 if (welcomelen > available_cols) welcomelen = available_cols; // Truncate if needed

                 int padding = (available_cols - welcomelen) / 2;
                 if (padding > 0) { // Add left padding only if there's space
                     // applyThemeDefaultColor(ab); // Ensure default colors for padding
                     for (int p = 0; p < padding; ++p) abAppend(ab, " ", 1);
                 }
                 // applyThemeDefaultColor(ab); // Ensure default colors for message
                 abAppend(ab, welcome, welcomelen);
             }
        } else {
            // Draw Actual File Content
            erow *row = &E.row[filerow];
            int len = row->rsize - E.coloff; // Length of render string from current column offset
            if (len < 0) len = 0; // Clamp if offset is past the end
            if (len > available_cols) len = available_cols; // Clamp to screen width

            if (len > 0) {
                char *c = &row->render[E.coloff];
                unsigned char *hl = &row->hl[E.coloff];
                int current_applied_hl = -1; // Track applied highlight to minimize escape codes

                // Set default text color before iterating through chars
                applyThemeDefaultColor(ab);
                applyTrueColor(ab, E.theme.hl_normal_fg, E.theme.hl_normal_bg); // Apply default normal text color
                current_applied_hl = HL_NORMAL;

                for (int j = 0; j < len; j++) {
                    // Handle Control Characters
                    if (iscntrl(c[j])) {
                        char sym = (c[j] <= 26) ? '@' + c[j] : '?'; // Map to visible char
                        applyTrueColor(ab, E.theme.hl_normal_fg, E.theme.hl_normal_bg); // Ensure default bg
                        abAppend(ab, "\x1b[7m", 4); // Inverse video
                        abAppend(ab, &sym, 1);
                        abAppend(ab, "\x1b[m", 3); // Turn off all attributes (including inverse)
                        applyTrueColor(ab, E.theme.hl_normal_fg, E.theme.hl_normal_bg); // Reapply default colors
                        current_applied_hl = HL_NORMAL; // Force color reset after control char
                    }
                    // Apply Syntax Highlighting
                    else if (hl[j] != current_applied_hl) {
                        current_applied_hl = hl[j];
                        char *fg = NULL, *bg = NULL;
                        // Map highlight type to theme colors (ensure all HL_ enums are handled)
                        switch (hl[j]) {
                           case HL_COMMENT:   fg = E.theme.hl_comment_fg;   bg = E.theme.hl_comment_bg;   break;
                           case HL_MLCOMMENT: fg = E.theme.hl_mlcomment_fg; bg = E.theme.hl_mlcomment_bg; break;
                           case HL_KEYWORD1:  fg = E.theme.hl_keyword1_fg;  bg = E.theme.hl_keyword1_bg;  break;
                           case HL_KEYWORD2:  fg = E.theme.hl_keyword2_fg;  bg = E.theme.hl_keyword2_bg;  break;
                           case HL_KEYWORD3:  fg = E.theme.hl_keyword3_fg;  bg = E.theme.hl_keyword3_bg;  break;
                           case HL_TYPE:      fg = E.theme.hl_type_fg;      bg = E.theme.hl_type_bg;      break;
                           case HL_BUILTIN:   fg = E.theme.hl_builtin_fg;   bg = E.theme.hl_builtin_bg;   break;
                           case HL_STRING:    fg = E.theme.hl_string_fg;    bg = E.theme.hl_string_bg;    break;
                           case HL_NUMBER:    fg = E.theme.hl_number_fg;    bg = E.theme.hl_number_bg;    break;
                           case HL_MATCH:     fg = E.theme.hl_match_fg;     bg = E.theme.hl_match_bg;     break;
                           case HL_NORMAL:
                           default:           fg = E.theme.hl_normal_fg;    bg = E.theme.hl_normal_bg;    break;
                        }
                        applyTrueColor(ab, fg, bg); // Apply the new colors
                        abAppend(ab, &c[j], 1); // Append the character
                    } else {
                        // Character has the same highlight as the previous one
                        abAppend(ab, &c[j], 1);
                    }
                }
            }
            // Reset colors after drawing the row content
            applyThemeDefaultColor(ab);
        }
        // Line finished, append newline
        abAppend(ab, "\r\n", 2);
    }
}


// --- Status Bar Functions ---

// (getThemeColorByName remains the same)
char* getThemeColorByName(const char* name) {
    if (!name) return NULL;
    // Consider using a more efficient lookup (hash map?) if the number of names grows significantly.
    char* color = NULL;
    if (strcmp(name, "ui_background_bg") == 0) color = E.theme.ui_background_bg;
    else if (strcmp(name, "ui_lineno_fg") == 0) color = E.theme.ui_lineno_fg;
    else if (strcmp(name, "ui_tilde_fg") == 0) color = E.theme.ui_tilde_fg;
    else if (strcmp(name, "ui_message_fg") == 0) color = E.theme.ui_message_fg;
    else if (strcmp(name, "ui_message_bg") == 0) color = E.theme.ui_message_bg;
    else if (strcmp(name, "ui_status_bg") == 0) color = E.theme.ui_status_bg;
    else if (strcmp(name, "ui_status_fg") == 0) color = E.theme.ui_status_fg;
    else if (strcmp(name, "ui_status_mode_fg") == 0) color = E.theme.ui_status_mode_fg;
    else if (strcmp(name, "ui_status_mode_bg") == 0) color = E.theme.ui_status_mode_bg;
    else if (strcmp(name, "ui_status_file_fg") == 0) color = E.theme.ui_status_file_fg;
    else if (strcmp(name, "ui_status_file_bg") == 0) color = E.theme.ui_status_file_bg;
    else if (strcmp(name, "ui_status_ft_fg") == 0) color = E.theme.ui_status_ft_fg;
    else if (strcmp(name, "ui_status_ft_bg") == 0) color = E.theme.ui_status_ft_bg;
    else if (strcmp(name, "ui_status_pos_fg") == 0) color = E.theme.ui_status_pos_fg;
    else if (strcmp(name, "ui_status_pos_bg") == 0) color = E.theme.ui_status_pos_bg;
    else if (strcmp(name, "ui_status_info_fg") == 0) color = E.theme.ui_status_info_fg;
    else if (strcmp(name, "ui_status_info_bg") == 0) color = E.theme.ui_status_info_bg;
    // Syntax highlight colors (if needed by Lua)
    else if (strcmp(name, "hl_comment_fg") == 0) color = E.theme.hl_comment_fg;
    else if (strcmp(name, "hl_mlcomment_fg") == 0) color = E.theme.hl_mlcomment_fg;
    // ... add other hl_ colors if Lua might request them by name ...
    return color;
}


/**
 * @brief Calculate the visible column width of a UTF-8 string, ignoring ANSI escape codes.
 *
 * This function processes a UTF-8 encoded string, skips over ANSI SGR escape
 * sequences (like colors), and uses wcwidth() to determine the column width
 * of each visible character. It requires the locale to be set correctly
 * (e.g., via setlocale(LC_CTYPE, "") in main) to handle multi-byte characters.
 *
 * @param s The null-terminated UTF-8 string to measure.
 * @return The total visible column width of the string.
 */
int calculate_visible_length_ansi(const char *s) {
    if (!s) return 0;

    // IMPORTANT: Requires setlocale(LC_CTYPE, "") to be called previously!
    // Reset mbtowc internal state at the beginning (good practice)
    mbtowc(NULL, NULL, 0);

    int visible_width = 0;
    int i = 0; // Byte index into the string 's'
    int n = strlen(s);

    while (i < n) {
        // Check for ANSI escape sequence
        if (s[i] == '\x1b') {
            i++; // Skip '\x1b'
            if (i < n && s[i] == '[') { // CSI (Control Sequence Introducer)
                i++; // Skip '['
                // Skip parameters (digits and ';')
                while (i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == ';')) {
                    i++;
                }
                // Skip the final command character (if present)
                if (i < n) {
                    // Common SGR command is 'm'
                    // Could add checks for other common CSI sequences if needed
                    i++;
                }
            } else if (i < n && s[i] == ']') { // OSC (Operating System Command) - less common in status lines
                i++; // Skip ']'
                // Skip until BEL ('\a') or ST ('\x1b\\')
                while (i < n && s[i] != '\a') {
                     if (s[i] == '\x1b' && i + 1 < n && s[i+1] == '\\') {
                         i += 2; // Skip ST
                         break;
                     }
                     i++;
                }
                if(i < n && s[i] == '\a') i++; // Skip BEL
            }
             else {
                // Other escape sequence type (less common for SGR, e.g., '\x1b' followed by a single char)
                // Skip the next character as well
                if (i < n) i++;
            }
        } else { // Not an escape sequence, process as a character
            wchar_t wc;
            int bytes_consumed = mbtowc(&wc, &s[i], n - i); // Pass remaining length

            if (bytes_consumed > 0) { // Valid multi-byte character decoded
                int width = wcwidth(wc);
                if (width > 0) { // Only count printable characters with positive width
                    visible_width += width;
                } else if (width == 0) {
                    // Zero-width character (e.g., combining marks), don't add width
                } else { // width < 0 implies non-printable or error
                    // Treat as single-width replacement? Or ignore?
                    // Let's ignore for width calculation, like control chars.
                }
                i += bytes_consumed; // Advance index by number of bytes used
            } else if (bytes_consumed == 0) {
                 // Null terminator encountered within the string? Should not happen if n=strlen(s)
                 // Treat as end of string for safety.
                 break;
            } else { // bytes_consumed < 0: Invalid UTF-8 sequence
                // Treat the invalid byte as a single-width replacement character ('?') visually.
                visible_width++;
                i++; // Skip the invalid byte
                // Reset mbtowc state after an error
                mbtowc(NULL, NULL, 0);
            }
        }
    }
    return visible_width;
}

// Draw default C-based status bar (Updated to use ANSI-aware width calc)
void editorDrawDefaultStatusBar(struct abuf *ab) {
    // --- Configuration ---
     // Using UTF-8 encoded Powerline symbols directly
     const char *separator = "\uE0B0";      // Powerline Right Arrow U+E0B0 (UTF-8: EE 82 B0)
     const char *separator_rev = "\uE0B2";  // Powerline Left Arrow U+E0B2 (UTF-8: EE 82 B2)
     // Using Nerd Font icons (ensure your terminal font supports these)
     const char *folder_icon = "\uF115";    // Folder Open U+F115 (Nerd Font variation) (UTF-8: EF 84 95) - Check codepoint if needed
     const char *clock_icon = "\uF017";     // Clock U+F017 (Font Awesome via Nerd Font) (UTF-8: EF 80 97)
     const char *lang_default_icon = "\uF121"; // Code icon U+F121 (UTF-8: EF 84 A1)

     // Calculate widths using the updated function
     const int separator_width = calculate_visible_length_ansi(separator);
     const int separator_rev_width = calculate_visible_length_ansi(separator_rev);
     const int folder_icon_width = calculate_visible_length_ansi(folder_icon);
     const int clock_icon_width = calculate_visible_length_ansi(clock_icon);

     // --- Get Data ---
     // Filename (use basename if available, otherwise placeholder)
     // const char *filename = E.filename ? findBasename(E.filename) : "[No Name]"; // Requires findBasename
      const char *filename = E.filename ? (strrchr(E.filename, '/') ? strrchr(E.filename, '/') + 1 : E.filename) : "[No Name]"; // Basic basename


     // Mode string
     char *mode_str;
     switch (E.mode) {
        case MODE_NORMAL: mode_str = "NORMAL"; break;
        case MODE_INSERT: mode_str = "INSERT"; break;
        // Add other modes if they exist
        default: mode_str = "???"; break;
     }
     int mode_width = calculate_visible_length_ansi(mode_str);

     // Position Info
     char pos_info[40];
     int percentage = (E.numrows > 0) ? (int)(((double)(E.cy + 1) / E.numrows) * 100) : 100;
     // Ensure rx is based on the visual position (E.rx)
     snprintf(pos_info, sizeof(pos_info), "%d%% %d:%d", percentage, E.cy + 1, E.rx + 1);
     int pos_info_width = calculate_visible_length_ansi(pos_info);

     // Language / FileType Info
     char lang_str[30]; // Buffer for filetype name
     char lang_icon[10]; // Buffer for icon (UTF-8 can be multi-byte)
     strncpy(lang_icon, lang_default_icon, sizeof(lang_icon) - 1); // Start with default icon
     lang_icon[sizeof(lang_icon) - 1] = '\0';

     if (E.syntax) {
        // Use filetype name if available
        strncpy(lang_str, E.syntax->filetype, sizeof(lang_str) - 1);
        lang_str[sizeof(lang_str)-1] = '\0';
        // Use specific icon if provided in syntax definition
        if (E.syntax->status_icon) {
             // Simple check: if it starts with \uXXXX assume hex unicode, else copy directly
             // A more robust parser might be needed for various icon formats
            if (strncmp(E.syntax->status_icon, "\\u", 2) == 0 && strlen(E.syntax->status_icon) >= 6) {
                // Basic hex unicode parsing (replace with robust function if needed)
                // unsigned int code;
                // if (sscanf(E.syntax->status_icon + 2, "%4x", &code) == 1) {
                //    // Convert code point to UTF-8 string into lang_icon buffer
                //    // This is non-trivial, skip for simplicity, assume direct icon copy
                // }
                // For now, just copy if it doesn't look like \uXXXX
                 strncpy(lang_icon, E.syntax->status_icon, sizeof(lang_icon) - 1);
                 lang_icon[sizeof(lang_icon) - 1] = '\0';
            } else {
                 strncpy(lang_icon, E.syntax->status_icon, sizeof(lang_icon) - 1);
                 lang_icon[sizeof(lang_icon) - 1] = '\0';
            }
        }
     } else {
         // No syntax detected
         strncpy(lang_str, "[no ft]", sizeof(lang_str) - 1);
         lang_str[sizeof(lang_str)-1] = '\0';
     }
     int lang_icon_width = calculate_visible_length_ansi(lang_icon);
     int lang_str_width = calculate_visible_length_ansi(lang_str);


     // Time string
     char time_str[10];
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(time_str, sizeof(time_str), "%H:%M", tm_info); // HH:MM format
     int time_str_width = calculate_visible_length_ansi(time_str);

     // --- Define Segment Colors (using theme or fallbacks) ---
     // Helper macro for getting theme color or fallback
     #define GET_COLOR(theme_field, fallback) (E.theme.theme_field ? E.theme.theme_field : fallback)

     char *mode_fg = GET_COLOR(ui_status_mode_fg, "#000000"); // Black text
     char *mode_bg = GET_COLOR(ui_status_mode_bg, "#98971a"); // Green bg
     char *file_fg = GET_COLOR(ui_status_file_fg, "#ffffff"); // White text
     char *file_bg = GET_COLOR(ui_status_file_bg, "#504945"); // Dark gray bg
     char *ft_fg = GET_COLOR(ui_status_ft_fg, file_fg);      // Inherit from file fg
     char *ft_bg = GET_COLOR(ui_status_ft_bg, file_bg);      // Inherit from file bg
     char *pos_fg = GET_COLOR(ui_status_pos_fg, file_fg);      // Inherit from file fg
     char *pos_bg = GET_COLOR(ui_status_pos_bg, "#665c54"); // Mid gray bg
     char *info_fg = GET_COLOR(ui_status_info_fg, file_fg);     // Inherit from file fg
     char *info_bg = GET_COLOR(ui_status_info_bg, file_bg);     // Inherit from file bg
     char *status_bg = GET_COLOR(ui_status_bg, "#282828");   // Default status bg
     char *status_fg = GET_COLOR(ui_status_fg, "#ebdbb2");   // Default status fg
     char *dirty_fg = GET_COLOR(hl_keyword1_fg, "#fb4934"); // Use a highlight color for dirty indicator

     #undef GET_COLOR // Undefine macro after use


     // --- Prepare Buffers for Left and Right Sides ---
     struct abuf sb_left = ABUF_INIT;
     struct abuf sb_right = ABUF_INIT;
     int visible_left_width = 0;
     int visible_right_width = 0;

     // --- Build Left Side (sb_left) ---
     // Mode Segment
     applyTrueColor(&sb_left, mode_fg, mode_bg);
     abAppend(&sb_left, " ", 1); visible_left_width += 1;
     abAppend(&sb_left, mode_str, strlen(mode_str)); visible_left_width += mode_width;
     abAppend(&sb_left, " ", 1); visible_left_width += 1;

     // Separator 1 (Mode -> File/FT)
     // Determine next background color for the separator gradient
     char *left_sep1_next_bg = ft_bg; // Assume FT segment is next
     // Note: Original code had logic for E.dirname, which isn't used here. Add back if needed.
     // if (E.dirname && strlen(E.dirname) > 0) { left_sep1_next_bg = ft_bg; } else { left_sep1_next_bg = file_bg; }

     applyTrueColor(&sb_left, mode_bg, left_sep1_next_bg); // Current BG -> Next BG
     abAppend(&sb_left, separator, strlen(separator)); visible_left_width += separator_width;

     // Optional: Folder Segment (If E.dirname logic is added back)
     /*
     if (E.dirname && strlen(E.dirname) > 0) {
         applyTrueColor(&sb_left, ft_fg, ft_bg);
         abAppend(&sb_left, " ", 1); visible_left_width += 1;
         abAppend(&sb_left, folder_icon, strlen(folder_icon)); visible_left_width += folder_icon_width;
         abAppend(&sb_left, " ", 1); visible_left_width += 1;
         // Append dirname, calculate its width, add padding
         // ...
         applyTrueColor(&sb_left, ft_bg, file_bg); // Separator: FT BG -> File BG
         abAppend(&sb_left, separator, strlen(separator)); visible_left_width += separator_width;
     }
     */

     // File Segment (Icon + Name + Dirty Indicator)
     applyTrueColor(&sb_left, file_fg, file_bg);
     abAppend(&sb_left, " ", 1); visible_left_width += 1;
     abAppend(&sb_left, lang_icon, strlen(lang_icon)); visible_left_width += lang_icon_width; // Filetype icon
     abAppend(&sb_left, " ", 1); visible_left_width += 1;
     abAppend(&sb_left, filename, strlen(filename)); visible_left_width += calculate_visible_length_ansi(filename); // Filename
     if (E.dirty) {
        // Append dirty indicator with a distinct color, but same background
        applyTrueColor(&sb_left, dirty_fg, file_bg); // Use theme's dirty color
        abAppend(&sb_left, " \u271A", strlen(" \u271A")); // Heavy Greek Cross Mark (or just '*') UTF-8: E2 9C 9A
        visible_left_width += calculate_visible_length_ansi(" \u271A");
        applyTrueColor(&sb_left, file_fg, file_bg); // Switch back to file colors
     }
     abAppend(&sb_left, " ", 1); visible_left_width += 1;


     // Separator 2 (File -> Status BG)
     applyTrueColor(&sb_left, file_bg, status_bg); // File BG -> Default Status BG
     abAppend(&sb_left, separator, strlen(separator));
     visible_left_width += separator_width;


     // --- Build Right Side (sb_right, built in reverse visually) ---
     // Note: Segments are appended to sb_right in the order they should appear from right to left.

     // Separator 4 (Position -> Status BG) - Appended first to sb_right
     applyTrueColor(&sb_right, pos_bg, status_bg); // Position BG -> Status BG
     abAppend(&sb_right, separator_rev, strlen(separator_rev)); visible_right_width += separator_rev_width;

     // Position Segment
     applyTrueColor(&sb_right, pos_fg, pos_bg);
     abAppend(&sb_right, " ", 1); visible_right_width += 1;
     abAppend(&sb_right, pos_info, strlen(pos_info)); visible_right_width += pos_info_width;
     abAppend(&sb_right, " ", 1); visible_right_width += 1;

     // Separator 3 (Info -> Position)
     applyTrueColor(&sb_right, info_bg, pos_bg); // Info BG -> Position BG
     abAppend(&sb_right, separator_rev, strlen(separator_rev)); visible_right_width += separator_rev_width;

     // Info Segment (Time + Filetype String)
     applyTrueColor(&sb_right, info_fg, info_bg);
     abAppend(&sb_right, " ", 1); visible_right_width += 1;
     // Append filetype string first
     abAppend(&sb_right, lang_str, strlen(lang_str)); visible_right_width += lang_str_width;
     abAppend(&sb_right, " \u2502 ", strlen(" \u2502 ")); // Vertical separator | UTF-8: E2 94 82
     visible_right_width += calculate_visible_length_ansi(" \u2502 ");
     // Append time
     abAppend(&sb_right, clock_icon, strlen(clock_icon)); visible_right_width += clock_icon_width;
     abAppend(&sb_right, " ", 1); visible_right_width += 1;
     abAppend(&sb_right, time_str, strlen(time_str)); visible_right_width += time_str_width;
     abAppend(&sb_right, " ", 1); visible_right_width += 1;

     // Separator 2 (Status BG -> Info) - Appended last to sb_right
     applyTrueColor(&sb_right, status_bg, info_bg); // Status BG -> Info BG
     abAppend(&sb_right, separator_rev, strlen(separator_rev)); visible_right_width += separator_rev_width;


     // --- Assemble Final Status Bar ---
     // Set the default background for the entire line and clear it
     applyTrueColor(ab, status_fg, status_bg);
     abAppend(ab, "\x1b[K", 3); // Clear line with status bg color

     // Append the left side buffer
     abAppend(ab, sb_left.b, sb_left.len);

     // Calculate and append padding
     applyTrueColor(ab, status_fg, status_bg); // Ensure padding uses default status colors
     int padding = E.screencols - visible_left_width - visible_right_width;
     if (padding < 0) padding = 0; // Prevent negative padding
     for (int i = 0; i < padding; i++) abAppend(ab, " ", 1);

     // Append the right side buffer
     abAppend(ab, sb_right.b, sb_right.len);

     // Reset terminal colors completely at the end of the status bar
     applyThemeDefaultColor(ab);
     // No need for \r\n here, editorRefreshScreen handles moving to the next line implicitly
     // or editorDrawMessageBar will overwrite this line. If message bar is optional, might need \r\n.
     // Let's assume message bar always draws.

     // Free temporary buffers
     abFree(&sb_left);
     abFree(&sb_right);
}


// Draw status bar using Lua configuration (Direct Rendering Version)
void editorDrawStatusBar(struct abuf *ab) {
    lua_State *L = getLuaState(); // Assuming getLuaState() is defined in k_lua.h/c

    // Ensure statusbar_callback_ref is defined and initialized (e.g., in k_lua.c)
    // extern int statusbar_callback_ref; // Make sure this is accessible

    if (!L || statusbar_callback_ref == LUA_NOREF) {
        editorSetStatusMessage("Lua not ready or no callback");
        // editorDrawDefaultStatusBar(ab); // Fallback to default C bar if Lua not ready or no callback
        return;
    }

    // --- Call Lua Function ---
    lua_rawgeti(L, LUA_REGISTRYINDEX, statusbar_callback_ref);
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        editorSetStatusMessage("Status bar Lua error: %s", error_msg ? error_msg : "unknown");
        lua_pop(L, 1); // Pop error message
        // editorDrawDefaultStatusBar(ab); // Fallback on error
        return;
    }

    // --- Parse Lua Return Values ---
    // Expects: table segments [, table options]
    int segments_idx = -2;
    int options_idx = -1;

    // Determine which return values are tables
    int top = lua_gettop(L);
    int has_options = 0;

    if (top >= 1 && lua_istable(L, -1)) { // Top is options or segments
        if (top >= 2 && lua_istable(L, -2)) { // Both tables provided
            segments_idx = -2;
            options_idx = -1;
            has_options = 1;
        } else { // Only one table provided, assume it's segments
            segments_idx = -1;
            options_idx = 0; // Indicate no options table
            has_options = 0;
        }
    } else {
        // Invalid return type(s)
        editorSetStatusMessage("Status bar Lua function must return a table of segments");
        lua_pop(L, top); // Clear the stack
        // editorDrawDefaultStatusBar(ab);
        return;
    }


    // --- Default & Parsed Options ---
    int statusbar_height = 1; // Default height is 1 line
    // Use theme defaults, allow Lua override
    char *status_bg = E.theme.ui_status_bg ? E.theme.ui_status_bg : "#282828"; // Fallback default
    char *status_fg = E.theme.ui_status_fg ? E.theme.ui_status_fg : "#ebdbb2"; // Fallback default

    if (has_options) {
        lua_getfield(L, options_idx, "height");
        if (lua_isnumber(L, -1)) {
             statusbar_height = lua_tointeger(L, -1);
             if (statusbar_height < 1) statusbar_height = 1; // Min height 1
             // Add a reasonable max height?
             // if (statusbar_height > E.screenrows -1) statusbar_height = E.screenrows -1; // Avoid overlap
             if (statusbar_height > 5) statusbar_height = 5; // Arbitrary max
        }
        lua_pop(L, 1); // Pop height or nil

        lua_getfield(L, options_idx, "bg");
        if (lua_isstring(L, -1)) {
             const char *n = lua_tostring(L, -1);
             char* t = getThemeColorByName(n); // Check theme first
             if(t) status_bg = t;
             else if(n[0] == '#') status_bg = (char*)n; // Allow direct hex codes
        }
        lua_pop(L, 1); // Pop bg or nil

        lua_getfield(L, options_idx, "fg");
        if (lua_isstring(L, -1)) {
             const char *n = lua_tostring(L, -1);
             char* t = getThemeColorByName(n);
             if(t) status_fg = t;
             else if(n[0] == '#') status_fg = (char*)n;
        }
        lua_pop(L, 1); // Pop fg or nil
    }

    // Clamp statusbar height based on available screen rows (leave space for message bar)
    if (statusbar_height >= E.screenrows) {
        statusbar_height = E.screenrows - 1;
        if (statusbar_height < 1) statusbar_height = 1; // Ensure at least 1 if possible
    }

    // Define structure for collected segment data
    typedef struct {
        const char *text; // Includes ANSI codes, points to Lua string (careful!)
        char *fg;
        char *bg;
        int width; // Visual width (ANSI stripped, wcwidth calculated)
        int alignment; // 0=left, 1=center (not implemented), 2=right
    } SegmentData;


    // --- Loop Through Each Status Bar Line ---
    for (int line_idx = 0; line_idx < statusbar_height; line_idx++) {

        // --- Collect Segments for this specific line ---
        lua_Integer segment_count_raw = lua_rawlen(L, segments_idx);
        // Check for potential overflow if segment_count_raw is huge
        if (segment_count_raw > INT_MAX) {
             editorSetStatusMessage("Error: Too many status bar segments");
             segment_count_raw = INT_MAX; // Prevent overflow in malloc/loop
        }
        int segment_count = (int)segment_count_raw;

        // Use dynamic allocation, check for failure
        SegmentData *line_segments = malloc(sizeof(SegmentData) * segment_count);
        if (!line_segments && segment_count > 0) { // Check segment_count > 0 avoids malloc(0) issues
             editorSetStatusMessage("Error: Status bar segment memory allocation failed");
             lua_pop(L, has_options ? 2 : 1); // Pop segments and maybe options
             // Maybe draw default bar here instead of just returning?
             editorDrawDefaultStatusBar(ab);
             return;
        }

        int line_segment_idx = 0; // Index within line_segments for the current line
        int total_left_width = 0;
        int total_right_width = 0;
        // Center alignment is harder with direct rendering, skipping for now.

        // Iterate through all segments returned by Lua
        for (int i = 1; i <= segment_count; i++) {
            lua_rawgeti(L, segments_idx, i); // Get the i-th segment table
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1); // Pop non-table item
                continue; // Skip this segment
            }

            // Check which line this segment belongs to (default to line 0 if not specified)
            int segment_line_num = 0; // Default to the first line (index 0)
            lua_getfield(L, -1, "line");
            if (lua_isnumber(L, -1)) {
                segment_line_num = lua_tointeger(L, -1) - 1; // Lua uses 1-based index
            }
            lua_pop(L, 1); // Pop line number or nil

            // If this segment isn't for the current line being drawn, skip it
            if (segment_line_num != line_idx) {
                lua_pop(L, 1); // Pop the segment table
                continue;
            }

            // Get segment text (required)
            const char *text = NULL;
            lua_getfield(L, -1, "text");
            if (lua_isstring(L, -1)) {
                text = lua_tostring(L, -1); // Get pointer to Lua's string data
            }
            lua_pop(L, 1); // Pop text or nil

            if (!text) { // Text is mandatory for a segment
                lua_pop(L, 1); // Pop the segment table
                continue; // Skip segment without text
            }

            // Get optional fg/bg colors
            const char *fg_name = NULL, *bg_name = NULL;
            lua_getfield(L, -1, "fg"); if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "bg"); if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1); lua_pop(L, 1);

            // Resolve colors (use segment specific, fallback to line default, fallback to theme default)
            char* fg_color = status_fg; // Start with line default
            if (fg_name) {
                char* t = getThemeColorByName(fg_name);
                if(t) fg_color = t; else if(fg_name[0] == '#') fg_color = (char*)fg_name;
            }
            char* bg_color = status_bg; // Start with line default
            if (bg_name) {
                char* t = getThemeColorByName(bg_name);
                if(t) bg_color = t; else if(bg_name[0] == '#') bg_color = (char*)bg_name;
            }

            // Get optional alignment (default left)
            int alignment = 0; // 0 = left
            lua_getfield(L, -1, "align");
            if (lua_isstring(L, -1)) {
                const char *a = lua_tostring(L, -1);
                if (strcmp(a, "right") == 0) alignment = 2;
                // else if (strcmp(a, "center") == 0) alignment = 1; // Center not handled yet
            }
            lua_pop(L, 1); // Pop align or nil

            // Calculate visual width using the ANSI-aware, wcwidth-based function
            int visual_width = calculate_visible_length_ansi(text);

            // Store segment data for rendering pass
            // Ensure we don't write past the allocated buffer (shouldn't happen if malloc succeeded)
             if (line_segment_idx < segment_count) {
                line_segments[line_segment_idx].text = text; // Store pointer to Lua string
                line_segments[line_segment_idx].fg = fg_color;
                line_segments[line_segment_idx].bg = bg_color;
                line_segments[line_segment_idx].width = visual_width;
                line_segments[line_segment_idx].alignment = alignment;

                // Accumulate widths for padding calculation
                if (alignment == 0) total_left_width += visual_width;
                else if (alignment == 2) total_right_width += visual_width;

                line_segment_idx++;
             } // else: Should ideally report an error or increase buffer if possible


            lua_pop(L, 1); // Pop the segment table for this iteration
        } // --- End segment collection loop for this line ---


        // --- Render the status line directly to abuf ---
        applyTrueColor(ab, status_fg, status_bg); // Set default background/foreground for the line
        abAppend(ab, "\x1b[K", 3);                 // Clear line with these default colors

        int current_visual_col = 0; // Track visual column position on screen

        // Render Left-Aligned Segments directly
        for (int i = 0; i < line_segment_idx; i++) {
            if (line_segments[i].alignment == 0) { // Left aligned
                 // Check if adding this segment would exceed screen width
                 if (current_visual_col + line_segments[i].width <= E.screencols) {
                    applyTrueColor(ab, line_segments[i].fg, line_segments[i].bg);
                    // IMPORTANT: Appending the raw text from Lua string pointer
                    abAppend(ab, line_segments[i].text, strlen(line_segments[i].text));
                    current_visual_col += line_segments[i].width; // Advance by calculated visual width
                 } else {
                     // Not enough space for the whole segment.
                     // Option 1: Skip it entirely (simplest)
                     // Option 2: Truncate it (more complex, requires careful handling of ANSI/UTF-8 within the segment)
                     // Let's skip for now.
                     break; // Stop rendering left segments if one doesn't fit
                 }
            }
        }

        // Render Padding (fills space between left and right segments)
        applyTrueColor(ab, status_fg, status_bg); // Reset to default line colors for padding
        int padding = E.screencols - total_left_width - total_right_width;
        if (padding < 0) padding = 0; // No padding if segments already exceed width

         // Ensure padding doesn't push content off-screen if left segments were near the edge
         if (current_visual_col + padding > E.screencols) {
             padding = E.screencols - current_visual_col;
             if (padding < 0) padding = 0;
         }

        for (int i = 0; i < padding; i++) {
            abAppend(ab, " ", 1);
        }
        current_visual_col += padding; // Account for padding width


        // Render Right-Aligned Segments directly (in the order they appear in the Lua table)
        // Note: This renders them starting immediately after the padding.
        // A true right-alignment might require calculating positions beforehand.
        for (int i = 0; i < line_segment_idx; i++) {
            if (line_segments[i].alignment == 2) { // Right aligned
                 // Check if there's *any* space left before attempting to draw
                 // More precise check: current_visual_col + line_segments[i].width <= E.screencols
                 if (current_visual_col < E.screencols) {
                      if (current_visual_col + line_segments[i].width <= E.screencols) {
                          applyTrueColor(ab, line_segments[i].fg, line_segments[i].bg);
                          abAppend(ab, line_segments[i].text, strlen(line_segments[i].text));
                          current_visual_col += line_segments[i].width;
                      } else {
                          // Not enough space for the whole segment. Skip or truncate?
                          // Let's skip to avoid partial drawing issues.
                          // If truncation is needed, it's complex due to ANSI/UTF-8.
                          continue; // Skip this right-aligned segment
                      }
                 } else {
                     // No space left at all, stop trying to render right segments
                     break;
                 }
            }
        }

        // Reset colors at the very end of the line drawing
        applyThemeDefaultColor(ab);

        // Free collected segment data for this line
        free(line_segments); // Free memory allocated for this line's segments

        // Add newline separator BETWEEN lines for multi-line status bars
        // But NOT after the very last line.
        if (line_idx < statusbar_height - 1) {
            abAppend(ab, "\r\n", 2);
        }

    } // --- End loop through status bar lines ---

    // Pop Lua return values (segments table and potentially options table)
    lua_pop(L, has_options ? 2 : 1);
}


// (editorDrawMessageBar remains mostly the same, ensures color reset)
void editorDrawMessageBar(struct abuf *ab) {
    // Set message bar colors (or use defaults)
    char *msg_fg = E.theme.ui_message_fg ? E.theme.ui_message_fg : E.theme.ui_status_fg; // Fallback to status fg
    char *msg_bg = E.theme.ui_message_bg ? E.theme.ui_message_bg : E.theme.ui_status_bg; // Fallback to status bg

    applyTrueColor(ab, msg_fg, msg_bg);
    abAppend(ab, "\x1b[K", 3); // Clear line with message bar colors

    int msglen = 0;
    // Check message validity and expiration (assuming E.statusmsg_time exists)
    if (E.statusmsg[0] != '\0' && time(NULL) - E.statusmsg_time < 5) {
         // Calculate visual length of the message using the new function
         msglen = calculate_visible_length_ansi(E.statusmsg);

         if (msglen > E.screencols) {
             // Simple truncation by bytes (might cut multi-byte chars or ANSI)
             // TODO: Implement proper truncation aware of UTF-8 and ANSI if needed
             msglen = E.screencols; // Limit byte length for now
             // A better approach would truncate based on visual width, not bytes.
             char truncated_msg[E.screencols + 1]; // Temp buffer
             strncpy(truncated_msg, E.statusmsg, E.screencols);
             truncated_msg[E.screencols] = '\0';
             abAppend(ab, truncated_msg, strlen(truncated_msg));

         } else {
             // Message fits, append the whole thing
             abAppend(ab, E.statusmsg, strlen(E.statusmsg));
         }
         // Fill remaining space on the message line with background color
         int padding = E.screencols - msglen;
         if (padding < 0) padding = 0;
         for(int i = 0; i < padding; ++i) abAppend(ab, " ", 1);

    } else {
        // No message or expired, line is already cleared by \x1b[K
        // Optionally display default help message here?
    }
    // Reset colors after message bar is drawn
    applyThemeDefaultColor(ab);
}


// (editorRefreshScreen remains the same, calls the updated draw functions)
void editorRefreshScreen() {
    editorScroll(); // Update scroll offsets based on cursor position

    struct abuf ab = ABUF_INIT; // Initialize the append buffer for drawing

    // Hide cursor, move to top-left
    abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
    abAppend(&ab, "\x1b[H", 3);    // Go to home position (1,1)

    // Draw components
    editorDrawRows(&ab);          // Draw file content/line numbers
    editorDrawStatusBar(&ab);     // Draw the status bar (Lua or default C)
    editorDrawMessageBar(&ab);    // Draw the message bar

    // Position cursor at its logical position in the editor window
    char buf[32];
    // Calculate screen row/col based on file row/col and offsets
    int screen_row = (E.cy - E.rowoff) + 1;
    int screen_col = (E.rx - E.coloff) + KILO_LINE_NUMBER_WIDTH + 1; // Add line number width
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", screen_row, screen_col);
    abAppend(&ab, buf, strlen(buf));

    // Show cursor again
    abAppend(&ab, "\x1b[?25h", 6); // Show cursor

    // Write the entire buffer to standard output
    // Error handling for write is important in a real application
    if (write(STDOUT_FILENO, ab.b, ab.len) == -1) {
         perror("editorRefreshScreen: write error");
         // Handle error appropriately, maybe attempt recovery or exit
    }

    abFree(&ab); // Free the append buffer memory
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  // Use vsnprintf for safety against buffer overflows
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL); // Record the time the message was set
}

// (editorClearStatusMessage remains the same, or provide a default message)
void editorClearStatusMessage() {
    // Set a default message or clear it
    // editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
     E.statusmsg[0] = '\0'; // Or simply clear the message
}