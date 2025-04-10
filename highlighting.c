#include "kilo.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <limits.h> // For PATH_MAX (might need sys/param.h on some systems)

// Removes leading/trailing whitespace from a string in-place
char *trimWhitespace(char *str) {
    if (!str) return NULL;
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = '\0';

    return str;
}

// Splits a string by a delimiter, returning a NULL-terminated array of strings.
// Stores the count in count_out. Remember to free the array and its contents.
static char **splitString(const char *str, const char *delim, int *count_out) {
    char **result = NULL;
    int count = 0;
    char *input_copy = NULL;
    char *token = NULL;
    char *saveptr = NULL; // For strtok_r (or ignored by strtok)

    if (!str || !delim || !count_out) {
        if(count_out) *count_out = 0;
        return NULL;
    }

    input_copy = strdup(str); // Use a copy because strtok modifies the string
    if (!input_copy) {
        perror("splitString: strdup failed");
        *count_out = 0;
        return NULL;
    }

    // Use strtok_r if available (thread-safe), otherwise strtok
    #ifdef _POSIX_C_SOURCE
    token = strtok_r(input_copy, delim, &saveptr);
    #else
    token = strtok(input_copy, delim); // Less safe but standard
    #endif


    while (token != NULL) {
        char **new_result = realloc(result, sizeof(char *) * (count + 1));
        if (!new_result) {
            perror("splitString: realloc failed");
            // Free already allocated tokens
            for (int i = 0; i < count; i++) free(result[i]);
            free(result);
            free(input_copy);
            *count_out = 0;
            return NULL;
        }
        result = new_result;

        result[count] = strdup(token);
        if (!result[count]) {
             perror("splitString: strdup token failed");
            // Free already allocated tokens including the result array
            for (int i = 0; i < count; i++) free(result[i]);
            free(result);
            free(input_copy);
            *count_out = 0;
            return NULL;
        }
        count++;

        #ifdef _POSIX_C_SOURCE
        token = strtok_r(NULL, delim, &saveptr);
        #else
        token = strtok(NULL, delim);
        #endif
    }

    // Add NULL terminator to the array
    char **final_result = realloc(result, sizeof(char *) * (count + 1));
     if (!final_result && count > 0) { // If realloc fails but we had items
        // Can't add NULL terminator, proceed with caution or free everything
        fprintf(stderr, "splitString: realloc for NULL terminator failed\n");
        // Let's return what we have, but it's not ideal
    } else {
         result = final_result; // Use realloc'd pointer (might be same or new)
         if (result) result[count] = NULL; // Add NULL terminator
    }


    free(input_copy); // Free the copy we made
    *count_out = count;
    return result;
}


// Frees memory allocated for a string array (like from splitString)
static void freeStringArray(char **arr) {
    if (!arr) return;
    for (int i = 0; arr[i]; i++) {
        free(arr[i]);
    }
    free(arr);
}


// --- Core Parsing and Loading ---

// Parses a single .syntax file
static int parseSyntaxFile(const char *filepath, struct editorSyntax *s) {
    memset(s, 0, sizeof(struct editorSyntax)); // Initialize struct
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("parseSyntaxFile: fopen failed");
        return -1;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    int success = 0; // Assume success until error

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        char *trimmed_line = trimWhitespace(line);
        if (trimmed_line[0] == '#' || trimmed_line[0] == '\0') continue; // Skip comments/empty

        char *colon = strchr(trimmed_line, ':');
        if (!colon) {
            fprintf(stderr, "parseSyntaxFile: Invalid line in %s: %s\n", filepath, trimmed_line);
            continue; // Skip invalid line
        }

        *colon = '\0'; // Split key and value
        char *key = trimWhitespace(trimmed_line);
        char *value = trimWhitespace(colon + 1);
        int count; // Declare count here for reuse

        if (strcmp(key, "filetype") == 0) {
            free(s->filetype); // Free previous if exists
            s->filetype = strdup(value);
            if (!s->filetype) goto parse_error;
        } else if (strcmp(key, "filematch") == 0) {
            freeStringArray(s->filematch);
            s->filematch = splitString(value, " ", &count);
            if (!s->filematch && count > 0) goto parse_error;
        } else if (strcmp(key, "keywords1") == 0) {
            freeStringArray(s->keywords1);
            s->keywords1 = splitString(value, " ", &count);
            if (!s->keywords1 && count > 0) goto parse_error;
        } else if (strcmp(key, "keywords2") == 0) {
            freeStringArray(s->keywords2);
            s->keywords2 = splitString(value, " ", &count);
            if (!s->keywords2 && count > 0) goto parse_error;
        } else if (strcmp(key, "keywords3") == 0) {
            freeStringArray(s->keywords3);
            s->keywords3 = splitString(value, " ", &count);
            if (!s->keywords3 && count > 0) goto parse_error;
        } else if (strcmp(key, "types") == 0) {
            freeStringArray(s->types);
            s->types = splitString(value, " ", &count);
            if (!s->types && count > 0) goto parse_error;
        } else if (strcmp(key, "builtins") == 0) {
            freeStringArray(s->builtins);
            s->builtins = splitString(value, " ", &count);
            if (!s->builtins && count > 0) goto parse_error;
        } else if (strcmp(key, "comment_start") == 0) {
            free(s->singleline_comment_start);
            s->singleline_comment_start = strdup(value);
            if (!s->singleline_comment_start) goto parse_error;
        } else if (strcmp(key, "ml_comment_start") == 0) {
            free(s->multiline_comment_start);
            s->multiline_comment_start = strdup(value);
            if (!s->multiline_comment_start) goto parse_error;
        } else if (strcmp(key, "ml_comment_end") == 0) {
            free(s->multiline_comment_end);
            s->multiline_comment_end = strdup(value);
            if (!s->multiline_comment_end) goto parse_error;
        } else if (strcmp(key, "language_icon") == 0) {
            free(s->status_icon);
            s->status_icon = strdup(value);
            if (!s->status_icon) goto parse_error;
        } else if (strcmp(key, "flags") == 0) {
            s->flags = 0; // Reset flags before parsing new ones
            char **flags_list = splitString(value, " ", &count);
            if (!flags_list && count > 0) goto parse_error;
            if (flags_list){
                for (int i = 0; i < count; i++) {
                    if (strcmp(flags_list[i], "numbers") == 0) s->flags |= HL_HIGHLIGHT_NUMBERS;
                    if (strcmp(flags_list[i], "strings") == 0) s->flags |= HL_HIGHLIGHT_STRINGS;
                    // Add more flag checks here if needed (e.g., "comments")
                }
                freeStringArray(flags_list);
            }
        } else {
            fprintf(stderr, "parseSyntaxFile: Unknown key in %s: %s\n", filepath, key);
        }
    } // End while getline

    goto parse_cleanup; // Jump to cleanup

parse_error:
    fprintf(stderr, "parseSyntaxFile: Memory allocation failed or error occurred in %s\n", filepath);
    success = -1;
    // Free partially allocated structure members
    free(s->filetype); s->filetype = NULL;
    freeStringArray(s->filematch); s->filematch = NULL;
    freeStringArray(s->keywords1); s->keywords1 = NULL;
    freeStringArray(s->keywords2); s->keywords2 = NULL;
    freeStringArray(s->keywords3); s->keywords3 = NULL;
    freeStringArray(s->types);     s->types = NULL;
    freeStringArray(s->builtins);  s->builtins = NULL;
    free(s->singleline_comment_start); s->singleline_comment_start = NULL;
    free(s->multiline_comment_start); s->multiline_comment_start = NULL;
    free(s->multiline_comment_end); s->multiline_comment_end = NULL;
    free(s->status_icon); s->status_icon = NULL;
    // s->flags is an int, no freeing needed

parse_cleanup:
    free(line);
    fclose(fp);
    return success;
}

// Loads all .syntax files from the syntax/ directory
void loadSyntaxFiles(void) {
    const char *dirpath = "syntax"; // Consider making this configurable
    DIR *dir = opendir(dirpath);
    if (!dir) {
        // It might be okay not to find the directory if syntax highlighting is optional
        // perror("loadSyntaxFiles: Could not open syntax directory");
        fprintf(stderr, "loadSyntaxFiles: Could not open syntax directory '%s'. No syntax highlighting loaded.\n", dirpath);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip '.' and '..' entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Check if it's a regular file (DT_REG might not be available everywhere, stat as fallback?)
        // For simplicity, we rely on DT_REG if present.
        #ifdef _DIRENT_HAVE_D_TYPE
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) { // Allow unknown, check extension later
             continue;
        }
        #endif
        // Alternatively, use stat() here for portability if DT_TYPE is unreliable


        char *dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".syntax") == 0) {
            // Construct full path
            char filepath[PATH_MAX];
            int len = snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);
            if (len < 0 || (size_t)len >= sizeof(filepath)) {
                fprintf(stderr, "loadSyntaxFiles: filepath too long for %s\n", entry->d_name);
                continue;
            }

            // Resize global array (E.syntax_defs assumed to be pointer in global struct E)
            struct editorSyntax *new_defs = realloc(E.syntax_defs, sizeof(struct editorSyntax) * (E.num_syntax_defs + 1));
            if (!new_defs) {
                perror("loadSyntaxFiles: realloc failed");
                closedir(dir); // Close dir before returning on failure
                // Consider freeing already loaded definitions? Depends on overall error handling strategy.
                return;
            }
            E.syntax_defs = new_defs;

            // Parse the file into the new slot
            if (parseSyntaxFile(filepath, &E.syntax_defs[E.num_syntax_defs]) == 0) {
                E.num_syntax_defs++; // Increment count ONLY on successful parse
            } else {
                fprintf(stderr, "loadSyntaxFiles: Failed to parse %s\n", filepath);
                // The allocated slot E.syntax_defs[E.num_syntax_defs] contains garbage
                // or partially allocated data from parseSyntaxFile's error path.
                // We don't increment the count, effectively ignoring it.
                // A more robust solution might realloc smaller, but this is simpler.
                // Ensure parseSyntaxFile cleaned up after itself properly.
            }
        }
    }
    closedir(dir);
}

// --- Cleanup ---

// Frees all dynamically allocated syntax definitions
void freeSyntaxDefs(void) {
    if (!E.syntax_defs) return;
    for (int i = 0; i < E.num_syntax_defs; i++) {
        struct editorSyntax *s = &E.syntax_defs[i];
        free(s->filetype);
        freeStringArray(s->filematch);
        freeStringArray(s->keywords1); // FIX: Free keywords1
        freeStringArray(s->keywords2); // ADD: Free keywords2
        freeStringArray(s->keywords3); // ADD: Free keywords3
        freeStringArray(s->types);     // ADD: Free types
        freeStringArray(s->builtins);  // ADD: Free builtins
        free(s->singleline_comment_start);
        free(s->multiline_comment_start);
        free(s->multiline_comment_end);
        free(s->status_icon);
        // Note: flags is just an int, no free needed
    }
    free(E.syntax_defs);
    E.syntax_defs = NULL;
    E.num_syntax_defs = 0;
}


int is_separator(int c) {
    // Added '$' as maybe relevant for some languages? Adjust as needed.
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:$", c) != NULL;
}


// Checks if word at row->render[i] matches any word in list.
// Returns 1 if match found (and updates hl, i), 0 otherwise.
static int match_and_highlight(erow *row, int *i, char **list, enum editorHighlight hl_type) {
    if (!list) return 0; // List not loaded or defined for this syntax

    char *render = row->render;
    int rsize = row->rsize;
    int current_i = *i;

    for (int j = 0; list[j]; j++) {
        int klen = strlen(list[j]);
        int end_char_pos = current_i + klen;

        // Check if the keyword fits and is followed by a separator
        // Also handle case where keyword is at the very end of the line
        if (end_char_pos <= rsize &&
            !strncmp(&render[current_i], list[j], klen) &&
            (end_char_pos == rsize || is_separator(render[end_char_pos])) ) // Separator check is key
        {
            memset(&row->hl[current_i], hl_type, klen); // Apply highlight
            *i += klen; // Advance main loop counter
            return 1; // Match found
        }
    }
    return 0; // No match in this list
}


void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    if (!row->hl && row->rsize > 0) die("editorUpdateSyntax: realloc hl failed"); // Check realloc! Handle 0 size case.
    if (row->hl) { // Add a check before memset if realloc can return NULL for size 0
        memset(row->hl, HL_NORMAL, row->rsize);
    }

    if (E.syntax == NULL) return; // No syntax definition selected for this file

    // Get pointers to the lists from the current syntax definition
    // These might be NULL if not defined in the .syntax file
    char **keywords1 = E.syntax->keywords1;
    char **keywords2 = E.syntax->keywords2;
    char **keywords3 = E.syntax->keywords3;
    char **types = E.syntax->types;
    char **builtins = E.syntax->builtins;

    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;       // Is the previous character a separator? Start of line counts.
    int in_string = 0;      // Current string delimiter ('"' or '\''), or 0 if not in string.
    // Multiline comment state persists from previous line
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        // Handle single line comments first (only if not in string or ML comment)
        if (scs_len && !in_string && !in_comment) {
            if (i + scs_len <= row->rsize && // Bounds check
                !strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break; // Rest of the line is a comment
            }
        }

        // Handle multi-line comments (only if not in string)
        // Must be checked *before* keywords if comment start looks like a keyword (e.g., /*)
        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (i + mce_len <= row->rsize && // Bounds check
                    !strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len); // Highlight the end marker too
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1; // Treat as separator after comment ends
                    continue;
                } else {
                    i++;
                    // prev_sep should remain 0 while inside comment
                    prev_sep = 0; // Explicitly set
                    continue;
                }
            } else if (i + mcs_len <= row->rsize && // Bounds check
                       !strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len); // Highlight start marker
                i += mcs_len;
                in_comment = 1;
                prev_sep = 0; // Start of comment is not a separator for next char
                continue;
            }
        }

        // Handle strings (if flag is set)
        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                 // Basic escape sequence handling (highlight \ and next char)
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING; // Or HL_STRING_ESCAPE if defined
                    i += 2; // Skip escaped char
                    prev_sep = 0; // Treat escape sequence as non-separator internally
                    continue;
                }
                // Check for end of string *after* escape check
                if (c == in_string) {
                     in_string = 0; // End of string
                     prev_sep = 1; // Closing quote acts as a separator
                } else {
                    prev_sep = 0; // Inside string is not a separator
                }
                i++;
                continue; // Move to next character
            } else if (c == '"' || c == '\'') { // Start of string
                in_string = c;
                row->hl[i] = HL_STRING;
                i++;
                prev_sep = 0; // Opening quote is not a separator for next char
                continue;
            }
        }

        // Handle numbers (if flag is set and not in string or comment)
        // Needs careful check to avoid highlighting things like `my.var` as numbers
        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || // Starts with digit after sep/num
                (c == '.' && prev_hl == HL_NUMBER)) // Includes '.' if part of a number
            {
                 // Avoid highlighting "..", "1..2", etc.
                 if (c == '.' && i > 0 && row->render[i-1] == '.') {
                      // Not a number continuation, let it fall through
                 } else {
                    row->hl[i] = HL_NUMBER;
                    i++;
                    prev_sep = 0; // Part of a number is not a separator
                    continue;
                 }
            }
        }

        // --- Check Keywords/Types/Builtins if preceded by a separator ---
        // This needs to happen *after* comments/strings are handled
        if (prev_sep) {
            // Check lists in order of priority (adjust order if needed)
            if (match_and_highlight(row, &i, keywords1, HL_KEYWORD1) ||
                match_and_highlight(row, &i, keywords2, HL_KEYWORD2) ||
                match_and_highlight(row, &i, keywords3, HL_KEYWORD3) || // Add if you define HL_KEYWORD3
                match_and_highlight(row, &i, types, HL_TYPE)       || // Add if you define HL_TYPE
                match_and_highlight(row, &i, builtins, HL_BUILTIN))    // Add if you define HL_BUILTIN
            {
                prev_sep = 0; // Keyword was matched, not a separator
                continue;     // Continue main loop, `i` was advanced by match_and_highlight
            }
            // If no keyword matched, prev_sep remains 1 for the is_separator check below
        }

        // If none of the above specific highlight types matched, update separator status and advance
        prev_sep = is_separator(c);
        i++;
    } // End while loop

    // Update multi-line comment status for next line
    // Propagate change downwards if the open comment status changed for this line
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numrows) {
        editorUpdateSyntax(&E.row[row->idx + 1]); // Recursively update next line
    }
}


void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return; // No filename, no syntax

    char *ext = strrchr(E.filename, '.');
    char *fname = strrchr(E.filename, '/'); // Find last '/' for filename part
    if (fname) {
        fname++; // Point to character after '/'
    } else {
        fname = E.filename; // No '/', filename is the whole string
    }

    for (int j = 0; j < E.num_syntax_defs; j++) {
        struct editorSyntax *s = &E.syntax_defs[j]; // Use dynamic array
        if (!s->filematch) continue; // Skip if filematch failed to load or isn't defined

        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            int match = 0;

            if (is_ext && ext && strcmp(ext, s->filematch[i]) == 0) {
                match = 1; // File extension matches
            } else if (!is_ext) {
                 // Check for exact filename match (e.g., "Makefile")
                 if (strcmp(fname, s->filematch[i]) == 0) {
                    match = 1;
                 }
                 // Add more sophisticated pattern matching here if needed (e.g., shebang)
            }

            if (match) {
                E.syntax = s; // Found match

                // Re-highlight entire file as syntax context might change ML comments etc.
                for (int filerow = 0; filerow < E.numrows; filerow++) {
                    // Need to check if row->hl exists before updating - safety check
                    if (E.row[filerow].hl || E.row[filerow].rsize == 0) { // Update if hl exists or row is empty
                       editorUpdateSyntax(&E.row[filerow]);
                    } else {
                       // If hl doesn't exist but row isn't empty, need full update
                       editorUpdateRow(&E.row[filerow]); // This will call editorUpdateSyntax
                    }
                }
                return; // Exit after finding first match
            }
            i++;
        }
    }
    // If no match found, E.syntax remains NULL, no highlighting applied.
}