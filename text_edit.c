//Define a feature test macro.
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define CHECK_QUIT 1
#define TAB_STOP 8
#define CTRL_KEY(k) ((k) & 0x1f)

enum specialKey {
    BACKSPACE = 127,
    //Constants that represents the arrow keys that don't conflict with any
    //ordinary keypresses.
    ARROW_UP = 1000,
    //1001
    ARROW_DOWN,
    //1002
    ARROW_LEFT,
    //1003
    ARROW_RIGHT
};

/*** data ***/

//store a row of text in the editor.
typedef struct erow {
    //size of the row.
    int size;
    char *chars;
    //size of the contents of render.
    int size_r;
    char *render;
} erow;


struct abuf {
    //Pointer to our buffer memory
    char *b;
    //Length of the buffer
    int len;
};

//Define an empty buffer.
#define ABUF_INIT {NULL, 0}


struct Config {
    //Keep track of what row and col the cursor is within the text file.
    int cursor_x, cursor_y;
    //Index into the render field.
    int render_x;
    //Keep track of what row/col of the file the user is currently scrolled to.
    int rowoff;
    int coloff;
    //Number of rows and column in the screen.
    int screenrows;
    int screencols;
    //Total number of rows written in a file.
    int numrows;
    //Array of erow structs.
    erow *row;
    char *filename;
    int updated;
    //Status message in the status bar.
    char message[100];
    //Timestamp for the status message to erase it few seconds after displayed.
    time_t message_time;
    //original terminal attribute
    struct termios orig_attribute;
};

//Variable containing state of the text file.
struct Config T;

void updateStatusBar(const char *fmt, ...);
void refreshScreen();
char *getNewFileName(char *s);


/*** terminal ***/

void error_exit(const char *error_type) {
    /*
    Exit the program when there's an error.
    */

    //Clear the screen and change the position of cursor on exit.
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    //Look at the global errno variable and prints a descriptive error message.
    perror(error_type);
    exit(1);
}

int readOneKey() {
    /*
    Wait for one keypress and return it.
    */
    int read_key;
    char key_val;
    while ((read_key = read(STDIN_FILENO, &key_val, 1)) != 1) {
        if (read_key == -1 && errno != EAGAIN) error_exit("read");
    }
    //If it reads an escape character, read two more bytes into next buffer.
    if (key_val == '\x1b') {
        char next[3];

        if (read(STDIN_FILENO, &next[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &next[1], 1) != 1) return '\x1b';

        if (next[0] == '[') {
            if (next[1] < '0' || next[1] > '9') {
                switch (next[1]) {
                    //'\xlb[A' is arrow up key.
                    case 'A': return ARROW_UP;
                    //'\xlb[B' is arrow down key.
                    case 'B': return ARROW_DOWN;
                    //'\xlb[C' is arrow right key.
                    case 'C': return ARROW_RIGHT;
                    //'\xlb[D' is arrow left key.
                    case 'D': return ARROW_LEFT;
                }
            }
        }

        return '\x1b';
    } else {
        return key_val;
    }
}

/*** get screen size ***/
int getCursorPosition(int *rows, int *cols) {
    /*
    Get the position of the cursor.
    */
    unsigned int i = 0;
    char buf[32];

    //Get cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    //Read each character in the buffer.
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    //Assign '\0' to the final byte of buf.
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    //Skip '\x1b' and '[', and parse two integers of row and col.
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    //If ioctl() fail, manually find the row and col by moving the cursor to the
    //bottom right corner.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        //Move the cursor to the bottom-right of the screen.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);

    //If succeed, ioctl() put the number of column and row into winsize struct.
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

int convertToRender(erow *row, int cursor_x) {
    int render_x = 0;
    int j;
    for (j = 0; j < cursor_x; j++) {
        if (row->chars[j] == '\t')
            //Add how many columns left to the next tab stop.
            render_x += (TAB_STOP - 1) - (render_x % TAB_STOP);
        render_x++;
    }
    return render_x;
}

void updateRender(erow *row) {
    /*
    Use chars string of an erow to fill the contents of the render string.
    */
    int tabs = 0;
    int j;

    //Loop through the chars of the row and count the tabs to know how much
    //memory to allocate for render.
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

    int idx = 0;

    //Copy each character from chars to render.
    for (j = 0; j < row->size; j++) {
        //If the current character is a tab, append space until a column that
        //is divisible by the tab stop.
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    //Set the size of the render to the size of the char.
    row->render[idx] = '\0';
    row->size_r = idx;
}

void insertRow(int at, char *s, size_t len) {
    /*
    Insert the context in the row.
    */

    //Validate the index of the column.
    if (at < 0 || at > T.numrows) return;

    //Allocate space for a new row.
    T.row = realloc(T.row, sizeof(erow) * (T.numrows + 1));

    //Make room at the specified index for the new row.
    memmove(&T.row[at + 1], &T.row[at], sizeof(erow) * (T.numrows - at));

    //Set the current row size.
    T.row[at].size = len;

    //Put the contents in the row into 'chars'.
    T.row[at].chars = malloc(len + 1);
    memcpy(T.row[at].chars, s, len);
    T.row[at].chars[len] = '\0';

    //Initialize the render.
    T.row[at].size_r = 0;
    T.row[at].render = NULL;
    updateRender(&T.row[at]);

    //Increment the number of rows in the current file.
    T.numrows++;
    //Increment the number of changes made since saving the file.
    T.updated++;
}

void freeRow(erow *row) {
    /*
    Free the memory of the row that is deleted.
    */
    free(row->render);
    free(row->chars);
}

void deleteRow(int at) {
    /*
    Delete a row.
    */

    //Validate the index of the column.
    if (at < 0 || at >= T.numrows) return;
    freeRow(&T.row[at]);
    //Overwrite the deleted rwo struct with the rest of the rows
    memmove(&T.row[at], &T.row[at + 1], sizeof(erow) * (T.numrows - at - 1));
    T.numrows--;
    T.updated++;
}

void insertCharFromKey(erow *row, int at, int c) {
    /*
    Insert a single character into the position.
    */

    //Validate the index(col) that character will be inserted into.
    if (at < 0 || at > row->size) at = row->size;
    //Allocate spaces for chars of the erow
    row->chars = realloc(row->chars, row->size + 2);

    //Make room for the new character.
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

    //Increment the size and assign the character to its position in the array.
    row->size++;
    row->chars[at] = c;

    //Update render and size_r with new row content.
    updateRender(row);
    T.updated++;
}

void appendTwoRows(erow *row, char *s, size_t len) {
    /*
    Appends a string to the end of the row.
    */
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    updateRender(row);
    T.updated++;
}

void deleteChar(erow *row, int at) {
    /*
    Delete a character in the row.
    */
    if (at < 0 || at >= row->size) return;
    //Overwrite the deleted character with the charctger that come after it.
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    //Decrement the size of the row.
    row->size--;

    updateRender(row);
    T.updated++;
}

/*** editor operations ***/

void insertChar(int c) {
    /*
    Take a character and insert into the position that cursor is at.
    */

    //Append new row to the file when the cursor is on the last line.
    if (T.cursor_y == T.numrows) {
        insertRow(T.numrows, "", 0);
    }
    //Insert the character.
    insertCharFromKey(&T.row[T.cursor_y], T.cursor_x, c);
    //Move the cursor forward.
    T.cursor_x++;
}

void createNewLine() {
    /*
    Insert a new line when Enter is pressed.
    */

    //If the cursor was at the beginning of a line, insert a new blank row.
    if (T.cursor_x == 0) {
        insertRow(T.cursor_y, "", 0);
    //Otherwise, split the line into two rows.
    } else {
        erow *row = &T.row[T.cursor_y];
        //Create a new row with characters that are in the right of the cursor.
        insertRow(T.cursor_y + 1, &row->chars[T.cursor_x], row->size - T.cursor_x);
        row = &T.row[T.cursor_y];
        //Truncate the current row's contents to contain only characters on the
        //left.
        row->size = T.cursor_x;
        row->chars[row->size] = '\0';
        updateRender(row);
    }
    //Move the cursor to the beginning of the next new line.
    T.cursor_y++;
    T.cursor_x = 0;
}

void processDelete() {
    /*
    Delete the character that is to the left of the cursor.
    */

    //Return if the cursor past the end of the file or it's in the beginning.
    if (T.cursor_y == T.numrows) return;
    if (T.cursor_x == 0 && T.cursor_y == 0) return;

    erow *row = &T.row[T.cursor_y];
    if (T.cursor_x > 0) {
        deleteChar(row, T.cursor_x - 1);
        //Move the cursor to the left.
        T.cursor_x--;
    //If the cursor was at the beginning of the line, append the two rows and
    //delete the current row.
    } else {
        T.cursor_x = T.row[T.cursor_y - 1].size;
        appendTwoRows(&T.row[T.cursor_y - 1], row->chars, row->size);
        deleteRow(T.cursor_y);
        T.cursor_y--;
    }
}


/*** file ***/


void openFile(char *filename) {
    /*
    Open the file to edit.
    */
    free(T.filename);
    //Set the file name to the filename variable.
    T.filename = strdup(filename);

    //Open the file for reading.
    FILE *fp = fopen(filename, "r");
    if (!fp) error_exit("fopen");

    char *line = NULL;

    //Set the line capacity to 0.
    size_t linecap = 0;
    ssize_t linelen;

    //Get the line and the length from getline().
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        //Find the actual length of the line by reducing the linelen until the
        //last character is '\n' or '\r'
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                line[linelen - 1] == '\r'))
            linelen--;

        //Append the current row.
        insertRow(T.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    T.updated = 0;
}

char *rowsToString(int *len) {
    /*
    Convert the array of erow structs into a single string to write out as a
    file.
    */
    int totlen = 0;
    int j;

    //Add up the lengths of each row of text.
    for (j = 0; j < T.numrows; j++)
        totlen += T.row[j].size + 1;

    //Save the total length.
    *len = totlen;

    char *buf = malloc(totlen);
    char *p = buf;

    //Loop through the rows and copy the contents of each row to the buffer.
    for (j = 0; j < T.numrows; j++) {
        memcpy(p, T.row[j].chars, T.row[j].size);
        p += T.row[j].size;
        //Add a new line character after each row.
        *p = '\n';
        p++;
    }

    return buf;
}


void saveFile() {
    //Get the new name of the file if it's not an existing file.
    if (T.filename == NULL) {
        T.filename = getNewFileName("Save as: %s (ESC to cancel)");
        if (T.filename == NULL) {
            updateStatusBar("Save aborted");
            return;
        }
    }

    int len;
    //Change the contexts into string.
    char *buf = rowsToString(&len);

    //Write the string to the path.
    int fd = open(T.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        //ftruncate() sets the file size to specific length.
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                T.updated = 0;
                updateStatusBar("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    //Notify the user that save failed.
    updateStatusBar("Can't save! I/O error: %s", strerror(errno));
}


void appendBuffer(struct abuf *ab, const char *s, int len) {
    /*
    Append a string to abuf.
    */

    //Allocate enough memory to hold the new string.
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;

    //Copy the new string after the end of the current data in the buffer.
    memcpy(&new[ab->len], s, len);
    //Update the pointer and length of the abuf to the new values.
    ab->b = new;
    ab->len += len;
}

void freeBuffer(struct abuf *ab) {
    /*
    Deallocates the dynamice memory used by an abuf.
    */

    free(ab->b);
}

/*** output ***/

void controlScroll() {
    /*
    Set the value of rowoff/coloff (what row of the file user is scrolled to).
    If the cursor has moved outside of the visible window, adjust it so
    that the cursor is just inside the visible window.
    */

    T.render_x = 0;
    if (T.cursor_y < T.numrows) {
        T.render_x = convertToRender(&T.row[T.cursor_y], T.cursor_x);
    }

    //If the cursor is above the visible window, scroll up to where the cursor
    //is.
    if (T.cursor_y < T.rowoff) {
        T.rowoff = T.cursor_y;
    }
    //If the cursor is past the bottom of the visible window, scroll down.
    if (T.cursor_y >= T.rowoff + T.screenrows) {
        T.rowoff = T.cursor_y - T.screenrows + 1;
    }
    //If the cursor is past the left of the visible window, scroll left.
    if (T.render_x < T.coloff) {
        T.coloff = T.render_x;
    }

    //If the cursor is past the right of the visible window, scroll right.
    if (T.render_x >= T.coloff + T.screencols) {
        T.coloff = T.render_x - T.screencols + 1;
    }
}

void createRows(struct abuf *ab) {
    /*
    Draw rows of the text editor.
    */
    int y;
    for (y = 0; y < T.screenrows; y++) {
        //Get the row of the file we want to display at each y position.
        int filerow = y + T.rowoff;
        if (filerow >= T.numrows) {
            //Write welcome message only when program starts a new file, not
            //when they open a existing file.
            if (T.numrows == 0 && y == T.screenrows / 3) {
                //Write welcome message.
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                "Encrypted Text Editor");

                //Truncate the length of the string if terminal can't fit.
                if (welcomelen > T.screencols) welcomelen = T.screencols;

                //Center the welcome message
                int padding = (T.screencols - welcomelen) / 2;
                if (padding) {
                    appendBuffer(ab, "", 1);
                    padding--;
                }
                while (padding--) appendBuffer(ab, " ", 1);
                appendBuffer(ab, welcome, welcomelen);
            } else {
                appendBuffer(ab, "", 1);
            }
        } else {

            int len = T.row[filerow].size_r - T.coloff;
            //When user scrolled horizontally past the end of the line, set
            //len to 0.
            if (len < 0) len = 0;

            //Truncate the length of the string if terminal can't fit.
            if (len > T.screencols) len = T.screencols;
            appendBuffer(ab, &T.row[filerow].render[T.coloff], len);
        }
        //Only erase the current line to the right of the cursor.
        appendBuffer(ab, "\x1b[K", 3);

        appendBuffer(ab, "\r\n", 2);
    }
}

void createStatusBar(struct abuf *ab) {
    /*
    Draw a status bar in the document.
    */

    //Invert color for the status bar.
    appendBuffer(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    //Set the text to display in the status bar.
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        T.filename ? T.filename : "[Document 1]", T.numrows,
        T.updated ? "(not up to date)" : "");
    //Set a text that displays the current line number.
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
        T.cursor_y + 1, T.numrows);

    //Truncate the text if it's longer than width of the screen.
    if (len > T.screencols) len = T.screencols;
    appendBuffer(ab, status, len);

    //Show the current line number aligned to the right of the screen.
    while (len < T.screencols) {
        if (T.screencols - len == rlen) {
            appendBuffer(ab, rstatus, rlen);
            break;
        } else {
            appendBuffer(ab, " ", 1);
            len++;
        }
    }
    //Go back to normal text formatting.
    appendBuffer(ab, "\x1b[m", 3);
    //Make another line for message bar.
    appendBuffer(ab, "\r\n", 2);
}


void createMessageBar(struct abuf *ab) {
    /*
    Draw a message bar in the document.
    */

    //Clear the message bar.
    appendBuffer(ab, "\x1b[K", 3);
    int msglen = strlen(T.message);
    //Truncate if the message is longer than the width of the screen.
    if (msglen > T.screencols) msglen = T.screencols;

    //Display the message only if the message is less than 5 secs old.
    if (msglen && time(NULL) - T.message_time < 5)
        appendBuffer(ab, T.message, msglen);
}

void refreshScreen() {
    /*
    Refresh the screen for text editor for each keypress.
    */
    controlScroll();

    struct abuf ab = ABUF_INIT;
    //Hide the cursor before refreshing the screen.
    appendBuffer(&ab, "\x1b[?25l", 6);
    // Reposition the cursor back up at the top-left corner.
    appendBuffer(&ab, "\x1b[H", 3);

    createRows(&ab);
    createStatusBar(&ab);
    createMessageBar(&ab);

    char buf[32];

    //Move the cursor to the position where the current cursor is. Subtract
    //rowoff and coloff to find the position of the cursor on the screen, not
    //within the text file.
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (T.cursor_y - T.rowoff) + 1,
                                            (T.render_x - T.coloff) + 1);
    appendBuffer(&ab, buf, strlen(buf));

    //Show the cursor again after the refresh.
    appendBuffer(&ab, "\x1b[?25h", 6);

    //Write the buffer's contents out to standard output.
    write(STDOUT_FILENO, ab.b, ab.len);
    freeBuffer(&ab);
}

void updateStatusBar(const char *fmt, ...) {
    /*
    Set the status message in the status bar.
    */
    va_list ap;
    va_start(ap, fmt);
    //Store the string in E.message. vsnprintf reads the format string and
    //call va_arg() to get each argument.
    vsnprintf(T.message, sizeof(T.message), fmt, ap);
    va_end(ap);

    //Set to the current time.
    T.message_time = time(NULL);
}

/*** input ***/

char *getNewFileName(char *s) {
    /*
    */
    size_t bufsize = 128;
    //User's input is stored in buf.
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';
    //Repeatedly set the status message, refresh screen, and wait for keypress.
    while (1) {
        updateStatusBar(s, buf);
        refreshScreen();

        int c = readOneKey();
        //Enable deleting.
        if (c == BACKSPACE || c == CTRL_KEY('h') || c == CTRL_KEY('d')) {
            if (buflen != 0) buf[--buflen] = '\0';
        //If the input is cancelled, free the buf and return NULL.
        } else if (c == '\x1b') {
            updateStatusBar("");
            free(buf);
            return NULL;
        //If the user input is Enter and input is not empty, clear the status
        //message and return the input.
        } else if (c == '\r') {
            if (buflen != 0) {
                updateStatusBar("");
                return buf;
            }
        //If it's a printable character, append it to buf.
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
}

void moveCursorWithArrows(int key) {
    /*
    Move the cursor based on the arrow key pressed.
    */

    //Point to the erow that the cursor is on when the cursor is on an
    //actual line.
    erow *row = (T.cursor_y >= T.numrows) ? NULL : &T.row[T.cursor_y];

    switch (key) {
        case ARROW_LEFT:
        //Prevent moving the cursor off screen.
        if (T.cursor_x != 0) {
            T.cursor_x--;
        //Move to the end of previous line if it was in the beginning of line.
        } else if (T.cursor_y > 0) {
            T.cursor_y--;
            T.cursor_x = T.row[T.cursor_y].size;
        }
        break;
        case ARROW_RIGHT:
        //Move cursor to the right if the cursor is to the left of the end of
        //the line.
        if (row && T.cursor_x < row->size) {
            T.cursor_x++;
        //Move to the beginning of the next line if it was end of a line.
        } else if (row && T.cursor_x == row->size) {
            T.cursor_y++;
            T.cursor_x = 0;
        }
        break;
        case ARROW_UP:
        //Prevent moving the cursor off screen.
        if (T.cursor_y != 0) {
            T.cursor_y--;
        }
        break;
        case ARROW_DOWN:
        //Prevent moving the cursor off screen.
        if (T.cursor_y < T.numrows) {
            T.cursor_y++;
        }
        break;
    }

    //Prevent a case when the cursor points to a different line and be off to
    //the right of the end of the line it's now on.
    row = (T.cursor_y >= T.numrows) ? NULL : &T.row[T.cursor_y];
    int rowlen = row ? row->size : 0;
    if (T.cursor_x > rowlen) {
        T.cursor_x = rowlen;
    }
}

void processKeypress() {
    /*
    Wait for a keypress and then handles it.
    */
    static int quit_times = CHECK_QUIT;

    int c = readOneKey();

    switch (c) {
        //Enter key.
        case '\r':
        createNewLine();
        break;

        case CTRL_KEY('q'):
        if (T.updated && quit_times > 0) {
            //Ask one more time before quitting if there's unsaved changes.
            updateStatusBar("WARNING!!! File has unsaved changes. "
            "Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }
        //Clear the entire screen.
        write(STDOUT_FILENO, "\x1b[2J", 4);
        //Reposition cursor to the top-left corner.
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

        case CTRL_KEY('s'):
        saveFile();
        break;


        case BACKSPACE:
        case CTRL_KEY('h'):
        case CTRL_KEY('d'):
        processDelete();
        break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        moveCursorWithArrows(c);
        break;

        case CTRL_KEY('l'):
        case '\x1b':
        break;

        //Insert the character if the key is not a special key.
        default:
        insertChar(c);
        break;
    }

    quit_times = CHECK_QUIT;
}

/*** mode change ***/

void endRawMode() {
    /*
    Disable raw mode when exit.
    */
    //Use tcgetattr() to read the current attributes into a struct.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &T.orig_attribute) == -1)
        error_exit("tcsetattr");
}

void startRawMode() {
    /*
    Enable raw mode to read the input byte-by-byte.
    */

    if (tcgetattr(STDIN_FILENO, &T.orig_attribute) == -1) error_exit("tcgetattr");
    //Call endRawMode when the program exits.
    atexit(endRawMode);

    //Save a copy of terminos struct in its original state.
    struct termios raw = T.orig_attribute;

    //Use input flags to disable special key values (Ctrl-S, Ctrl-Q, etc.)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    //Use output flags to turn off all output processing.
    raw.c_oflag &= ~(OPOST);
    //Use control flag to set the character size to 8 bits per byte.
    raw.c_cflag |= (CS8);
    //Use local flags to turn off canonical mode and ECHO mode, and other
    //special key functions.
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    //Set the minimum number of bytes of input needed to 0 so that read() returns
    //as soon as there is any input to be read.
    raw.c_cc[VMIN] = 0;
    //Set the maximum amount of time to wait before read() returns.
    raw.c_cc[VTIME] = 1;

    //Enable raw mode unless there's error.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) error_exit("tcsetattr");
}

/*** init  ***/

void initialize() {
    /*
    Initialize all the fields in the E struct.
    */
    //Initialize the coordinate of the cursor to top-left of the screen.
    T.cursor_x = 0;
    T.cursor_y = 0;
    T.render_x = 0;
    //Initialize to be scrolled to the top of the file.
    T.rowoff = 0;
    T.coloff = 0;
    T.numrows = 0;
    T.row = NULL;
    T.updated = 0;
    //Will stay NULL if a new file is created instead of opening existing one.
    T.filename = NULL;
    T.message[0] = '\0';
    T.message_time = 0;

    if (getWindowSize(&T.screenrows, &T.screencols) == -1) error_exit("getWindowSize");

    //Skip the last two lines for a status bar.
    T.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    startRawMode();
    initialize();

    //If ran this file with argument(file), open the file.
    if (argc >= 2) {
        openFile(argv[1]);
    }

    updateStatusBar("Ctrl-S = save | Ctrl-Q = quit");

    while (1) {
        refreshScreen();
        processKeypress();
    }

    return 0;
}
