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
    size_t new_size = ab->len + len;
    char *new_buf = realloc(ab->b, new_size);
    // char *new_buf = realloc(ab->b, ab->len + len);
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
// Draws rows within the specified text area boundaries
void editorDrawRows(struct abuf *ab, int text_area_start_row, int text_area_start_col, int text_area_height, int text_area_width) {
    int y;
    // Loop for the number of rows available in the calculated text area height
    for (y = 0; y < text_area_height; y++) {
        int filerow = y + E.rowoff; // Calculate the actual file row index
        int screen_row = text_area_start_row + y; // Calculate the absolute screen row

        // --- Position Cursor and Clear Line within Bounds ---
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", screen_row, text_area_start_col);
        abAppend(ab, pos_buf, strlen(pos_buf));

        // Apply background and clear *only* the width of the text area
        applyTrueColor(ab, NULL, E.theme.ui_background_bg); // Use theme background
        for (int k = 0; k < text_area_width; ++k) {
            abAppend(ab, " ", 1);
        }
        // Reposition cursor back to start column for this line after clearing
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", screen_row, text_area_start_col);
        abAppend(ab, pos_buf, strlen(pos_buf));

        // --- Draw Line Number (at the start of the text area) ---
        int ln_width = KILO_LINE_NUMBER_WIDTH;
        if (ln_width > text_area_width) ln_width = 0; // Disable if no space

        if (ln_width > 0) {
            char linenum[32];
            if (filerow < E.numrows) {
                // Line numbers for actual file lines
                snprintf(linenum, sizeof(linenum), "%*d ", ln_width - 1, filerow + 1);
                applyTrueColor(ab, E.theme.ui_lineno_fg, E.theme.ui_background_bg);
                abAppend(ab, linenum, strlen(linenum));
            } else {
                // Tildes or welcome message padding
                snprintf(linenum, sizeof(linenum), "%*s", ln_width, ""); // Padding
                // Show tilde only in the rows corresponding to empty lines after file end OR empty file screen
                if (y >= E.numrows - E.rowoff || E.numrows == 0) {
                    // Don't show tilde on the welcome message line itself if customizing
                     if (!(E.numrows == 0 && y == text_area_height / 3)) {
                         linenum[0] = '~';
                     }
                } // Else: leave blank space
                applyTrueColor(ab, E.theme.ui_tilde_fg, E.theme.ui_background_bg);
                abAppend(ab, linenum, strlen(linenum));
            }
        }
        // Reset color before drawing content
        applyThemeDefaultColor(ab);

        // --- Calculate Content Area ---
        int content_start_col_abs = text_area_start_col + ln_width;
        int content_available_width = text_area_width - ln_width;
        if (content_available_width < 0) content_available_width = 0;

        // Reposition cursor after line number (if drawn)
        if (ln_width > 0) { // Only reposition if line numbers took space
             snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", screen_row, content_start_col_abs);
             abAppend(ab, pos_buf, strlen(pos_buf));
        }
        // Ensure default colors for content area
        applyTrueColor(ab, E.theme.hl_normal_fg, E.theme.ui_background_bg);


        // --- Draw Row Content / Welcome Message ---
        if (filerow >= E.numrows) {
            // Draw Welcome Message (only if file is empty)
            if (E.numrows == 0 && y == text_area_height / 3 && content_available_width > 0) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > content_available_width) welcomelen = content_available_width;

                int padding = (content_available_width - welcomelen) / 2;
                if (padding > 0) {
                    for (int p = 0; p < padding; ++p) abAppend(ab, " ", 1);
                }
                abAppend(ab, welcome, welcomelen);
                // Fill rest of welcome line with spaces?
                // for (int p = padding + welcomelen; p < content_available_width; ++p) abAppend(ab, " ", 1);
            }
             // Tilde drawing is handled in the line number section
        } else {
            // Draw Actual File Content
            erow *row = &E.row[filerow];
            int len = row->rsize - E.coloff; // Content to draw based on horizontal scroll
            if (len < 0) len = 0;
            // Clip length to the available width in the content area
            if (len > content_available_width) len = content_available_width;

            if (len > 0) {
                char *c = &row->render[E.coloff];
                unsigned char *hl = &row->hl[E.coloff];
                int current_applied_hl = -1;

                for (int j = 0; j < len; j++) {
                    if (iscntrl(c[j])) {
                        // Handle Control Chars (draw inverted)
                        char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                        // Ensure default background for inverted char
                        applyTrueColor(ab, E.theme.hl_normal_fg, E.theme.ui_background_bg);
                        abAppend(ab, "\x1b[7m", 4); // Inverse video
                        abAppend(ab, &sym, 1);
                        abAppend(ab, "\x1b[m", 3);  // Reset all attributes
                        current_applied_hl = -1; // Force color re-application next
                    } else if (hl[j] != current_applied_hl) {
                        // Apply Syntax Highlighting Color Change
                         current_applied_hl = hl[j];
                         char *fg = NULL, *bg = E.theme.ui_background_bg; // Default to area background
                         // Switch statement mapping hl[j] to fg/bg from E.theme...
                         switch (hl[j]) {
                             case HL_COMMENT:   fg = E.theme.hl_comment_fg;   /* bg = E.theme.hl_comment_bg; */   break; // Use theme BG or default?
                             case HL_MLCOMMENT: fg = E.theme.hl_mlcomment_fg; /* bg = E.theme.hl_mlcomment_bg; */ break;
                             case HL_KEYWORD1:  fg = E.theme.hl_keyword1_fg;  /* bg = E.theme.hl_keyword1_bg; */  break;
                             case HL_KEYWORD2:  fg = E.theme.hl_keyword2_fg;  /* bg = E.theme.hl_keyword2_bg; */  break;
                             case HL_KEYWORD3:  fg = E.theme.hl_keyword3_fg;  /* bg = E.theme.hl_keyword3_bg; */  break;
                             case HL_TYPE:      fg = E.theme.hl_type_fg;      /* bg = E.theme.hl_type_bg; */      break;
                             case HL_BUILTIN:   fg = E.theme.hl_builtin_fg;   /* bg = E.theme.hl_builtin_bg; */   break;
                             case HL_STRING:    fg = E.theme.hl_string_fg;    /* bg = E.theme.hl_string_bg; */    break;
                             case HL_NUMBER:    fg = E.theme.hl_number_fg;    /* bg = E.theme.hl_number_bg; */    break;
                             case HL_MATCH:     fg = E.theme.hl_match_fg;     bg = E.theme.hl_match_bg;     break; // Match often uses explicit BG
                             case HL_NORMAL:
                             default:           fg = E.theme.hl_normal_fg;    bg = E.theme.ui_background_bg;    break;
                         }
                         // Use resolved background only if explicitly set by theme, else default area bg
                         char* final_bg = (bg && strcmp(bg, E.theme.ui_background_bg) != 0) ? bg : E.theme.ui_background_bg;
                         applyTrueColor(ab, fg, final_bg);
                         abAppend(ab, &c[j], 1);
                    } else {
                         // Character has same highlight, just append
                         abAppend(ab, &c[j], 1);
                    }
                }
            }
            // No need to clear rest of line here, done at the start
        }
        // Reset colors before next line potentially (or rely on start of loop)
         applyThemeDefaultColor(ab);
    } // End main loop 'for y'
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
             // This ensures we always move to the next line before the message bar.
     abAppend(ab, "\r\n", 2);

     // Free temporary buffers
     abFree(&sb_left);
     abFree(&sb_right);
}


// Draw status bar using Lua configuration
void editorDrawStatusBar(struct abuf *ab) {
    lua_State *L = getLuaState(); // Assuming getLuaState() is defined

    // Ensure statusbar_callback_ref is defined and initialized
    if (!L || statusbar_callback_ref == LUA_NOREF) {
        // showDebugOnError("Failed to draw status bar: Lua not ready or no callback"); // Optional debug
        editorDrawDefaultStatusBar(ab); // Fallback
        return;
    }

    // --- Call Lua Function ---
    lua_rawgeti(L, LUA_REGISTRYINDEX, statusbar_callback_ref);
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf(error_msg ? error_msg : "unknown error in status bar Lua function");
        lua_pop(L, 1); // Pop error message
        editorDrawDefaultStatusBar(ab); // Fallback on error
        return;
    }

    // --- Parse Lua Return Values ---
    int segments_idx = -2;
    int options_idx = -1;
    int has_options = 0;
    int top = lua_gettop(L);

    if (top >= 1 && lua_istable(L, -1)) {
        if (top >= 2 && lua_istable(L, -2)) {
            segments_idx = -2; options_idx = -1; has_options = 1;
        } else {
            segments_idx = -1; options_idx = 0; has_options = 0;
        }
    } else {
        debug_printf("Failed to draw status bar: Must return a table of segments");
        lua_pop(L, top); // Clear the stack
        editorDrawDefaultStatusBar(ab);
        return;
    }

    // --- Default & Parsed Options ---
    int statusbar_height = 1;
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

    if (has_options) {
        lua_getfield(L, options_idx, "height");
        if (lua_isnumber(L, -1)) {
            statusbar_height = lua_tointeger(L, -1);
            if (statusbar_height < 1) statusbar_height = 1;
            if (statusbar_height > 5) statusbar_height = 5; // Limit max height
        }
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "bg");
        if (lua_isstring(L, -1)) {
            const char *n = lua_tostring(L, -1);
            char* t = getThemeColorByName(n); // Check theme map first
            if(t) status_bg = t; else if(n[0] == '#') status_bg = (char*)n; // Allow direct hex
        }
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "fg");
        if (lua_isstring(L, -1)) {
            const char *n = lua_tostring(L, -1);
            char* t = getThemeColorByName(n);
            if(t) status_fg = t; else if(n[0] == '#') status_fg = (char*)n;
        }
        lua_pop(L, 1);
    }

    // Clamp statusbar height based on available screen rows
    if (statusbar_height >= E.screenrows) {
        statusbar_height = E.screenrows - 1;
        if (statusbar_height < 1) statusbar_height = 1; // Ensure at least 1 if possible
    }

    // Define structure for collected segment data
    typedef struct {
        const char *text; // Pointer to Lua string (includes ANSI)
        char *fg;
        char *bg;
        int width;       // Visual width (ANSI stripped)
        int alignment;   // 0=left, 1=center(unused), 2=right
    } SegmentData;

    // --- Loop Through Each Status Bar Line ---
    for (int line_idx = 0; line_idx < statusbar_height; line_idx++) {

        // --- Collect Segments for this specific line ---
        lua_Integer segment_count_raw = lua_rawlen(L, segments_idx);
        if (segment_count_raw > INT_MAX || segment_count_raw < 0) { // Added check for negative result
             debug_printf("Failed to draw status bar: Invalid segment count from Lua");
             segment_count_raw = 0; // Treat as zero segments
        }
        int segment_count = (int)segment_count_raw;

        // Use dynamic allocation, check for failure
        SegmentData *line_segments = NULL; // Initialize to NULL
        if (segment_count > 0) { // Avoid malloc(0) if Lua returns empty table
             line_segments = malloc(sizeof(SegmentData) * segment_count);
             if (!line_segments) {
                 debug_printf("Failed to draw status bar: Status bar segment memory allocation failed");
                 // Clean up Lua stack before returning
                 lua_pop(L, has_options ? 2 : 1);
                 editorDrawDefaultStatusBar(ab); // Attempt fallback drawing
                 return; // Exit the function
             }
        }

        int line_segment_idx = 0; // Index within line_segments for the current line
        int total_left_width = 0;
        int total_right_width = 0;

        // Iterate through all segments returned by Lua to collect data for the current line
        for (int i = 1; i <= segment_count; i++) {
            lua_rawgeti(L, segments_idx, i);
            if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; } // Skip non-tables

            // Check segment line number
            int segment_line_num = 0; // Default to line index 0 (first line)
            lua_getfield(L, -1, "line");
            if (lua_isnumber(L, -1)) segment_line_num = lua_tointeger(L, -1) - 1; // Lua is 1-based
            lua_pop(L, 1);

            // Only collect segments for the current line being drawn
            if (segment_line_num != line_idx) { lua_pop(L, 1); continue; }

            // Get mandatory segment text
            const char *text = NULL;
            lua_getfield(L, -1, "text");
            if (lua_isstring(L, -1)) text = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!text) { lua_pop(L, 1); continue; } // Skip segments without text

            // Get optional fg/bg colors
            const char *fg_name = NULL, *bg_name = NULL;
            lua_getfield(L, -1, "fg"); if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "bg"); if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1); lua_pop(L, 1);

            // Resolve colors
            char* fg_color = status_fg; // Start with line default
            if (fg_name) { char* t = getThemeColorByName(fg_name); if(t) fg_color = t; else if(fg_name[0] == '#') fg_color = (char*)fg_name; }
            char* bg_color = status_bg; // Start with line default
            if (bg_name) { char* t = getThemeColorByName(bg_name); if(t) bg_color = t; else if(bg_name[0] == '#') bg_color = (char*)bg_name; }

            // Get optional alignment
            int alignment = 0; // Default left
            lua_getfield(L, -1, "align");
            if (lua_isstring(L, -1)) { if (strcmp(lua_tostring(L, -1), "right") == 0) alignment = 2; }
            lua_pop(L, 1);

            // Calculate visual width (ensure calculate_visible_length_ansi exists and works correctly)
            // ASSUMPTION: calculate_visible_length_ansi handles NULL input gracefully (returns 0).
            int visual_width = calculate_visible_length_ansi(text);

            // Store segment data for rendering pass (check index bounds)
            // line_segment_idx should be < segment_count if malloc succeeded and loop runs
            line_segments[line_segment_idx].text = text;
            line_segments[line_segment_idx].fg = fg_color;
            line_segments[line_segment_idx].bg = bg_color;
            line_segments[line_segment_idx].width = visual_width;
            line_segments[line_segment_idx].alignment = alignment;

            // Accumulate widths based on alignment
            if (alignment == 0) total_left_width += visual_width;
            else if (alignment == 2) total_right_width += visual_width;

            line_segment_idx++;

            lua_pop(L, 1); // Pop the segment table
        } // --- End segment data collection loop ---

        // --- Render the status line using collected data ---
        applyTrueColor(ab, status_fg, status_bg); // Set default background/foreground for the line
        abAppend(ab, "\x1b[K", 3);               // Clear line with these default colors

        int left_render_end_col = 0; // Track visual column position *after* rendering left segments (0-based)

        // Render Left-Aligned Segments
        if (line_segments) { // Check if segments were allocated
             for (int i = 0; i < line_segment_idx; i++) {
                 if (line_segments[i].alignment == 0) { // Left aligned
                     if (left_render_end_col + line_segments[i].width <= E.screencols) {
                         applyTrueColor(ab, line_segments[i].fg, line_segments[i].bg);
                         abAppend(ab, line_segments[i].text, strlen(line_segments[i].text));
                         left_render_end_col += line_segments[i].width;
                     } else { break; } // Stop if segment doesn't fit
                 }
             }
        }

        // Render Right-Aligned Segments using absolute positioning
        if (line_segments && total_right_width > 0 && E.screencols > 0) {
            // Calculate target start column (1-based for \x1b[...G)
            int right_start_col = E.screencols - total_right_width + 1;

            // Prevent overlap: only draw if right would start AFTER where left ended
            if (right_start_col > left_render_end_col) {

                // Move cursor to the calculated starting position
                char pos_buf[32];
                snprintf(pos_buf, sizeof(pos_buf), "\x1b[%dG", right_start_col);
                abAppend(ab, pos_buf, strlen(pos_buf));

                int current_right_col = right_start_col; // Track columns for right segments (1-based)

                for (int i = 0; i < line_segment_idx; i++) {
                    if (line_segments[i].alignment == 2) { // Right aligned
                        // Check if segment fits: end column <= screen width
                        if (current_right_col + line_segments[i].width - 1 <= E.screencols) {
                            applyTrueColor(ab, line_segments[i].fg, line_segments[i].bg);
                            abAppend(ab, line_segments[i].text, strlen(line_segments[i].text));
                            current_right_col += line_segments[i].width;
                        } else { break; } // Stop if segment doesn't fit
                    }
                }
            } // else: Right segments would overlap or touch left segments, skipped.
        }

        // Reset colors at the very end of the line drawing (maybe redundant if applyThemeDefaultColor does it)
        // applyTrueColor(ab, status_fg, status_bg); // Or just reset fully:
        applyThemeDefaultColor(ab); // Should reset SGR attributes

        // Free collected segment data for this line (if allocated)
        free(line_segments); // free(NULL) is safe.

    } // --- End loop through status bar lines ---

    // Add required newline after the status bar content (before message bar)
    abAppend(ab, "\r\n", 2);

    // Pop Lua return values from the stack
    lua_pop(L, has_options ? 2 : 1);

    // applyThemeDefaultColor(ab); // Usually done AFTER the message bar in editorRefreshScreen
}


/**
 * @brief Draws the tabline showing open buffers at the top of the screen.
 * @param ab The append buffer to draw into.
 */
void editorDrawDefaultTabline(struct abuf *ab) {
    // Set default colours for the tabline
    char *tab_bg = E.theme.ui_status_bg ? E.theme.ui_status_bg : "#282828";
    char *tab_fg = E.theme.ui_status_fg ? E.theme.ui_status_fg : "#ebdbb2";
    char *active_tab_bg = E.theme.ui_status_mode_bg ? E.theme.ui_status_mode_bg : "#98971a"; // Green bg
    char *active_tab_fg = E.theme.ui_status_mode_fg ? E.theme.ui_status_mode_fg : "#000000"; // Black text

    // Clear the line with background colour
    applyTrueColor(ab, tab_fg, tab_bg);
    abAppend(ab, "\x1b[K", 3);

    // If no buffers, just show empty tabline
    if (E.num_buffers == 0) {
        abAppend(ab, " [No Buffers] ", 14);
        applyThemeDefaultColor(ab); // Reset color before newline
        abAppend(ab, "\r\n", 2);
        return;
    }

    // int current_pos = 0; // REMOVE THIS LINE - Unused variable
    int tab_width = 20; // Default width for each tab

    // Calculate available width and adjust tab width if needed
    int available_width = E.screencols;
    if (E.num_buffers * tab_width > available_width) {
        tab_width = available_width / E.num_buffers;
        if (tab_width < 10) tab_width = 10; // Minimum reasonable tab width
    }

    // Loop through all buffers and draw tabs
    editorBuffer *buf = E.buffer_list_head;
    int i = 1;

    // DECLARE counter and limit BEFORE the loop vvv
    int tab_loop_count = 0;
    const int MAX_TAB_LOOPS = E.num_buffers + 5; // Set a reasonable limit (e.g., slightly more than expected buffers)

    while (buf != NULL) {
        // Check loop counter (increment BEFORE check)
        if (++tab_loop_count > MAX_TAB_LOOPS) {
             fprintf(stderr, "\nERROR: Exceeded MAX_TAB_LOOPS in editorDrawTabline! List corrupted?\n");
             fflush(stderr); // Make sure error message is printed immediately
             // Display an error tab instead of crashing?
             applyTrueColor(ab, "#ffffff", "#ff0000"); // White on Red for error
             abAppend(ab, " ERROR ", 7);
             applyTrueColor(ab, tab_fg, tab_bg); // Reset to default tab colors
             break; // Stop the loop
        }

        // Determine if this is the active buffer
        int is_active = (buf == E.current_buffer);

        // Set appropriate colours
        if (is_active) {
            applyTrueColor(ab, active_tab_fg, active_tab_bg);
        } else {
            applyTrueColor(ab, tab_fg, tab_bg);
        }

        // Extract filename (basename) from the buffer
        const char *filename = buf->filename ? findBasename(buf->filename) : "[No Name]";

        // Format buffer tab string (with index and dirty indicator)
        char tab_str[64]; // Increased buffer size slightly
        int prefix_len = snprintf(tab_str, sizeof(tab_str), " %d:", i);
        int max_filename_len = tab_width - prefix_len - 3; // Space for prefix, " + ", padding space
        if (max_filename_len < 1) max_filename_len = 1; // Ensure at least 1 char for name

        char truncated_name[max_filename_len + 1];
        strncpy(truncated_name, filename, max_filename_len);
        truncated_name[max_filename_len] = '\0';
        if (strlen(filename) > max_filename_len && max_filename_len > 3) {
             // Add ellipsis if truncated and enough space
             strcpy(truncated_name + max_filename_len - 3, "...");
        }

        int current_len = snprintf(tab_str + prefix_len, sizeof(tab_str) - prefix_len,
                                  "%s%s ", truncated_name, buf->dirty ? "+" : "");

        int total_len = prefix_len + current_len;

        // Append the formatted tab string
        abAppend(ab, tab_str, total_len);

        // Fill remaining space in the tab slot
        // Note: Using byte length `total_len`. If using multi-byte chars, a visual width calculation would be more accurate.
        int remaining_width = tab_width - total_len;
        if (remaining_width < 0) remaining_width = 0;

        for (int k = 0; k < remaining_width; k++) {
             abAppend(ab, " ", 1); // Fill with spaces using current tab color
        }

        i++; // Increment buffer index counter

        // MOVE TO NEXT BUFFER **INSIDE** THE LOOP vvv
        buf = buf->next;
    } // <--- End of while loop brace

    // Reset colors and add newline after drawing all tabs (or breaking from error)
    applyThemeDefaultColor(ab);
    abAppend(ab, "\r\n", 2);
}

/**
 * @brief Draws the tabline, attempting to use Lua configuration first.
 * Parses segment tables returned by Lua, similar to editorDrawStatusBar.
 * Falls back to C implementation if Lua is unavailable or fails.
 * @param ab The append buffer to draw into.
 */
void editorDrawTabline(struct abuf *ab) {
    typedef struct {
    const char *text; // Pointer to Lua string (valid during render)
    char *fg;         // Resolved color string
    char *bg;         // Resolved color string
    int width;        // Calculated visual width
    } TabSegmentData;

    lua_State *L = getLuaState();

    // Check if Lua is configured and ready for tabline
    if (!L || tabline_callback_ref == LUA_NOREF || tabline_callback_ref == LUA_REFNIL) {
        editorDrawDefaultTabline(ab); // Use C implementation
        return;
    }

    // --- Call Lua Function ---
    lua_rawgeti(L, LUA_REGISTRYINDEX, tabline_callback_ref);

    // Call Lua function: 0 arguments, expect 2 return values (segments, options)
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf(error_msg ? error_msg : "unknown error in tabline Lua function");
        lua_pop(L, 1); // Pop error message
        editorDrawDefaultTabline(ab); // Fallback on error
        return;
    }

    // --- Parse Lua Return Values ---
    // Use similar logic as status bar to find segments/options tables on stack
    int segments_idx = -2;
    int options_idx = -1;
    int has_options = 0;
    int top = lua_gettop(L);

    if (top >= 1 && lua_istable(L, -1)) { // Check top value (options)
        if (top >= 2 && lua_istable(L, -2)) { // Check value below (segments)
            segments_idx = -2; options_idx = -1; has_options = 1;
        } else { // Only one table returned, assume it's segments
            segments_idx = -1; options_idx = 0; has_options = 0;
        }
    } else {
        debug_printf("Tabline Lua function must return a table (list) of segments");
        lua_pop(L, top); // Clear the stack
        editorDrawDefaultTabline(ab);
        return;
    }

    // --- Default & Parsed Options ---
    // Get defaults from theme or hardcoded values
    char *default_tab_bg = E.theme.ui_status_bg ? E.theme.ui_status_bg : "#282828";
    char *default_tab_fg = E.theme.ui_status_fg ? E.theme.ui_status_fg : "#ebdbb2";
    // int tabline_height = 1; // Default height (might parse from options)

    if (has_options) {
        // Optionally parse "height", "bg", "fg" from options_idx table
        // Remember to pop values retrieved from the options table.
        // Example for background:
        lua_getfield(L, options_idx, "bg");
        if (lua_isstring(L, -1)) {
            const char *n = lua_tostring(L, -1);
            char* t = getThemeColorByName(n);
            if(t) default_tab_bg = t; else if(n[0] == '#') default_tab_bg = (char*)n;
        }
        lua_pop(L, 1);
        // Add similar for "fg", "height" if needed
    }

    // --- Collect Segment Data ---
    lua_Integer segment_count_raw = lua_rawlen(L, segments_idx);
    if (segment_count_raw > INT_MAX || segment_count_raw < 0) {
        debug_printf("Invalid segment count from Lua for tabline");
        segment_count_raw = 0;
    }
    int segment_count = (int)segment_count_raw;

    TabSegmentData *tabs = NULL; // Array to hold parsed C data
    if (segment_count > 0) {
        tabs = malloc(sizeof(TabSegmentData) * segment_count);
        if (!tabs) {
            debug_printf("Failed to allocate memory for tabline segments");
            lua_pop(L, has_options ? 2 : 1); // Pop Lua return values before falling back
            editorDrawDefaultTabline(ab);
            return;
        }
    }

    int collected_tab_count = 0; // Track valid segments collected
    for (int i = 1; i <= segment_count; i++) {
        lua_rawgeti(L, segments_idx, i);
        if (!lua_istable(L, -1)) { // *** Check if element is a TABLE ***
            debug_printf("Tabline Lua function returned non-table element at index %d", i);
            lua_pop(L, 1); continue; // Skip this element
        }

        // Get mandatory segment text
        const char *text = NULL;
        lua_getfield(L, -1, "text");
        if (lua_isstring(L, -1)) text = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!text) {
            debug_printf("Tabline segment at index %d missing 'text' field", i);
            lua_pop(L, 1); continue; // Skip segment without text
        }

        // Get optional fg/bg colors
        const char *fg_name = NULL, *bg_name = NULL;
        lua_getfield(L, -1, "fg"); if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "bg"); if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1); lua_pop(L, 1);

        // Resolve colors using tabline defaults
        char* fg_color = default_tab_fg;
        if (fg_name) { char* t = getThemeColorByName(fg_name); if(t) fg_color = t; else if(fg_name[0] == '#') fg_color = (char*)fg_name; }
        char* bg_color = default_tab_bg;
        if (bg_name) { char* t = getThemeColorByName(bg_name); if(t) bg_color = t; else if(bg_name[0] == '#') bg_color = (char*)bg_name; }

        // Calculate visual width
        int visual_width = calculate_visible_length_ansi(text);

        // Store parsed segment data
        // NOTE: `tabs[collected_tab_count].text` points to Lua's memory.
        // This is okay for immediate rendering, but don't store it long-term without copying.
        tabs[collected_tab_count].text = text;
        tabs[collected_tab_count].fg = fg_color;
        tabs[collected_tab_count].bg = bg_color;
        tabs[collected_tab_count].width = visual_width;
        collected_tab_count++;

        lua_pop(L, 1); // Pop the segment table
    } // --- End segment data collection loop ---


    // --- Render the Tabline Line ---
    applyTrueColor(ab, default_tab_fg, default_tab_bg);
    abAppend(ab, "\x1b[K", 3); // Clear line with default background

    int current_visual_width = 0;
    if (tabs) { // Check if tabs array was allocated
        for (int i = 0; i < collected_tab_count; i++) {
            // Check if segment fits on screen
            if (current_visual_width + tabs[i].width <= E.screencols) {
                applyTrueColor(ab, tabs[i].fg, tabs[i].bg);
                // Use strlen to get byte length for abAppend
                abAppend(ab, tabs[i].text, strlen(tabs[i].text));
                current_visual_width += tabs[i].width;
            } else {
                // Not enough space, stop rendering segments
                break;
            }
        }
    }

    // Optionally: Fill remaining space on the line with default background
    applyTrueColor(ab, default_tab_fg, default_tab_bg);
    while (current_visual_width < E.screencols) {
        abAppend(ab, " ", 1);
        current_visual_width++;
    }

    // Clean up allocated C memory and Lua stack
    free(tabs); // free(NULL) is safe
    lua_pop(L, has_options ? 2 : 1); // Pop Lua return values (segments table, options table)

    // Reset terminal colors and add newline
    applyThemeDefaultColor(ab);
    abAppend(ab, "\r\n", 2);
}


// Draws the panel within a fixed rectangle passed as arguments
// Called by editorRefreshScreen when panel is registered and E.panel_mode is LEFT/RIGHT
void editorDrawDirTreeFixed(struct abuf *ab, int panel_x, int panel_y, int panel_w, int panel_h) {
    // Basic checks - Use the *global* callback ref
    if (!E.panel_visible || dirtree_callback_ref == LUA_NOREF || dirtree_callback_ref == LUA_REFNIL) {
        return;
    }
    lua_State *L = getLuaState();
    if (!L) return;

    // --- Call Lua ---
    lua_rawgeti(L, LUA_REGISTRYINDEX, dirtree_callback_ref);
    // 0 args, expect 1 return: segments table
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf("Fixed Panel Lua Error: %s", error_msg ? error_msg : "unknown");
        lua_pop(L, 1);
        return;
     }

    // --- Process Segments ---
    if (!lua_istable(L, -1)) {
        debug_printf("Fixed Panel Lua function must return a table of segments");
        lua_pop(L, 1);
        return;
    }

    // Get default colors
    char* panel_default_fg = E.theme.hl_normal_fg ? E.theme.hl_normal_fg : "#ffffff"; // Provide fallbacks
    char* panel_default_bg = E.theme.ui_background_bg ? E.theme.ui_background_bg : "#000000";

    // Clear panel area background (optional but recommended for fixed panels)
    applyTrueColor(ab, panel_default_fg, panel_default_bg);
    for (int r = 0; r < panel_h; ++r) {
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", panel_y + r, panel_x);
        abAppend(ab, pos_buf, strlen(pos_buf));
        for (int c = 0; c < panel_w; ++c) abAppend(ab, " ", 1);
    }

    // Render segments sequentially top-to-bottom
    lua_Integer segment_count_raw = lua_rawlen(L, -1);
    int segment_count = (segment_count_raw > 0 && segment_count_raw < INT_MAX) ? (int)segment_count_raw : 0;
    int current_panel_line = 0; // Relative line within panel (0-based)

    for (int i = 1; i <= segment_count; i++) {
        // Stop if panel height reached
        if (current_panel_line >= panel_h) break;

        lua_rawgeti(L, -1, i); // Get segment table at index i from segments table at top stack (-1)
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; } // Skip non-table segment

        // Get text (Mandatory)
        const char *text = NULL;
        lua_getfield(L, -1, "text");
        if (lua_isstring(L, -1)) text = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!text) { lua_pop(L, 1); continue; } // Skip segment without text

        // Get optional fg/bg
        const char *fg_name = NULL, *bg_name = NULL;
        lua_getfield(L, -1, "fg"); if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "bg"); if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1); lua_pop(L, 1);

        // Resolve colors
        char* fg_color = panel_default_fg;
        char* bg_color = panel_default_bg;
        // Assuming getThemeColorByName helper exists
        if (fg_name) { char* t = getThemeColorByName(fg_name); if(t) fg_color = t; else if(fg_name[0] == '#') fg_color = (char*)fg_name; }
        if (bg_name) { char* t = getThemeColorByName(bg_name); if(t) bg_color = t; else if(bg_name[0] == '#') bg_color = (char*)bg_name; }

        // --- Draw Segment Sequentially ---
        int abs_row = panel_y + current_panel_line;
        int abs_col = panel_x;
        int available_width = panel_w;

        // Position Cursor
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", abs_row, abs_col);
        abAppend(ab, pos_buf, strlen(pos_buf));
        // Set Colors
        applyTrueColor(ab, fg_color, bg_color);
        // Append Text (Clip)
        int text_len = strlen(text);
        int len_to_draw = (text_len > available_width) ? available_width : text_len;
        if (len_to_draw < 0) len_to_draw = 0;
        abAppend(ab, text, len_to_draw);
        // Clear rest of line within panel (optional)
        // applyTrueColor(ab, panel_default_fg, panel_default_bg);
        // for(int k=len_to_draw; k < available_width; ++k) abAppend(ab, " ", 1);


        current_panel_line++; // Move to next line for next segment
        lua_pop(L, 1); // Pop segment table value
    } // End segment loop

    // --- Cleanup ---
    lua_pop(L, 1); // Pop the main segments table
    applyThemeDefaultColor(ab); // Reset color
}

// Draws the panel as a floating overlay
// Called by the overlay system loop in editorRefreshScreen
void editorDrawDirTreeFloating(struct abuf *ab, void *state /* DirTreeState* */) {
    // Use the state parameter (mark as used for now)
    DirTreeState *pstate = (DirTreeState*)state;
    (void)pstate; // Suppress unused warning until pstate fields are used

    // Basic checks - Use the global dirtree_callback_ref
    lua_State *L = getLuaState();
    if (!E.panel_visible || !L || dirtree_callback_ref == LUA_NOREF || dirtree_callback_ref == LUA_REFNIL) {
        return;
    }

    // --- Call Lua ---
    lua_rawgeti(L, LUA_REGISTRYINDEX, dirtree_callback_ref);
    // 0 args, expect 2 returns: segments, options (layout needed here)
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf("Floating Panel Lua Error: %s", error_msg ? error_msg : "unknown");
        lua_pop(L, 1); // Pop error message
        return;
    }

    // --- Parse Segments & Options ---
    int segments_idx = -2, options_idx = -1, has_options = 0;
    int top = lua_gettop(L);
    // Find segments (mandatory) and options (mandatory for float) tables
    if (top >= 2 && lua_istable(L, -1) && lua_istable(L, -2)) {
        segments_idx = -2; options_idx = -1; has_options = 1; // Correct order found
    } else {
         debug_printf("Floating Panel Lua function must return segments and options tables");
         lua_pop(L, top); // Clear stack
         return;
    }

    // --- Determine Layout (Always Floating) ---
    // Get layout from Lua options or use defaults
    int panel_w = E.screencols / 3;
    int panel_h = E.screenrows / 2; // Use calculated text area height for default
    int panel_x = (E.screencols - panel_w) / 2 + 1;
    // ** CORRECTED DEFAULT Y ** Use E.content_start_row
    int panel_y = E.content_start_row + (E.screenrows - panel_h) / 2;

    // Defaults for colors
    char* panel_default_fg = E.theme.hl_normal_fg ? E.theme.hl_normal_fg : "#ffffff";
    char* panel_default_bg = E.theme.ui_background_bg ? E.theme.ui_background_bg : "#000000";

    // Read layout from options table using options_idx
    lua_getfield(L, options_idx, "x"); if (lua_isnumber(L, -1)) panel_x = lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, options_idx, "y"); if (lua_isnumber(L, -1)) panel_y = lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, options_idx, "width"); if (lua_isnumber(L, -1)) panel_w = lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, options_idx, "height"); if (lua_isnumber(L, -1)) panel_h = lua_tointeger(L, -1); lua_pop(L, 1);
    // Get bg/fg overrides
    lua_getfield(L, options_idx, "bg"); if (lua_isstring(L, -1)) { const char *n=lua_tostring(L,-1); char *t=getThemeColorByName(n); if(t) panel_default_bg=t; else if(n[0]=='#') panel_default_bg=(char*)n;} lua_pop(L, 1);
    lua_getfield(L, options_idx, "fg"); if (lua_isstring(L, -1)) { const char *n=lua_tostring(L,-1); char *t=getThemeColorByName(n); if(t) panel_default_fg=t; else if(n[0]=='#') panel_default_fg=(char*)n;} lua_pop(L, 1);

    // Clamp/validate x, y, w, h to screen bounds
    if (panel_x < 1) panel_x = 1;
    if (panel_y < 1) panel_y = 1; // Allow drawing anywhere on screen for overlays
    if (panel_w <= 0) panel_w = 1;
    if (panel_h <= 0) panel_h = 1;
    if (panel_x + panel_w > E.screencols + 1) panel_w = E.screencols - panel_x + 1;
    if (panel_y + panel_h > E.total_rows + 1) panel_h = E.total_rows - panel_y + 1;

    // Draw floating panel background/border (optional)
    applyTrueColor(ab, panel_default_fg, panel_default_bg);
    for (int r = 0; r < panel_h; ++r) {
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", panel_y + r, panel_x);
        abAppend(ab, pos_buf, strlen(pos_buf));
        // Fill with spaces - check bounds correctly
        for (int c = 0; c < panel_w; ++c) {
             // Ensure we don't write past screen width (though ANSI might handle it)
             // if (panel_x + c <= E.screencols) {
                   abAppend(ab, " ", 1);
             // }
        }
    }

    // --- Render Segments ---
    lua_Integer segment_count_raw = lua_rawlen(L, segments_idx);
    // ** FIX: Use segment_count in loop **
    int segment_count = (segment_count_raw > 0 && segment_count_raw < INT_MAX) ? (int)segment_count_raw : 0;

    for (int i = 1; i <= segment_count; ++i) { // Use for loop
        lua_rawgeti(L, segments_idx, i); // Get segment table at index i from segments table
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; } // Skip non-tables

        // Get text, fg, bg
        const char *text = NULL; lua_getfield(L, -1, "text"); if (lua_isstring(L, -1)) text = lua_tostring(L, -1); lua_pop(L, 1);
        if (!text) { lua_pop(L, 1); continue; } // Skip segment without text

        const char *fg_name = NULL, *bg_name = NULL;
        lua_getfield(L, -1, "fg"); if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "bg"); if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1); lua_pop(L, 1);

        // Resolve colors
        char* fg_color = panel_default_fg;
        char* bg_color = panel_default_bg; // Use panel's default BG unless overridden
        if (fg_name) { char* t = getThemeColorByName(fg_name); if(t) fg_color = t; else if(fg_name[0] == '#') fg_color = (char*)fg_name; }
        if (bg_name) { char* t = getThemeColorByName(bg_name); if(t) bg_color = t; else if(bg_name[0] == '#') bg_color = (char*)bg_name; }

        // Get relative x/y from segment
        int rel_x = 0, rel_y = 0;
        lua_getfield(L, -1, "x"); if (lua_isnumber(L, -1)) rel_x = lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "y"); if (lua_isnumber(L, -1)) rel_y = lua_tointeger(L, -1); lua_pop(L, 1);

        int abs_row = panel_y + rel_y;
        int abs_col = panel_x + rel_x;
        int available_width = panel_w - rel_x;

        // Check bounds & Draw segment
        if (abs_row >= panel_y && abs_row < panel_y + panel_h &&
            abs_col >= panel_x && abs_col < panel_x + panel_w &&
            available_width > 0) // Check if there's actually space to draw at this col
        {
            char pos_buf[32];
            snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", abs_row, abs_col);
            abAppend(ab, pos_buf, strlen(pos_buf));
            applyTrueColor(ab, fg_color, bg_color);
            int text_len = strlen(text);
            int len_to_draw = (text_len > available_width) ? available_width : text_len;
            if (len_to_draw < 0) len_to_draw = 0;
            abAppend(ab, text, len_to_draw);
        }
        lua_pop(L, 1); // Pop segment table value
    } // End segment loop

    // --- Cleanup ---
    lua_pop(L, 2); // Pop segments table & options table
    applyThemeDefaultColor(ab);
}


// Draws the navigator overlay
// Called by the overlay system loop in editorRefreshScreen
void editorDrawNavigator(struct abuf *ab, void *state /* NavigatorState* */) {
    // Use the state parameter
    NavigatorState *nstate = (NavigatorState*)state;
    (void)nstate; // Mark as used for now, replace with actual usage later

    // Basic checks - Use the global navigator callback ref
    // Check navigator_active flag from E or maybe from nstate->active? Assume E for now.
    if (!E.navigator_active || navigator_callback_ref == LUA_NOREF || navigator_callback_ref == LUA_REFNIL) {
        return;
    }
    lua_State *L = getLuaState();
    if (!L) return;

    // --- Call Lua ---
    lua_rawgeti(L, LUA_REGISTRYINDEX, navigator_callback_ref);
    // 0 args, expect 2 returns: segments, options
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf("Navigator Lua Error: %s", error_msg ? error_msg : "unknown");
        lua_pop(L, 1);
        return;
    }

    // --- Parse Segments & Options ---
    int segments_idx = -2, options_idx = -1, has_options = 0;
    int top = lua_gettop(L);
    // Find segments (mandatory) and options (mandatory) tables
    if (top >= 2 && lua_istable(L, -1) && lua_istable(L, -2)) {
        segments_idx = -2; options_idx = -1; has_options = 1;
    } else {
         debug_printf("Navigator Lua function must return segments and options tables");
         lua_pop(L, top); return;
    }
     if (!lua_istable(L, segments_idx)){ debug_printf("Navigator segments is not a table"); lua_pop(L, top); return; }
     // options_idx should point to the options table now

    // --- Determine Layout (Always Floating) ---
    // Get layout from Lua options or use defaults
    int nav_w = E.screencols * 3 / 4;
    int nav_h = E.screenrows / 2;
    int nav_x = (E.screencols - nav_w) / 2 + 1;
    // ** CORRECTED DEFAULT Y ** Use E.content_start_row
    int nav_y = E.content_start_row + (E.screenrows - nav_h) / 2;

    // Defaults for colors
    char* nav_default_fg = E.theme.hl_normal_fg ? E.theme.hl_normal_fg : "#ffffff";
    char* nav_default_bg = E.theme.ui_background_bg ? E.theme.ui_background_bg : "#1d2021"; // Darker default?

    // Read layout from options table using options_idx
    lua_getfield(L, options_idx, "x"); if (lua_isnumber(L, -1)) nav_x = lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, options_idx, "y"); if (lua_isnumber(L, -1)) nav_y = lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, options_idx, "width"); if (lua_isnumber(L, -1)) nav_w = lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, options_idx, "height"); if (lua_isnumber(L, -1)) nav_h = lua_tointeger(L, -1); lua_pop(L, 1);
    // Get bg/fg overrides
    lua_getfield(L, options_idx, "bg"); if (lua_isstring(L, -1)) { const char *n=lua_tostring(L,-1); char *t=getThemeColorByName(n); if(t) nav_default_bg=t; else if(n[0]=='#') nav_default_bg=(char*)n;} lua_pop(L, 1);
    lua_getfield(L, options_idx, "fg"); if (lua_isstring(L, -1)) { const char *n=lua_tostring(L,-1); char *t=getThemeColorByName(n); if(t) nav_default_fg=t; else if(n[0]=='#') nav_default_fg=(char*)n;} lua_pop(L, 1);

    // Clamp/validate x, y, w, h to screen bounds
    if (nav_x < 1) nav_x = 1;
    if (nav_y < 1) nav_y = 1; // Allow drawing anywhere on screen for overlays
    if (nav_w <= 0) nav_w = 1;
    if (nav_h <= 0) nav_h = 1;
    if (nav_x + nav_w > E.screencols + 1) nav_w = E.screencols - nav_x + 1;
    if (nav_y + nav_h > E.total_rows + 1) nav_h = E.total_rows - nav_y + 1;

    // Draw navigator background/border (optional)
    applyTrueColor(ab, nav_default_fg, nav_default_bg);
    for (int r = 0; r < nav_h; ++r) {
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", nav_y + r, nav_x);
        abAppend(ab, pos_buf, strlen(pos_buf));
        for (int c = 0; c < nav_w; ++c) abAppend(ab, " ", 1);
    }

    // --- Render Segments ---
    lua_Integer segment_count_raw = lua_rawlen(L, segments_idx);
    // ** FIX: Use segment_count in loop **
    int segment_count = (segment_count_raw > 0 && segment_count_raw < INT_MAX) ? (int)segment_count_raw : 0;

    // Use for loop
    for (int i = 1; i <= segment_count; ++i) {
        lua_rawgeti(L, segments_idx, i); // Get segment table at index i
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        // Get text, fg, bg
        const char *text = NULL; lua_getfield(L, -1, "text"); if (lua_isstring(L, -1)) text = lua_tostring(L, -1); lua_pop(L, 1);
        if (!text) { lua_pop(L, 1); continue; }
        const char *fg_name = NULL, *bg_name = NULL;
        lua_getfield(L, -1, "fg"); if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "bg"); if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1); lua_pop(L, 1);

        // Resolve colors
        char* fg_color = nav_default_fg;
        char* bg_color = nav_default_bg;
        if (fg_name) { char* t = getThemeColorByName(fg_name); if(t) fg_color = t; else if(fg_name[0] == '#') fg_color = (char*)fg_name; }
        if (bg_name) { char* t = getThemeColorByName(bg_name); if(t) bg_color = t; else if(bg_name[0] == '#') bg_color = (char*)bg_name; }

        // Get relative x/y from segment
        int rel_x = 0, rel_y = 0;
        lua_getfield(L, -1, "x"); if (lua_isnumber(L, -1)) rel_x = lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "y"); if (lua_isnumber(L, -1)) rel_y = lua_tointeger(L, -1); lua_pop(L, 1);

        int abs_row = nav_y + rel_y;
        int abs_col = nav_x + rel_x;
        int available_width = nav_w - rel_x;

        // Check bounds & Draw segment (using nav_x, nav_y, nav_w, nav_h)
        if (abs_row >= nav_y && abs_row < nav_y + nav_h &&
            abs_col >= nav_x && abs_col < nav_x + nav_w &&
            available_width > 0)
        {
            char pos_buf[32];
            snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", abs_row, abs_col);
            abAppend(ab, pos_buf, strlen(pos_buf));
            applyTrueColor(ab, fg_color, bg_color);
            int text_len = strlen(text);
            int len_to_draw = (text_len > available_width) ? available_width : text_len;
             if (len_to_draw < 0) len_to_draw = 0;
            abAppend(ab, text, len_to_draw);
        }
        lua_pop(L, 1); // Pop segment table value
    } // End segment loop

    // --- Cleanup ---
    lua_pop(L, 2); // Pop segments table & options table
    applyThemeDefaultColor(ab);
}

// --- Overlay Management ---

// Adds or updates an overlay entry for a given state pointer
// Returns 1 on success, 0 on failure
int activateOverlay(void (*draw_func)(struct abuf *, void *), void *state) {
    if (!draw_func || !state) {
        debug_printf("activateOverlay: Invalid draw_func or state pointer.");
        return 0;
    }

    // Check if already active
    for (int i = 0; i < E.num_active_overlays; i++) {
        if (E.active_overlays[i].state == state) {
            E.active_overlays[i].draw_func = draw_func; // Update func just in case
            return 1; // Already active
        }
    }

    // Add to list if space available
    if (E.num_active_overlays < MAX_ACTIVE_OVERLAYS) {
        OverlayInstance *overlay = &E.active_overlays[E.num_active_overlays];
        overlay->draw_func = draw_func;
        overlay->state = state;
        // overlay->is_active = true; // Field is in your struct, maybe use it?
        E.num_active_overlays++;
        return 1; // Success
    } else {
        debug_printf("Error: Cannot activate overlay, maximum reached (%d)", MAX_ACTIVE_OVERLAYS);
        return 0; // Failure
    }
}

// Deactivates overlay associated with the given state pointer
// Returns 1 if deactivated, 0 if not found
int deactivateOverlay(void *state) {
    if (!state) {
         debug_printf("deactivateOverlay: Invalid state pointer.");
         return 0;
    }

    for (int i = 0; i < E.num_active_overlays; i++) {
        if (E.active_overlays[i].state == state) {
            // Found it
            E.num_active_overlays--; // Decrease count first
            // Shift subsequent elements down to fill the gap
            if (i < E.num_active_overlays) {
                 memmove(&E.active_overlays[i], &E.active_overlays[i + 1],
                        (E.num_active_overlays - i) * sizeof(OverlayInstance));
            }
            // Zero out the last (now unused) slot
            memset(&E.active_overlays[E.num_active_overlays], 0, sizeof(OverlayInstance));
            return 1; // Success
        }
    }
    return 0; // Not found
}


// Helper potentially called by Lua API config function
void editorSetPanelMode(PanelDisplayMode new_mode) {
    // Deactivate any floating panel overlay first
    // Pass pointer to the panel's state struct in E
    deactivateOverlay(&E.panel_state);

    E.panel_mode = new_mode;
    E.panel_visible = (new_mode != PANEL_MODE_NONE);

    // If the new mode is FLOAT and it should be visible, activate it
    if (E.panel_mode == PANEL_MODE_FLOAT && E.panel_visible) {
        // Activate overlay using the FLOATING draw func and panel state
        if (!activateOverlay(editorDrawDirTreeFloating, &E.panel_state)) {
             editorSetStatusMessage("Error: Could not activate floating panel overlay.");
             E.panel_visible = false; // Mark as not visible if activation failed
        }
    }

    // If switching TO a fixed mode, ensure component is registered and enabled
    // (Registration might happen once in initEditor)
    // You might need uiEnableComponent("dir_panel", E.panel_visible && E.panel_mode != PANEL_MODE_FLOAT);
}

void editorDrawDebugOverlay(struct abuf *ab) {
    // --- Full Screen Setup ---
    int overlay_width = E.screencols;
    int overlay_height = E.screenrows;
    int overlay_start_row = 0; // Screen coordinates are 1-based, index is 0
    int overlay_start_col = 0; // Screen coordinates are 1-based, index is 0

    // Basic check: Don't draw if screen isn't usable
    if (overlay_height < 1 || overlay_width < 1) return;

    // --- Color Setup ---
    // Use background_bg for overlay background. Allow fallback.
    char *overlay_bg = E.theme.ui_background_bg ? E.theme.ui_background_bg : "#000044";
    // Use ui_message_fg for default content text. Allow fallback. (CORRECTED)
    char *overlay_fg = E.theme.ui_message_fg ? E.theme.ui_message_fg : "#ffffff";
    // Use status_mode colors for the title bar. Allow fallbacks.
    char *overlay_title_bg = E.theme.ui_status_mode_bg ? E.theme.ui_status_mode_bg : "#ff0000";
    char *overlay_title_fg = E.theme.ui_status_mode_fg ? E.theme.ui_status_mode_fg : "#ffffff";


    // --- Prepare Raw Log Buffer Content ---
    // Make a temporary copy to safely parse lines by inserting null terminators
    char raw_buffer_copy[DEBUG_BUFFER_SIZE];
    strncpy(raw_buffer_copy, debug_buffer, DEBUG_BUFFER_SIZE - 1);
    raw_buffer_copy[DEBUG_BUFFER_SIZE - 1] = '\0';

    // Calculate how many lines we might need from the raw buffer
    int title_lines = 1;
    int border_lines = 1; // Assuming a bottom border/blank line for padding
    int content_height = overlay_height - title_lines - border_lines;
    if (content_height < 0) content_height = 0;

    int max_raw_lines_needed = content_height - displayed_error_count;
    if (max_raw_lines_needed < 0) max_raw_lines_needed = 0;

    // Array to hold pointers to the start of relevant lines in raw_buffer_copy
    char *raw_lines[MAX_OVERLAY_RAW_LINES];
    int raw_line_count = 0;
    // Limit the lines we actually try to fetch to avoid excessive processing/memory
    int lines_to_fetch = (max_raw_lines_needed > MAX_OVERLAY_RAW_LINES) ? MAX_OVERLAY_RAW_LINES : max_raw_lines_needed;

    // Find line starts by working backwards through the buffer copy
    if (lines_to_fetch > 0) {
         char *end = raw_buffer_copy + strlen(raw_buffer_copy);
         char *ptr = end;
         while (ptr > raw_buffer_copy && raw_line_count < lines_to_fetch) {
             ptr--;
             if (*ptr == '\n' || ptr == raw_buffer_copy) {
                 char *line_start = (ptr == raw_buffer_copy) ? ptr : ptr + 1;
                 // Only add non-empty lines
                 if (*line_start != '\0' && *line_start != '\n') {
                     if (raw_line_count < MAX_OVERLAY_RAW_LINES) { // Check array bounds
                          raw_lines[raw_line_count++] = line_start;
                     } else {
                          break; // Stop if fixed array is full
                     }
                 }
                 // Null-terminate the *previous* line in the copy for safe strlen/drawing later
                 if (ptr > raw_buffer_copy && *ptr == '\n') *ptr = '\0';
             }
         }
         // Catch the very first line if buffer doesn't start with newline and wasn't empty
         if (ptr == raw_buffer_copy && raw_line_count < lines_to_fetch && *ptr != '\0' && *ptr != '\n') {
             if (raw_line_count < MAX_OVERLAY_RAW_LINES) { // Check bounds again
                  raw_lines[raw_line_count++] = ptr;
             }
         }
    }
    // --- End Raw Log Buffer Prep ---


    // --- Draw the Full Overlay ---
    int raw_line_render_idx = raw_line_count - 1; // Index to render raw lines (most recent first)

    for (int i = 0; i < overlay_height; i++) {
        // Move cursor to the start of the current overlay line
        char pos_buf[32];
        // ANSI escape codes use 1-based row/column numbers
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH",
                 overlay_start_row + i + 1, overlay_start_col + 1);
        abAppend(ab, pos_buf, strlen(pos_buf));

        // Set colors for the current line type and clear the line
        if (i == 0) { // Title bar line
            applyTrueColor(ab, overlay_title_fg, overlay_title_bg);
        } else { // Content or bottom border line
            applyTrueColor(ab, overlay_fg, overlay_bg); // Use corrected default fg
        }
        abAppend(ab, "\x1b[K", 3); // Clear line from cursor to end with current colors

        // Draw the specific content for this line
        if (i == 0) {
            // --- Draw Title Bar ---
            // Centered title text
            char title_text[128]; // Buffer for the title string
            snprintf(title_text, sizeof(title_text), " DEBUG LOG - Press Ctrl-D to dismiss ");
            int title_len = strlen(title_text);
            int padding = (overlay_width - title_len) / 2;
            if (padding < 0) padding = 0;

            // Move cursor to start of centered text using ANSI code \x1b[<N>C (Cursor Forward)
            if (padding > 0) {
                char pad_buf[16];
                snprintf(pad_buf, sizeof(pad_buf), "\x1b[%dC", padding);
                abAppend(ab, pad_buf, strlen(pad_buf));
            }
            // Append the title text (only draw what fits)
            int draw_len = (title_len > overlay_width - padding) ? (overlay_width - padding) : title_len;
             if (draw_len > 0) {
                  abAppend(ab, title_text, draw_len);
             }
            // Remainder of line is cleared by \x1b[K

        } else if (i == overlay_height - 1) {
            // --- Draw Bottom Border (optional) ---
            // Line is already cleared with the default overlay background/foreground
            // You could add specific characters like '─' here if desired
            ; // Currently draws a blank line with the overlay background

        } else {
            // --- Draw Content Lines ---
            int content_line_idx = i - 1; // 0-based index for the content area
            char *line_to_draw = NULL;

            // Priority 1: Draw unique errors from displayed_errors buffer
            if (content_line_idx < displayed_error_count) {
                line_to_draw = displayed_errors[content_line_idx];
            }
            // Priority 2: Fill remaining space with recent lines from raw debug_buffer
            else if (raw_line_render_idx >= 0) {
                 // Make sure the pointer is still valid (within the copy buffer)
                 if(raw_lines[raw_line_render_idx] >= raw_buffer_copy && raw_lines[raw_line_render_idx] < raw_buffer_copy + DEBUG_BUFFER_SIZE) {
                     line_to_draw = raw_lines[raw_line_render_idx];
                 }
                 raw_line_render_idx--; // Move to the next older raw line for the next loop iteration
            }

            // Render the selected line (truncate if needed)
            if (line_to_draw) {
                int line_len = strlen(line_to_draw);
                int len_to_draw = (line_len < overlay_width) ? line_len : overlay_width;

                for (int j = 0; j < len_to_draw; j++) {
                    char ch = line_to_draw[j];
                    // Replace control characters (like newline embedded mid-string) with '?'
                    if (iscntrl((unsigned char)ch)) {
                        abAppend(ab, "?", 1);
                    } else {
                        abAppend(ab, &ch, 1);
                    }
                }
                // Rest of the line is cleared by \x1b[K
            }
            // If no line_to_draw, the line remains blank (cleared by \x1b[K)
        }
    }

    // Reset colors back to default after drawing the entire overlay
    applyThemeDefaultColor(ab);
}

// --- Overlay Management END ---


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

/**
 * @brief Generic draw function for UI elements configured by Lua
 * @param ab The append buffer to draw into
 * @param lua_callback_ref The reference to the Lua callback function
 * @return int 0 on success, non-zero on error
 */
int editorDrawUiElement(struct abuf *ab, int lua_callback_ref) {
    lua_State *L = getLuaState();
    if (!L || lua_callback_ref == LUA_NOREF) {
        return -1; // Lua not initialised or callback not set
    }

    // Call the Lua callback
    lua_rawgeti(L, LUA_REGISTRYINDEX, lua_callback_ref);
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf("UI element Lua error: %s", error_msg ? error_msg : "unknown");
        lua_pop(L, 1);
        return -1;
    }

    // Parse segments and options from Lua
    int segments_idx = -2;
    int options_idx = -1;
    int has_options = 0;
    int top = lua_gettop(L);

    if (top >= 1 && lua_istable(L, -1)) {
        if (top >= 2 && lua_istable(L, -2)) {
            segments_idx = -2; options_idx = -1; has_options = 1;
        } else {
            segments_idx = -1; options_idx = 0; has_options = 0;
        }
    } else {
        debug_printf("UI element Lua function must return at least a segments table");
        lua_pop(L, top);
        return -1;
    }

    // Extract layout information from options
    int x = 1, y = 1, width = E.screencols, height = 1;
    char *fg = NULL, *bg = NULL;

    if (has_options) {
        lua_getfield(L, options_idx, "x");
        if (lua_isnumber(L, -1)) x = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "y");
        if (lua_isnumber(L, -1)) y = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "width");
        if (lua_isnumber(L, -1)) width = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "height");
        if (lua_isnumber(L, -1)) height = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "fg");
        if (lua_isstring(L, -1)) {
            const char *n = lua_tostring(L, -1);
            char *t = getThemeColorByName(n);
            if (t) fg = t; else if (n[0] == '#') fg = (char*)n;
        }
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "bg");
        if (lua_isstring(L, -1)) {
            const char *n = lua_tostring(L, -1);
            char *t = getThemeColorByName(n);
            if (t) bg = t; else if (n[0] == '#') bg = (char*)n;
        }
        lua_pop(L, 1);
    }

    // Clamp layout values to screen dimensions
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (x + width > E.screencols + 1) width = E.screencols - x + 1;
    if (y + height > E.total_rows + 1) height = E.total_rows - y + 1;

    // Set default colours if not specified
    if (!fg) fg = E.theme.hl_normal_fg ? E.theme.hl_normal_fg : "#ffffff";
    if (!bg) bg = E.theme.ui_background_bg ? E.theme.ui_background_bg : "#000000";

    // Clear the background area
    for (int r = 0; r < height; r++) {
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", y + r, x);
        abAppend(ab, pos_buf, strlen(pos_buf));
        applyTrueColor(ab, fg, bg);
        
        for (int c = 0; c < width; c++) {
            abAppend(ab, " ", 1);
        }
    }

    // Process segments
    lua_Integer segment_count_raw = lua_rawlen(L, segments_idx);
    int segment_count = (segment_count_raw > INT_MAX || segment_count_raw < 0) ? 0 : (int)segment_count_raw;

    for (int i = 1; i <= segment_count; i++) {
        lua_rawgeti(L, segments_idx, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        // Get segment properties
        const char *text = NULL;
        lua_getfield(L, -1, "text");
        if (lua_isstring(L, -1)) text = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!text) { lua_pop(L, 1); continue; }

        // Get segment position and colours
        int rel_x = 0, rel_y = 0;
        char *segment_fg = fg, *segment_bg = bg;

        lua_getfield(L, -1, "x_rel");
        if (lua_isnumber(L, -1)) rel_x = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "y_rel");
        if (lua_isnumber(L, -1)) rel_y = lua_tointeger(L, -1);
        lua_pop(L, 1);

        const char *fg_name = NULL, *bg_name = NULL;
        lua_getfield(L, -1, "fg");
        if (lua_isstring(L, -1)) fg_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "bg");
        if (lua_isstring(L, -1)) bg_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        // Resolve segment colours
        if (fg_name) {
            char *t = getThemeColorByName(fg_name);
            if (t) segment_fg = t;
            else if (fg_name[0] == '#') segment_fg = (char*)fg_name;
        }

        if (bg_name) {
            char *t = getThemeColorByName(bg_name);
            if (t) segment_bg = t;
            else if (bg_name[0] == '#') segment_bg = (char*)bg_name;
        }

        // Calculate absolute position
        int abs_row = y + rel_y;
        int abs_col = x + rel_x;

        // Check if segment is within element boundaries
        if (abs_row >= y && abs_row < y + height &&
            abs_col >= x && abs_col < x + width) {
            
            // Position cursor and set colours
            char pos_buf[32];
            snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", abs_row, abs_col);
            abAppend(ab, pos_buf, strlen(pos_buf));
            applyTrueColor(ab, segment_fg, segment_bg);
            
            // Calculate available width and clip text if needed
            int available_width = width - rel_x;
            int text_len = strlen(text);
            int visible_width = calculate_visible_length_ansi(text);
            
            if (available_width > 0) {
                if (visible_width > available_width) {
                    // Text is too long, needs clipping
                    // For simplicity, just truncate by byte length
                    // A more accurate approach would clip based on visual width
                    int len_to_draw = text_len * available_width / visible_width;
                    if (len_to_draw > text_len) len_to_draw = text_len;
                    abAppend(ab, text, len_to_draw);
                } else {
                    // Text fits, draw it all
                    abAppend(ab, text, text_len);
                }
            }
        }
        
        lua_pop(L, 1); // Pop segment table
    }

    // Clean up
    lua_pop(L, has_options ? 2 : 1);
    applyThemeDefaultColor(ab);
    
    return 0;
}


// Helper function to draw all UI components using Lua layout
bool drawLuaLayout(struct abuf *ab) {
  lua_State *L = getLuaState();
  bool using_lua_layout = false;

  if (L && layout_callback_ref != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, layout_callback_ref);
    if (lua_pcall(L, 0, 1, 0) == LUA_OK && lua_istable(L, -1)) {
      // Process UI elements from Lua
      lua_getfield(L, -1, "elements");
      if (lua_istable(L, -1)) {
        lua_Integer element_count = lua_rawlen(L, -1);
        for (int i = 1; i <= element_count; i++) {
          lua_rawgeti(L, -1, i);
          if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "name");
            const char *element_name = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
            lua_pop(L, 1);
            
            if (element_name) {
              // Add debug code here
              printf("Looking for UI element: %s", element_name);
              lua_getglobal(L, "kilo");
              lua_getfield(L, -1, "ui_elements");
              if (lua_istable(L, -1)) {
                printf("kilo.ui_elements table exists");
                lua_getfield(L, -1, element_name);
                printf("kilo.ui_elements[%s] is %s", element_name, 
                          lua_isfunction(L, -1) ? "a function" : 
                          lua_isnil(L, -1) ? "nil" : "not a function");
                lua_pop(L, 1); // Pop the result
              } else {
                printf("kilo.ui_elements is not a table!");
              }
              lua_pop(L, 2); // Pop ui_elements and kilo
              
              // Special case for text area
              if (strcmp(element_name, "textarea") == 0) {
                editorDrawTextArea(ab);
              } else {
                // Get callback function for other elements
                lua_getglobal(L, "kilo");
                lua_getfield(L, -1, "ui_elements");
                lua_getfield(L, -1, element_name);
                
                if (lua_isfunction(L, -1)) {
                  int temp_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                  lua_pop(L, 2); // Pop ui_elements and kilo tables
                  
                  editorDrawUiElement(ab, temp_callback_ref);
                  
                  luaL_unref(L, LUA_REGISTRYINDEX, temp_callback_ref);
                } else {
                  lua_pop(L, 3); // Pop function (or nil), ui_elements, and kilo tables
                }
              }
            }
            
            lua_pop(L, 1); // Pop element table
          }
        }
      }
      lua_pop(L, 1); // Pop elements table
      
      // Set flag that we used Lua layout
      using_lua_layout = true;
      
      // Pop layout table
      lua_pop(L, 1);
    } else {
      // Failed to call Lua function or result is not a table
      if (lua_gettop(L) > 0) {
        printf("Failed to call layout function or result is not a table");
        lua_pop(L, 1); // Pop error message or non-table result
      }
    }
  } else {
    printf("Layout not used: L valid=%s, layout_callback_ref=%d", 
                L ? "yes" : "no", layout_callback_ref);
  }
  
  // If Lua layout failed or isn't available, use default layout
  if (!using_lua_layout) {
    // Draw tabline
    editorDrawTabline(ab);
    
    // Draw panel if visible
    if (E.panel_visible) {
      if (E.panel_mode == PANEL_MODE_LEFT) {
        editorDrawDirTreeFixed(ab, 1, E.content_start_row, 
                            E.panel_render_width, E.screenrows);
      } else if (E.panel_mode == PANEL_MODE_RIGHT) {
        int right_panel_start_col = E.screencols - E.panel_render_width + 1;
        editorDrawDirTreeFixed(ab, right_panel_start_col, E.content_start_row, 
                            E.panel_render_width, E.screenrows);
      }
    }
    
    // Draw text area
    editorDrawTextArea(ab);
    
    // Draw statusbar and message bar
    editorDrawStatusBar(ab);
    editorDrawMessageBar(ab);
  }
  
  // Draw active overlays (always done regardless of layout)
  for (int i = 0; i < E.num_active_overlays; i++) {
    OverlayInstance *overlay = &E.active_overlays[i];
    if (overlay->draw_func) {
      overlay->draw_func(ab, overlay->state);
    }
  }
  
  // Draw debug overlay if active
  if (debug_overlay_active) {
    editorDrawDebugOverlay(ab);
  }
  
  return true;
}


void editorDrawTextArea(struct abuf *ab) {
  // Default text area dimensions - calculate based on fixed components
  int reserved_rows_top = 1;     // Tabline
  int reserved_rows_bottom = 2;  // Statusbar & messagebar
  int reserved_cols_left = 0;    // Default: no side panel
  int reserved_cols_right = 0;   // Default: no right panel
  
  // Check if panel is enabled in fixed mode
  if (E.panel_visible) {
    if (E.panel_mode == PANEL_MODE_LEFT) {
      reserved_cols_left = E.panel_render_width;
    } else if (E.panel_mode == PANEL_MODE_RIGHT) {
      reserved_cols_right = E.panel_render_width;
    }
  }
  
  // Calculate default text area dimensions
  int text_area_start_row = reserved_rows_top + 1;
  int text_area_start_col = reserved_cols_left + 1;
  int text_area_width = E.screencols - reserved_cols_left - reserved_cols_right;
  int text_area_height = E.total_rows - reserved_rows_top - reserved_rows_bottom;
  
  // Ensure minimum dimensions
  if (text_area_width < 1) text_area_width = 1;
  if (text_area_height < 1) text_area_height = 1;
  
  lua_State *L = getLuaState();
  if (L && textarea_callback_ref != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, textarea_callback_ref);
    if (lua_pcall(L, 0, 1, 0) == LUA_OK && lua_istable(L, -1)) {
      lua_getfield(L, -1, "x");
      if (!lua_isnil(L, -1)) text_area_start_col = lua_tointeger(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "y");
      if (!lua_isnil(L, -1)) text_area_start_row = lua_tointeger(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "width");
      if (!lua_isnil(L, -1)) text_area_width = lua_tointeger(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "height");
      if (!lua_isnil(L, -1)) text_area_height = lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  
  // Update global screen dimensions for scrolling
  E.content_start_row = text_area_start_row;
  E.content_start_col = text_area_start_col;
  E.screenrows = text_area_height;
  E.content_width = text_area_width;
  
  editorDrawRows(ab, text_area_start_row, text_area_start_col, 
                text_area_height, text_area_width);
}


void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    
    // Hide cursor and go to home position
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    
    // Calculate component layout
    calculateLayout();
    
    // Draw all components
    drawComponents(&ab);
    
    // Position cursor based on text area and scroll position
    editorScroll();
    
    int cursor_screen_row = (E.cy - E.rowoff) + E.content_start_row;
    int cursor_screen_col = (E.rx - E.coloff) + E.content_start_col + KILO_LINE_NUMBER_WIDTH;
    
    // Clamp cursor to screen bounds
    if (cursor_screen_row < E.content_start_row) 
        cursor_screen_row = E.content_start_row;
    if (cursor_screen_row >= E.content_start_row + E.screenrows) 
        cursor_screen_row = E.content_start_row + E.screenrows - 1;
    
    if (cursor_screen_col < E.content_start_col) 
        cursor_screen_col = E.content_start_col;
    if (cursor_screen_col >= E.content_start_col + E.content_width) 
        cursor_screen_col = E.content_start_col + E.content_width - 1;
    
    // Ensure cursor position is valid
    if (cursor_screen_row < 1) cursor_screen_row = 1;
    if (cursor_screen_col < 1) cursor_screen_col = 1;
    
    // Position cursor
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_screen_row, cursor_screen_col);
    abAppend(&ab, buf, strlen(buf));
    
    // Show cursor
    abAppend(&ab, "\x1b[?25h", 6);
    
    // Write the buffer to stdout
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}


// OLD IMPLEMENTATION
// void editorRefreshScreen() {
//   struct abuf ab = ABUF_INIT;
  
//   // Update screen dimensions
//   getWindowSize(&E.total_rows, &E.screencols);
  
//   // Hide cursor and go to home position
//   abAppend(&ab, "\x1b[?25l", 6);
//   abAppend(&ab, "\x1b[H", 3);
  
//   // Draw all UI components (with default fallback)
//   drawLuaLayout(&ab);
  
//   // Perform scrolling
//   editorScroll();
  
//   // Position cursor (using global values set by drawLuaLayout > editorDrawTextArea)
//   int cursor_screen_row = (E.cy - E.rowoff) + E.content_start_row;
//   int cursor_screen_col = (E.rx - E.coloff) + E.content_start_col + KILO_LINE_NUMBER_WIDTH;
  
//   // Clamp cursor (using global text area dimensions)
//   if (cursor_screen_row < E.content_start_row) 
//     cursor_screen_row = E.content_start_row;
//   if (cursor_screen_row >= E.content_start_row + E.screenrows) 
//     cursor_screen_row = E.content_start_row + E.screenrows - 1;
  
//   if (cursor_screen_col < E.content_start_col) 
//     cursor_screen_col = E.content_start_col;
//   if (cursor_screen_col >= E.content_start_col + E.content_width) 
//     cursor_screen_col = E.content_start_col + E.content_width - 1;
  
//   char buf[32];
//   snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_screen_row, cursor_screen_col);
//   abAppend(&ab, buf, strlen(buf));
  
//   // Show cursor and write buffer
//   abAppend(&ab, "\x1b[?25h", 6);
//   write(STDOUT_FILENO, ab.b, ab.len);
//   abFree(&ab);
// }

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
    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-N/P = next/prev buff");
}