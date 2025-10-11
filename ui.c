/**
 * collaborative_editor.c
 *
 * This program is a functional terminal-based text editor with a UI mockup
 * for collaborative features and conflict resolution.
 *
 * How to Compile:
 * gcc -o collaborative_editor collaborative_editor.c -lncurses
 *
 * How to Run:
 * ./collaborative_editor
 *
 * Controls:
 * - In Editor View:
 * - Arrow Keys: Move your cursor.
 * - Typing: Inserts characters at the cursor position.
 * - Enter Key: Creates a new line.
 * - Backspace Key: Deletes characters. (Use Ctrl+H if Backspace doesn't work)
 * - 'c': Simulate a merge conflict and switch to the Conflict Resolution screen.
 * - 'q': Quit the program.
 */

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h> // For isprint()

// --- Color Pair Definitions ---
#define CURSOR_USER_1   1
#define SELECTION_USER_1 2
#define CURSOR_USER_2   3
#define SELECTION_USER_2 4
#define CURSOR_USER_3   5
#define SELECTION_USER_3 6
#define STATUS_BAR_PAIR 7
#define LOCKED_LINE_PAIR 8
#define HEADER_PAIR 9
#define OPTIONS_PAIR 10
#define HIGHLIGHT_PAIR 11

// --- Data Structures ---
typedef struct {
    int id;
    const char* name;
    int cursor_y;
    int cursor_x;
    int selection_start_y;
    int selection_start_x;
    short cursor_color_pair;
    short selection_color_pair;
} User;

// NEW: Structure for an editable line of text
typedef struct {
    char *chars;    // The characters on the line
    int length;     // Number of characters
    int capacity;   // Allocated memory size
} Line;

// --- Global Data ---
// NEW: Dynamic document structure
Line *document = NULL;
int line_count = 0;
int document_capacity = 0;

// Simulated data for other users and UI mockups
int locked_line_index = 7;
User users[3];
const char *local_version[] = { "This line has your local changes,", "which you made while you were", "offline. The change is here." };
const char *server_version[] = { "This line has conflicting remote", "changes that Alice pushed while you", "were gone. The change is here." };

// --- Function Prototypes ---
// Initialization & Cleanup
void init_colors();
void init_users();
void init_document_from_static_buffer();
void free_document();
// Core Editing Logic (NEW)
void handle_input(int ch);
void insert_char(int y, int x, char ch);
void insert_newline(int y, int x);
void delete_char(int y, int x);
// Main Editor UI
void draw_editor_ui(WINDOW *editor_win, WINDOW *status_bar_win);
void draw_editor_window(WINDOW *win);
void draw_status_bar(WINDOW *win, User current_user);
void move_user_cursor(int key);
// Conflict Resolver UI
void run_conflict_resolver();
void draw_conflict_window_borders(WINDOW *win, const char *title);
void print_text_in_conflict_window(WINDOW *win, const char **text, int count, int highlight_line);

// --- Main Function ---
int main() {
    initscr();
    raw(); // Use raw() for better control over input like backspace
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);

    start_color();
    init_colors();
    init_users();
    init_document_from_static_buffer();

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW *editor_win = newwin(max_y - 1, max_x, 0, 0);
    WINDOW *status_bar_win = newwin(1, max_x, max_y - 1, 0);

    draw_editor_ui(editor_win, status_bar_win);

    int ch;
    while ((ch = getch()) != 'q') {
        handle_input(ch);
        draw_editor_ui(editor_win, status_bar_win);
    }

    free_document();
    endwin();
    return 0;
}

// --- Initialization & Cleanup Functions ---
void init_colors() {
    init_pair(CURSOR_USER_1, COLOR_WHITE, COLOR_GREEN);
    init_pair(SELECTION_USER_1, COLOR_BLACK, COLOR_GREEN);
    init_pair(CURSOR_USER_2, COLOR_BLACK, COLOR_CYAN);
    init_pair(SELECTION_USER_2, COLOR_BLACK, COLOR_CYAN);
    init_pair(CURSOR_USER_3, COLOR_WHITE, COLOR_MAGENTA);
    init_pair(SELECTION_USER_3, COLOR_BLACK, COLOR_MAGENTA);
    init_pair(STATUS_BAR_PAIR, COLOR_WHITE, COLOR_BLUE);
    init_pair(LOCKED_LINE_PAIR, COLOR_WHITE, COLOR_BLACK + 8);
    init_pair(HEADER_PAIR, COLOR_WHITE, COLOR_RED);
    init_pair(OPTIONS_PAIR, COLOR_WHITE, COLOR_BLUE);
    init_pair(HIGHLIGHT_PAIR, COLOR_BLACK, COLOR_YELLOW);
}

void init_users() {
    users[0] = (User){1, "You", 2, 5, -1, -1, CURSOR_USER_1, SELECTION_USER_1};
    users[1] = (User){2, "Alice", 5, 20, 5, 12, CURSOR_USER_2, SELECTION_USER_2};
    users[2] = (User){3, "Bob", 8, 10, -1, -1, CURSOR_USER_3, SELECTION_USER_3};
}

void init_document_from_static_buffer() {
    char* static_buffer[] = {
        "# Collaborative Terminal Editor", "",
        "This is a functional, terminal-based text editor.",
        "The editor is being built in C, using the ncurses library for UI rendering.",
        "Press 'c' to trigger the conflict resolution screen.",
        "For example, User2 has a selection on this line.", "",
        "This line is locked by another user, indicated by the background highlight.",
        "You shouldn't be able to edit it.", "", "Press 'q' to quit."
    };
    int initial_line_count = sizeof(static_buffer) / sizeof(char*);

    document_capacity = initial_line_count;
    document = malloc(document_capacity * sizeof(Line));
    line_count = initial_line_count;

    for (int i = 0; i < line_count; i++) {
        int len = strlen(static_buffer[i]);
        document[i].length = len;
        document[i].capacity = len + 1;
        document[i].chars = malloc(document[i].capacity);
        strcpy(document[i].chars, static_buffer[i]);
    }
}

void free_document() {
    for (int i = 0; i < line_count; i++) {
        free(document[i].chars);
    }
    free(document);
}


// --- Core Editing Logic ---
void handle_input(int ch) {
    switch (ch) {
        case 'c':
            run_conflict_resolver();
            clear();
            refresh();
            break;
        case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT:
            move_user_cursor(ch);
            break;
        case '\n': // Enter key
        case KEY_ENTER:
            insert_newline(users[0].cursor_y, users[0].cursor_x);
            break;
        case KEY_BACKSPACE:
        case 127: // Backspace might also be 127
        case 8:   // Or 8 (Ctrl+H)
            delete_char(users[0].cursor_y, users[0].cursor_x);
            break;
        default:
            if (isprint(ch)) {
                insert_char(users[0].cursor_y, users[0].cursor_x, ch);
            }
            break;
    }
}

void insert_char(int y, int x, char ch) {
    Line *line = &document[y];
    if (line->length + 1 >= line->capacity) {
        line->capacity *= 2;
        line->chars = realloc(line->chars, line->capacity);
    }
    memmove(&line->chars[x + 1], &line->chars[x], line->length - x);
    line->chars[x] = ch;
    line->length++;
    line->chars[line->length] = '\0'; // Null-terminate
    users[0].cursor_x++;
}

void insert_newline(int y, int x) {
    if (line_count + 1 >= document_capacity) {
        document_capacity *= 2;
        document = realloc(document, document_capacity * sizeof(Line));
    }

    Line *current_line = &document[y];
    char *text_after_cursor = &current_line->chars[x];
    int len_after = current_line->length - x;

    // Create the new line
    memmove(&document[y + 2], &document[y + 1], (line_count - (y + 1)) * sizeof(Line));
    line_count++;
    
    Line *new_line = &document[y + 1];
    new_line->capacity = len_after > 16 ? len_after + 1 : 16;
    new_line->chars = malloc(new_line->capacity);
    memcpy(new_line->chars, text_after_cursor, len_after);
    new_line->chars[len_after] = '\0';
    new_line->length = len_after;

    // Truncate the old line
    current_line->length = x;
    current_line->chars[x] = '\0';
    
    // Move cursor
    users[0].cursor_y++;
    users[0].cursor_x = 0;
}

void delete_char(int y, int x) {
    if (x > 0) { // Standard backspace within a line
        Line *line = &document[y];
        memmove(&line->chars[x - 1], &line->chars[x], line->length - x);
        line->length--;
        line->chars[line->length] = '\0';
        users[0].cursor_x--;
    } else if (y > 0) { // Backspace at start of line, merge with previous
        Line *prev_line = &document[y - 1];
        Line *current_line = &document[y];
        int new_len = prev_line->length + current_line->length;

        if (new_len + 1 > prev_line->capacity) {
            prev_line->capacity = new_len + 1;
            prev_line->chars = realloc(prev_line->chars, prev_line->capacity);
        }
        
        memcpy(&prev_line->chars[prev_line->length], current_line->chars, current_line->length);
        int new_cursor_x = prev_line->length;
        prev_line->length = new_len;
        prev_line->chars[new_len] = '\0';
        
        // Free the now-empty current line's memory
        free(current_line->chars);
        // Shift all subsequent lines up
        memmove(&document[y], &document[y + 1], (line_count - (y + 1)) * sizeof(Line));
        line_count--;

        users[0].cursor_y--;
        users[0].cursor_x = new_cursor_x;
    }
}


// --- Main Editor UI Functions ---
void draw_editor_ui(WINDOW *editor_win, WINDOW *status_bar_win) {
    werase(editor_win);
    werase(status_bar_win);
    draw_editor_window(editor_win);
    draw_status_bar(status_bar_win, users[0]);
    wrefresh(editor_win);
    wrefresh(status_bar_win);
}

void draw_editor_window(WINDOW *win) {
    for (int i = 0; i < line_count; ++i) {
        if (i == locked_line_index) {
            mvwchgat(win, i + 1, 1, -1, A_NORMAL, LOCKED_LINE_PAIR, NULL);
        }
        mvwprintw(win, i + 1, 2, "%d ", i + 1);
        mvwprintw(win, i + 1, 5, "%s", document[i].chars);
    }
    for (int i = 0; i < 3; ++i) {
        User u = users[i];
        if (u.selection_start_y != -1) {
             mvwchgat(win, u.selection_start_y + 1, u.selection_start_x + 5, (u.cursor_x - u.selection_start_x), A_BOLD, u.selection_color_pair, NULL);
        }
        char char_under_cursor = mvwinch(win, u.cursor_y + 1, u.cursor_x + 5) & A_CHARTEXT;
        char_under_cursor = (char_under_cursor == 0 || char_under_cursor == ' ') ? ' ' : char_under_cursor;
        wattron(win, COLOR_PAIR(u.cursor_color_pair));
        mvwaddch(win, u.cursor_y + 1, u.cursor_x + 5, char_under_cursor);
        wattroff(win, COLOR_PAIR(u.cursor_color_pair));
    }
    box(win, 0, 0);
}

void draw_status_bar(WINDOW *win, User current_user) {
    wbkgd(win, COLOR_PAIR(STATUS_BAR_PAIR));
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);
    char left_status[100];
    snprintf(left_status, 100, " EDIT | document.txt | Ln %d, Col %d ", current_user.cursor_y + 1, current_user.cursor_x + 1);
    mvwprintw(win, 0, 1, "%s", left_status);
    char right_status[200];
    snprintf(right_status, 200, "Active: You, Alice, Bob | [Online] ");
    mvwprintw(win, 0, max_x - strlen(right_status) - 1, "%s", right_status);
}

void move_user_cursor(int key) {
    User *u = &users[0];
    switch (key) {
        case KEY_UP: if (u->cursor_y > 0) u->cursor_y--; break;
        case KEY_DOWN: if (u->cursor_y < line_count - 1) u->cursor_y++; break;
        case KEY_LEFT: if (u->cursor_x > 0) u->cursor_x--; break;
        case KEY_RIGHT: if (u->cursor_x < document[u->cursor_y].length) u->cursor_x++; break;
    }
    // Snap cursor to end of line if it's past the end
    int current_line_len = document[u->cursor_y].length;
    if (u->cursor_x > current_line_len) {
        u->cursor_x = current_line_len;
    }
}


// --- Conflict Resolver UI Functions ---
void run_conflict_resolver() {
    clear();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW *header_win = newwin(3, max_x, 0, 0);
    int panel_width = max_x / 2;
    int panel_height = max_y - 6;
    WINDOW *local_win = newwin(panel_height, panel_width, 3, 0);
    WINDOW *server_win = newwin(panel_height, panel_width, 3, panel_width);
    WINDOW *options_win = newwin(3, max_x, max_y - 3, 0);
    
    wbkgd(header_win, COLOR_PAIR(HEADER_PAIR));
    mvwprintw(header_win, 1, 2, "Merge Conflict in `document.txt` - Choose which version to keep.");
    box(header_win, 0, 0);
    draw_conflict_window_borders(local_win, " YOUR VERSION (Local) ");
    draw_conflict_window_borders(server_win, " SERVER VERSION (From Alice) ");
    print_text_in_conflict_window(local_win, local_version, 3, 2);
    print_text_in_conflict_window(server_win, server_version, 3, 2);
    wbkgd(options_win, COLOR_PAIR(OPTIONS_PAIR));
    box(options_win, 0, 0);
    const char* options_text = "OPTIONS: (k) Keep Your Version | (j) Keep Server Version | (e) Edit Manually | (q) Back to Editor";
    mvwprintw(options_win, 1, 2, "%s", options_text);

    wrefresh(header_win); wrefresh(local_win); wrefresh(server_win); wrefresh(options_win);

    int ch;
    while ((ch = getch()) != 'q') {
        wattron(options_win, COLOR_PAIR(OPTIONS_PAIR));
        mvwprintw(options_win, 1, 2, "%*s", max_x - 4, "");
        switch (ch) {
            case 'k': mvwprintw(options_win, 1, 2, "Chose: KEEP YOUR VERSION. Press 'q' to return."); break;
            case 'j': mvwprintw(options_win, 1, 2, "Chose: KEEP SERVER VERSION. Press 'q' to return."); break;
            case 'e': mvwprintw(options_win, 1, 2, "Chose: EDIT MANUALLY. Press 'q' to return."); break;
            default: mvwprintw(options_win, 1, 2, "%s", options_text); break;
        }
        wrefresh(options_win);
    }
    delwin(header_win); delwin(local_win); delwin(server_win); delwin(options_win);
}

void draw_conflict_window_borders(WINDOW *win, const char *title) {
    box(win, 0, 0);
    mvwprintw(win, 0, 2, "%s", title);
}

void print_text_in_conflict_window(WINDOW *win, const char **text, int count, int highlight_line) {
    for (int i = 0; i < count; i++) {
        if (i == highlight_line) wattron(win, COLOR_PAIR(HIGHLIGHT_PAIR));
        mvwprintw(win, i + 2, 2, "%s", text[i]);
        if (i == highlight_line) wattroff(win, COLOR_PAIR(HIGHLIGHT_PAIR));
    }
}

