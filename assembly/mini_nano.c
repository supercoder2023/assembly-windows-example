/* mini_nano.c
 * A tiny terminal text editor (nano-like) for learning and quick edits.
 * POSIX only (uses termios). Build with: gcc mini_nano.c -o mini_nano
 * Controls:
 *  Ctrl-S : Save (prompts for filename if needed)
 *  Ctrl-O : Open file (prompts for filename)
 *  Ctrl-X : Exit (prompts to save if modified)
 *  Arrow keys : Move cursor
 *  Backspace / Delete : Remove characters
 *  Enter : New line
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#else
#include <windows.h>
#endif
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Simple dynamic row (line) structure */
typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx, cy;         // cursor x,y (in chars / rows)
    int rowoff;         // row offset for vertical scrolling
    int coloff;         // col offset for horizontal scrolling
    int screenrows;
    int screencols;
    int numrows;
    erow *rows;
    int dirty;
    char filename[512];
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
} E;

/* Terminal raw mode */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        ;
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/* Utilities */
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': return 127; // DEL
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return 'A' + 1000; // up
                    case 'B': return 'B' + 1000; // down
                    case 'C': return 'C' + 1000; // right
                    case 'D': return 'D' + 1000; // left
                }
            }
        }

        return '\x1b';
    }

    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return -1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
#else
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
#endif
}

/* Row operations */
void editorAppendRow(char *s, size_t len) {
    E.rows = realloc(E.rows, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.rows[at].size = len;
    E.rows[at].chars = malloc(len + 1);
    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';
    E.numrows++;
    E.dirty = 1;
}

void editorFreeRows() {
    for (int i = 0; i < E.numrows; i++) free(E.rows[i].chars);
    free(E.rows);
    E.rows = NULL;
    E.numrows = 0;
}

/* File I/O */
void editorOpen(const char *filename) {
    editorFreeRows();
    strncpy(E.filename, filename, sizeof(E.filename)-1);
    E.filename[sizeof(E.filename)-1] = '\0';

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        snprintf(E.statusmsg, sizeof(E.statusmsg), "Could not open: %s", strerror(errno));
        E.statusmsg_time = time(NULL);
        return;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, fp)) != -1) {
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
        editorAppendRow(line, len);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    snprintf(E.statusmsg, sizeof(E.statusmsg), "Opened %s", filename);
    E.statusmsg_time = time(NULL);
}

int editorSave(const char *filename) {
    char tmp[1024];
    if (filename == NULL || filename[0] == '\0') filename = E.filename[0] ? E.filename : NULL;
    if (!filename) {
        snprintf(E.statusmsg, sizeof(E.statusmsg), "No filename");
        E.statusmsg_time = time(NULL);
        return 0;
    }

    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        snprintf(E.statusmsg, sizeof(E.statusmsg), "Can't save: %s", strerror(errno));
        E.statusmsg_time = time(NULL);
        return 0;
    }

    // Build buffer
    size_t totlen = 0;
    for (int i = 0; i < E.numrows; i++) totlen += E.rows[i].size + 1; // add newline

    char *buf = malloc(totlen);
    char *p = buf;
    for (int i = 0; i < E.numrows; i++) {
        memcpy(p, E.rows[i].chars, E.rows[i].size);
        p += E.rows[i].size;
        *p = '\n';
        p++;
    }

    // write
    if (ftruncate(fd, 0) == -1) { close(fd); free(buf); return 0; }
    if (write(fd, buf, totlen) != (ssize_t)totlen) {
        snprintf(E.statusmsg, sizeof(E.statusmsg), "Write error: %s", strerror(errno));
        E.statusmsg_time = time(NULL);
        close(fd);
        free(buf);
        return 0;
    }
    close(fd);
    free(buf);
    E.dirty = 0;
    snprintf(E.statusmsg, sizeof(E.statusmsg), "Saved to %s", filename);
    E.statusmsg_time = time(NULL);
    return 1;
}

/* Editor operations: insertion, deletion, newline */
void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        // append empty row
        editorAppendRow("", 0);
    }
    erow *row = &E.rows[E.cy];
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[E.cx + 1], &row->chars[E.cx], row->size - E.cx + 1);
    row->size++;
    row->chars[E.cx] = c;
    E.cx++;
    E.dirty = 1;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    erow *row = &E.rows[E.cy];
    if (E.cx > 0) {
        memmove(&row->chars[E.cx - 1], &row->chars[E.cx], row->size - E.cx + 1);
        row->size--;
        E.cx--;
        E.dirty = 1;
    } else {
        // join line with previous
        int prev_size = E.rows[E.cy - 1].size;
        E.rows[E.cy - 1].chars = realloc(E.rows[E.cy - 1].chars, prev_size + row->size + 1);
        memcpy(&E.rows[E.cy - 1].chars[prev_size], row->chars, row->size);
        E.rows[E.cy - 1].size = prev_size + row->size;
        E.rows[E.cy - 1].chars[E.rows[E.cy - 1].size] = '\0';
        free(row->chars);
        // shift rows up
        for (int i = E.cy; i < E.numrows - 1; i++) E.rows[i] = E.rows[i + 1];
        E.numrows--;
        E.cy--;
        E.cx = prev_size;
        E.dirty = 1;
    }
}

void editorInsertNewline() {
    if (E.cx == 0) {
        editorAppendRow("", 0);
        // shift rows down
        for (int i = E.numrows - 1; i > E.cy; i--) E.rows[i] = E.rows[i - 1];
        E.rows[E.cy + 1].chars = strdup(E.rows[E.cy].chars);
        E.rows[E.cy + 1].size = E.rows[E.cy].size;
        E.rows[E.cy].size = 0;
        free(E.rows[E.cy].chars);
        E.rows[E.cy].chars = strdup("");
    } else {
        erow *row = &E.rows[E.cy];
        char *newchars = malloc(row->size - E.cx + 1);
        int newlen = row->size - E.cx;
        memcpy(newchars, &row->chars[E.cx], newlen);
        newchars[newlen] = '\0';

        row->chars = realloc(row->chars, E.cx + 1);
        row->size = E.cx;
        row->chars[row->size] = '\0';

        // insert new row after current
        editorAppendRow(newchars, newlen);
        // shift appended row to correct position
        for (int i = E.numrows - 1; i > E.cy + 1; i--) E.rows[i] = E.rows[i - 1];
        E.rows[E.cy + 1].chars = newchars;
        E.rows[E.cy + 1].size = newlen;
    }
    E.cy++;
    E.cx = 0;
    E.dirty = 1;
}

/* Input prompt (status line) */
char *editorPrompt(const char *prompt) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;

    while (1) {
        snprintf(E.statusmsg, sizeof(E.statusmsg), "%s%s", prompt, buf);
        E.statusmsg_time = time(NULL);
        // refresh
        // draw
        // simple refresh partial
        write(STDOUT_FILENO, "\x1b[2K\r", 4);
        write(STDOUT_FILENO, E.statusmsg, strlen(E.statusmsg));

        int c = editorReadKey();
        if (c == '\r') {
            if (buflen != 0) return buf;
            else { free(buf); return NULL; }
        } else if (c == 127 || c == 8) {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (!iscntrl(c) && c < 128) {
            if (buflen + 1 >= bufsize) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        } else if (c == '\x1b') {
            free(buf);
            return NULL;
        }
    }
}

/* Output rendering */
void editorScroll() {
    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
    if (E.cx < E.coloff) E.coloff = E.cx;
    if (E.cx >= E.coloff + E.screencols) E.coloff = E.cx - E.screencols + 1;
}

void editorDrawRows() {
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows/3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "mini_nano -- simple editor");
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    write(STDOUT_FILENO, "~", 1);
                    padding--;
                }
                for (; padding > 0; padding--) write(STDOUT_FILENO, " ", 1);
                write(STDOUT_FILENO, welcome, welcomelen);
            } else {
                write(STDOUT_FILENO, "~", 1);
            }
        } else {
            int len = E.rows[filerow].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            write(STDOUT_FILENO, &E.rows[filerow].chars[E.coloff], len);
        }
        write(STDOUT_FILENO, "\x1b[K", 3);
        if (y < E.screenrows - 1) write(STDOUT_FILENO, "\r\n", 2);
    }
}

void editorRefreshScreen() {
    editorScroll();

    char buf[32];
    write(STDOUT_FILENO, "\x1b[?25l", 6); // hide cursor
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    // status bar
    write(STDOUT_FILENO, "\x1b[7m", 4); // invert colors
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s %s", E.filename[0] ? E.filename : "[No Name]", E.dirty ? " (modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d lines", E.numrows);
    if (len > E.screencols) len = E.screencols;
    write(STDOUT_FILENO, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            write(STDOUT_FILENO, rstatus, rlen);
            break;
        } else {
            write(STDOUT_FILENO, " ", 1);
            len++;
        }
    }
    write(STDOUT_FILENO, "\x1b[m", 3);
    write(STDOUT_FILENO, "\r\n", 2);

    // status message
    if (time(NULL) - E.statusmsg_time < 5) {
        write(STDOUT_FILENO, E.statusmsg, strlen(E.statusmsg));
    }

    // position cursor
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[?25h", 6); // show cursor
}

/* Input handling */
void editorMoveCursor(int key) {
    switch (key) {
        case 'A' + 1000: // up
            if (E.cy > 0) E.cy--;
            if (E.cy < E.numrows && E.cx > E.rows[E.cy].size) E.cx = E.rows[E.cy].size;
            break;
        case 'B' + 1000: // down
            if (E.cy < E.numrows) E.cy++;
            if (E.cy < E.numrows && E.cx > E.rows[E.cy].size) E.cx = E.rows[E.cy].size;
            break;
        case 'C' + 1000: // right
            if (E.cy < E.numrows && E.cx < E.rows[E.cy].size) E.cx++;
            else if (E.cy < E.numrows && E.cx == E.rows[E.cy].size) { E.cy++; E.cx = 0; }
            break;
        case 'D' + 1000: // left
            if (E.cx > 0) E.cx--;
            else if (E.cy > 0) { E.cy--; E.cx = E.rows[E.cy].size; }
            break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();
    if (c == '\x11') { // Ctrl-Q (unused) - we keep for future
        return;
    }

    switch (c) {
        case '\r':
            editorInsertNewline();
            break;
        case 127: // DEL / Backspace
        case '\b':
            editorDelChar();
            break;
        case '\x13': { // Ctrl-S save
            if (E.filename[0] == '\0') {
                char *fn = editorPrompt("Save as: ");
                if (fn) {
                    strncpy(E.filename, fn, sizeof(E.filename)-1);
                    E.filename[sizeof(E.filename)-1] = '\0';
                    free(fn);
                } else break;
            }
            editorSave(E.filename);
            break; }
        case '\x0f': { // Ctrl-O open
            char *fn = editorPrompt("Open file: ");
            if (fn) {
                editorOpen(fn);
                free(fn);
                E.cx = E.cy = 0;
            }
            break; }
        case '\x18': { // Ctrl-X exit
            if (E.dirty) {
                char *ans = editorPrompt("Save changes before exit? (y/N): ");
                if (ans && (ans[0] == 'y' || ans[0] == 'Y')) {
                    if (E.filename[0] == '\0') {
                        char *fn = editorPrompt("Save as: ");
                        if (fn) { strncpy(E.filename, fn, sizeof(E.filename)-1); free(fn); }
                        else { free(ans); break; }
                    }
                    editorSave(E.filename);
                }
                if (ans) free(ans);
            }
            // exit
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break; }
        case 'A' + 1000: case 'B' + 1000: case 'C' + 1000: case 'D' + 1000:
            editorMoveCursor(c);
            break;
        default:
            if (!iscntrl(c) && c < 128) {
                editorInsertChar(c);
            }
            break;
    }
}

void initEditor() {
    E.cx = 0; E.cy = 0; E.rowoff = 0; E.coloff = 0;
    E.numrows = 0; E.rows = NULL; E.dirty = 0; E.filename[0] = '\0';
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    // leave one row for status
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
        strncpy(E.filename, argv[1], sizeof(E.filename)-1);
        E.filename[sizeof(E.filename)-1] = '\0';
    }

    snprintf(E.statusmsg, sizeof(E.statusmsg), "Ctrl-S: Save | Ctrl-O: Open | Ctrl-X: Exit");
    E.statusmsg_time = time(NULL);

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
