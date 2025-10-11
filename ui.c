/**
 * collaborative_editor.c
 *
 * This program is a functional terminal-based text editor with a UI mockup
 * for collaborative features and conflict resolution. It supports loading
 * and saving files.
 *
 * How to Compile:
 * gcc -o collaborative_editor collaborative_editor.c -lncurses
 *
 * How to Run:
 * ./collaborative_editor [filename]
 *
 * Controls:
 * - In Editor View:
 * - Arrow Keys: Move your cursor.
 * - Typing: Inserts characters at the cursor position.
 * - Enter Key: Creates a new line.
 * - Backspace Key: Deletes characters. (Use Ctrl+H if Backspace doesn't work)
 * - Ctrl+S: Save the current file.
 * - Ctrl+C: Simulate a merge conflict.
 * - Ctrl+Q: Save and quit the program.
 */

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>  // For isprint()
#include <stdio.h>   // For file I/O
#include <stdarg.h>  // For status message function

// --- Defines ---
// Keybindings for the main editor
#define CTRL_S 19 // ASCII code for Ctrl+S
#define CTRL_C 3  // ASCII code for Ctrl+C
#define CTRL_Q 17 // ASCII code for Ctrl+Q

// Keybindings for the conflict resolver
#define CTRL_K 11 // ASCII for Ctrl+K
#define CTRL_J 10 // ASCII for Ctrl+J
#define CTRL_E 5  // ASCII for Ctrl+E

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

typedef struct {
    char *chars;
    int length;
    int capacity;
} Line;

// --- Global Data ---
Line *document = NULL;
int line_count = 0;
int document_capacity = 0;
char *current_filename = NULL;
char status_message[100];

// Mock data for collaborative features. In a real app, this would be updated via a network.
User users[3];
int locked_line_index = 7; 
const char *local_version[] = { "This line has your local changes,", "which you made while you were", "offline. The change is here." };
const char *server_version[] = { "This line has conflicting remote", "changes that Alice pushed while you", "were gone. The change is here." };

// --- Function Prototypes ---
// Initialization & Cleanup
void init_colors();
void init_users();
void init_document_from_static_buffer();
void init_empty_document();
void free_document();
// File I/O
void save_document(const char *filename);
void load_document(const char *filename);
// Core Editing Logic
void handle_input(int ch);
void insert_char(int y, int x, char ch);
void insert_newline(int y, int x);
void delete_char(int y, int x);
// Main Editor UI
void draw_editor_ui(WINDOW *editor_win, WINDOW *status_bar_win);
void draw_editor_window(WINDOW *win);
void draw_status_bar(WINDOW *win, User current_user);
void move_user_cursor(int key);
void set_status_message(const char *fmt, ...);
// Conflict Resolver UI
void run_conflict_resolver();
void draw_conflict_window_borders(WINDOW *win, const char *title);
void print_text_in_conflict_window(WINDOW *win, const char **text, int count, int highlight_line);

// --- Main Function ---
int main(int argc, char *argv[]) {
    initscr();
    raw();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);

    start_color();
    init_colors();
    init_users();
    status_message[0] = '\0';

    if (argc > 1) {
        current_filename = argv[1];
        load_document(current_filename);
    } else {
        init_document_from_static_buffer();
    }

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW *editor_win = newwin(max_y - 1, max_x, 0, 0);
    WINDOW *status_bar_win = newwin(1, max_x, max_y - 1, 0);

    draw_editor_ui(editor_win, status_bar_win);

    int ch;
    while ((ch = getch()) != CTRL_Q) {
        handle_input(ch);
        draw_editor_ui(editor_win, status_bar_win);
    }
    
    if (current_filename) {
        save_document(current_filename);
    }

    free_document();
    endwin();
    return 0;
}

// --- Initialization & Cleanup ---
void init_colors() {
    init_pair(CURSOR_USER_1, COLOR_WHITE, COLOR_GREEN); init_pair(SELECTION_USER_1, COLOR_BLACK, COLOR_GREEN);
    init_pair(CURSOR_USER_2, COLOR_BLACK, COLOR_CYAN); init_pair(SELECTION_USER_2, COLOR_BLACK, COLOR_CYAN);
    init_pair(CURSOR_USER_3, COLOR_WHITE, COLOR_MAGENTA); init_pair(SELECTION_USER_3, COLOR_BLACK, COLOR_MAGENTA);
    init_pair(STATUS_BAR_PAIR, COLOR_WHITE, COLOR_BLUE); init_pair(LOCKED_LINE_PAIR, COLOR_WHITE, COLOR_BLACK + 8);
    init_pair(HEADER_PAIR, COLOR_WHITE, COLOR_RED); init_pair(OPTIONS_PAIR, COLOR_WHITE, COLOR_BLUE);
    init_pair(HIGHLIGHT_PAIR, COLOR_BLACK, COLOR_YELLOW);
}

void init_users() {
    users[0] = (User){1, "You", 2, 5, -1, -1, CURSOR_USER_1, SELECTION_USER_1};
    users[1] = (User){2, "Alice", 5, 20, 5, 12, CURSOR_USER_2, SELECTION_USER_2};
    users[2] = (User){3, "Bob", 8, 10, -1, -1, CURSOR_USER_3, SELECTION_USER_3};
}

void init_document_from_static_buffer() {
    char* static_buffer[] = {
        "# Collaborative Terminal Editor", "","(No file loaded. Your work will not be saved)",
        "To save your work, provide a filename on startup:", "./collaborative_editor myfile.txt",
        "Press 'Ctrl+C' to trigger the conflict resolution screen.", "",
        "This line is locked by another user.", "Press 'Ctrl+Q' to quit."
    };
    int count = sizeof(static_buffer) / sizeof(char*);
    document_capacity = count;
    document = malloc(document_capacity * sizeof(Line));
    line_count = count;
    for (int i = 0; i < line_count; i++) {
        int len = strlen(static_buffer[i]);
        document[i].length = len;
        document[i].capacity = len + 1;
        document[i].chars = malloc(document[i].capacity);
        strcpy(document[i].chars, static_buffer[i]);
    }
}

void init_empty_document() {
    document_capacity = 1;
    document = malloc(sizeof(Line));
    line_count = 1;
    document[0].length = 0;
    document[0].capacity = 16;
    document[0].chars = malloc(16);
    document[0].chars[0] = '\0';
}

void free_document() {
    if (document == NULL) return;
    for (int i = 0; i < line_count; i++) {
        free(document[i].chars);
    }
    free(document);
}

// --- File I/O ---
void save_document(const char *filename) {
    if (filename == NULL) {
        set_status_message("No filename specified. Cannot save.");
        return;
    }
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        set_status_message("Error: Could not open \"%s\" for writing!", filename);
        return;
    }
    for (int i = 0; i < line_count; i++) {
        fprintf(fp, "%s\n", document[i].chars);
    }
    fclose(fp);
    set_status_message("%d lines written to \"%s\"", line_count, filename);
}

void load_document(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        init_empty_document();
        set_status_message("\"%s\" [New File]", filename);
        return;
    }
    free_document();
    document_capacity = 16;
    document = malloc(document_capacity * sizeof(Line));
    line_count = 0;
    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_size;
    while ((line_size = getline(&line_buf, &line_buf_size, fp)) != -1) {
        if (line_count >= document_capacity) {
            document_capacity *= 2;
            document = realloc(document, document_capacity * sizeof(Line));
        }
        if (line_size > 0 && (line_buf[line_size - 1] == '\n' || line_buf[line_size - 1] == '\r')) {
            line_buf[--line_size] = '\0';
        }
        if (line_size > 0 && line_buf[line_size - 1] == '\r') {
            line_buf[--line_size] = '\0';
        }
        document[line_count].length = line_size;
        document[line_count].capacity = line_size + 1;
        document[line_count].chars = malloc(document[line_count].capacity);
        strcpy(document[line_count].chars, line_buf);
        line_count++;
    }
    free(line_buf);
    fclose(fp);
    if (line_count == 0) init_empty_document();
    set_status_message("Loaded \"%s\"", filename);
}

// --- Core Editing Logic ---
void handle_input(int ch) {
    // Check if the current line is locked before processing input
    if (users[0].cursor_y == locked_line_index) {
        // Allow movement keys but block any editing keys
        if (ch != KEY_UP && ch != KEY_DOWN && ch != KEY_LEFT && ch != KEY_RIGHT) {
            set_status_message("This line is locked by another user and cannot be edited.");
            return; // Ignore the input
        }
    }
    
    switch (ch) {
        case CTRL_C: run_conflict_resolver(); break;
        case CTRL_S: save_document(current_filename); break;
        case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT: move_user_cursor(ch); break;
        case '\n': case KEY_ENTER: insert_newline(users[0].cursor_y, users[0].cursor_x); break;
        case KEY_BACKSPACE: case 127: case 8: delete_char(users[0].cursor_y, users[0].cursor_x); break;
        default: if (isprint(ch)) insert_char(users[0].cursor_y, users[0].cursor_x, ch); break;
    }
}

void insert_char(int y, int x, char ch) {
    Line *line = &document[y];
    if (line->length + 1 >= line->capacity) {
        line->capacity = (line->capacity == 0) ? 16 : line->capacity * 2;
        line->chars = realloc(line->chars, line->capacity);
    }
    memmove(&line->chars[x + 1], &line->chars[x], line->length - x);
    line->chars[x] = ch;
    line->length++;
    line->chars[line->length] = '\0';
    users[0].cursor_x++;
}

void insert_newline(int y, int x) {
    if (line_count + 1 >= document_capacity) {
        document_capacity *= 2;
        document = realloc(document, document_capacity * sizeof(Line));
    }
    Line *current_line = &document[y];
    int len_after = current_line->length - x;
    memmove(&document[y + 2], &document[y + 1], (line_count - (y + 1)) * sizeof(Line));
    line_count++;
    Line *new_line = &document[y + 1];
    new_line->capacity = len_after > 16 ? len_after + 1 : 16;
    new_line->chars = malloc(new_line->capacity);
    memcpy(new_line->chars, &current_line->chars[x], len_after);
    new_line->chars[len_after] = '\0';
    new_line->length = len_after;
    current_line->length = x;
    current_line->chars[x] = '\0';
    users[0].cursor_y++;
    users[0].cursor_x = 0;
}

void delete_char(int y, int x) {
    if (x > 0) {
        Line *line = &document[y];
        memmove(&line->chars[x - 1], &line->chars[x], line->length - x + 1);
        line->length--;
        users[0].cursor_x--;
    } else if (y > 0) {
        Line *prev_line = &document[y - 1];
        Line *current_line = &document[y];
        int new_len = prev_line->length + current_line->length;
        if (new_len + 1 > prev_line->capacity) {
            prev_line->capacity = new_len + 1;
            prev_line->chars = realloc(prev_line->chars, prev_line->capacity);
        }
        int new_cursor_x = prev_line->length;
        memcpy(&prev_line->chars[prev_line->length], current_line->chars, current_line->length + 1);
        prev_line->length = new_len;
        free(current_line->chars);
        memmove(&document[y], &document[y + 1], (line_count - (y + 1)) * sizeof(Line));
        line_count--;
        users[0].cursor_y--;
        users[0].cursor_x = new_cursor_x;
    }
}

// --- UI Functions ---
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
    wclear(win);
    
    if (status_message[0] != '\0') {
        mvwprintw(win, 0, 1, "%s", status_message);
        status_message[0] = '\0';
    } else {
        char left_status[100];
        char *filename_display = current_filename ? current_filename : "[No Name]";
        snprintf(left_status, 100, " EDIT | %s | Ln %d, Col %d ", filename_display, current_user.cursor_y + 1, current_user.cursor_x + 1);
        mvwprintw(win, 0, 1, "%s", left_status);
        char right_status[200];
        snprintf(right_status, 200, "Ctrl+S: Save | Ctrl+Q: Quit ");
        mvwprintw(win, 0, max_x - strlen(right_status) - 1, "%s", right_status);
    }
}

void move_user_cursor(int key) {
    User *u = &users[0];
    switch (key) {
        case KEY_UP: if (u->cursor_y > 0) u->cursor_y--; break;
        case KEY_DOWN: if (u->cursor_y < line_count - 1) u->cursor_y++; break;
        case KEY_LEFT: if (u->cursor_x > 0) u->cursor_x--; break;
        case KEY_RIGHT: if (u->cursor_x < document[u->cursor_y].length) u->cursor_x++; break;
    }
    int current_line_len = document[u->cursor_y].length;
    if (u->cursor_x > current_line_len) {
        u->cursor_x = current_line_len;
    }
}

void set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_message, sizeof(status_message), fmt, ap);
    va_end(ap);
}

void run_conflict_resolver() {
    clear(); // Give the resolver a clean screen to start
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Create all windows
    WINDOW *header_win = newwin(3, max_x, 0, 0);
    int panel_width = max_x / 2;
    int panel_height = max_y - 6;
    WINDOW *local_win = newwin(panel_height, panel_width, 3, 0);
    WINDOW *server_win = newwin(panel_height, panel_width, 3, panel_width);
    WINDOW *options_win = newwin(3, max_x, max_y - 3, 0);

    // Draw header
    wbkgd(header_win, COLOR_PAIR(HEADER_PAIR));
    box(header_win, 0, 0);
    mvwprintw(header_win, 1, 2, "Merge Conflict in `document.txt` - Choose which version to keep.");
    wrefresh(header_win);

    // Draw content windows
    draw_conflict_window_borders(local_win, " YOUR VERSION (Local) ");
    print_text_in_conflict_window(local_win, local_version, 3, 2);
    wrefresh(local_win);

    draw_conflict_window_borders(server_win, " SERVER VERSION (From Alice) ");
    print_text_in_conflict_window(server_win, server_version, 3, 2);
    wrefresh(server_win);
    
    // Draw options bar and loop for input
    const char* options_text = "OPTIONS: (Ctrl+K) Keep Yours | (Ctrl+J) Keep Server's | (Ctrl+E) Edit | (Ctrl+Q) Back";
    
    int ch;
    while ((ch = getch()) != CTRL_Q) {
        werase(options_win);
        wbkgd(options_win, COLOR_PAIR(OPTIONS_PAIR));
        box(options_win, 0, 0);

        switch (ch) {
            case CTRL_K: mvwprintw(options_win, 1, 2, "Chose: KEEP YOUR VERSION. Press Ctrl+Q to return."); break;
            case CTRL_J: mvwprintw(options_win, 1, 2, "Chose: KEEP SERVER VERSION. Press Ctrl+Q to return."); break;
            case CTRL_E: mvwprintw(options_win, 1, 2, "Chose: EDIT MANUALLY (Not Implemented). Press Ctrl+Q to return."); break;
            default: mvwprintw(options_win, 1, 2, "%s", options_text); break;
        }
        wrefresh(options_win);
    }

    // Cleanup
    delwin(header_win);
    delwin(local_win);
    delwin(server_win);
    delwin(options_win);
    clear(); // Clear the resolver UI
    refresh(); // Refresh to show the cleared screen before the main UI redraws
}

void draw_conflict_window_borders(WINDOW *win, const char *title) {
    box(win, 0, 0);
    mvwprintw(win, 0, 2, "%s", title);
}

void print_text_in_conflict_window(WINDOW *win, const char **text, int count, int highlight_line) {
    for (int i = 0; i < count; i++) {
        if (i == highlight_line) {
            wattron(win, COLOR_PAIR(HIGHLIGHT_PAIR));
        }
        mvwprintw(win, i + 2, 2, "%s", text[i]);
        if (i == highlight_line) {
            wattroff(win, COLOR_PAIR(HIGHLIGHT_PAIR));
        }
    }
}

