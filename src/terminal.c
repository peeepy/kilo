#include "kilo.h"


/*
 * Clears the screen, prints an error message based on errno,
 * and terminates the program.
 */
void die(const char* s) {
  // Clear the screen (\x1b[2J) and reposition cursor to top-left (\x1b[H)
  // using ANSI escape sequences.
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  // Print the error message associated with the last system call error (errno)
  perror(s);
  exit(1);
}

/*
 * Restores the original terminal attributes saved in E.orig_termios.
 * Called automatically on exit using atexit().
 */
void disableRawMode() {
    // TCSAFLUSH: Apply changes after all pending output has been written,
    // and discard any pending input.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr"); // Use die() for error handling
}

/*
 * Enables raw mode for the terminal.
 * Raw mode allows reading input byte-by-byte, without buffering,
 * and disables default terminal processing like echoing input.
 */
void enableRawMode() {
    // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    // Register disableRawMode to be called automatically on program exit
    atexit(disableRawMode);

    struct termios raw = E.orig_termios; // Make a copy to modify

    /* Modify terminal flags:
     * Input flags (c_iflag):
     * BRKINT: Don't send SIGINT on break condition.
     * ICRNL: Don't translate carriage return (\r) to newline (\n).
     * INPCK: Disable input parity checking.
     * ISTRIP: Don't strip the 8th bit from input characters.
     * IXON: Disable software flow control (Ctrl+S/Ctrl+Q).
     * Output flags (c_oflag):
     * OPOST: Disable all output processing (like converting \n to \r\n).
     * Control flags (c_cflag):
     * CS8: Set character size to 8 bits per byte (common).
     * Local flags (c_lflag):
     * ECHO: Don't echo input characters back to the terminal.
     * ICANON: Disable canonical mode (read input byte-by-byte, not line-by-line).
     * IEXTEN: Disable implementation-defined input processing (e.g. Ctrl+V).
     * ISIG: Disable signal generation (e.g. Ctrl+C sends SIGINT, Ctrl+Z sends SIGTSTP).
     * Control characters (c_cc):
     * VMIN: Minimum number of bytes to read before read() returns (0 = non-blocking).
     * VTIME: Maximum time to wait for input in deciseconds (1 = 100ms timeout).
     */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout

    // Apply the modified terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*
 * Waits for and reads a single keypress from standard input.
 * Handles multi-byte escape sequences for special keys (arrows, Home, End, etc.).
 * Returns the character read or a special key code (from editorKey enum).
 */
int editorReadKey() {
    int nread;
    char c;
    // Keep reading until a byte is received or an error occurs (excluding timeout)
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // EAGAIN typically means the read timed out (VMIN=0, VTIME>0), which is expected.
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    // Check if the character is an escape character (start of escape sequence)
    if (c == '\x1b') {
        char seq[3]; // Buffer to read the rest of the sequence

        // Try reading the next two bytes of the sequence with timeouts
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b'; // Timeout or error, return ESC
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b'; // Timeout or error, return ESC

        // Check for common escape sequence patterns (CSI - Control Sequence Introducer)
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Sequences like \x1b[1~ (Home), \x1b[3~ (Delete), etc.
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; // Need the trailing '~'
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY; // Sometimes Home is 7~
                        case '8': return END_KEY;  // Sometimes End is 8~
                    }
                }
            } else {
                // Sequences like \x1b[A (Up), \x1b[B (Down), etc.
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY; // Sometimes Home is [H
                    case 'F': return END_KEY;  // Sometimes End is [F
                }
            }
        } else if (seq[0] == 'O') {
             // Less common sequences like \x1bOH (Home), \x1bOF (End) used by some terminals
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b'; // Unrecognized escape sequence, return ESC itself
    } else {
        return c; // Normal character keypress
    }
}

/*
 * Tries to get the current cursor position using ANSI escape sequences.
 * Sends "\x1b[6n" (Device Status Report - Cursor Position) and parses the response "\x1b[<row>;<col>R".
 * Returns 0 on success, -1 on failure.
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // Request cursor position report
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // Read the response from stdin, looking for the terminating 'R'
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break; // Error or timeout reading
        if (buf[i] == 'R') break; // Found end of response
        i++;
    }
    buf[i] = '\0'; // Null-terminate the buffer

    // Check if the response starts with the expected escape sequence
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // Parse the row and column numbers from the response string
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0; // Success
}

/*
 * Tries to get the terminal window size.
 * First attempts using ioctl(TIOCGWINSZ). If that fails, falls back
 * to moving the cursor far down-right and querying its position.
 * Returns 0 on success, -1 on failure.
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // Try the ioctl system call first (preferred method)
    // TIOCGWINSZ = Terminal IO Control Get Window SiZe
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Fallback: Move cursor far right (\x1b[999C) and far down (\x1b[999B),
        // then query its position. Terminals usually cap the position at the edge.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols); // Use the fallback position query
    } else {
        // Success using ioctl
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}