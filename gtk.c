#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdlib.h> // For rand()
#include <time.h>   // For time()
#include <pango/pangocairo.h> // For drawing text labels
#include <math.h> // For floor() in HSV conversion

// --- Forward Declaration ---
static void create_editor_window(GtkApplication *app, GFile *file_to_open);

// --- Structures ---

// A struct to hold pointers to widgets we need in different functions.
typedef struct {
    GtkTextView *textview;
    GtkWindow *window;
    GtkLabel *word_count_label;
    GtkLabel *cursor_pos_label;
    GFile *current_file; // Holds the file for subsequent saves
    GtkRevealer *save_notification_revealer;
    GtkTextBuffer *log_buffer; // Buffer for the live log window
} AppWidgets;

// Represents a simulated remote user's cursor and selection.
typedef struct {
    const char *user_name;
    GdkRGBA color;
    GtkTextMark *cursor_mark;
    GtkTextMark *selection_bound_mark;
    char selection_tag_name[64];
} RemoteCursor;

// Data needed for the remote cursor timer callback
typedef struct {
    RemoteCursor **cursors;
    GtkWidget *overlay_area;
} TimerCallbackData;

// Data needed for the custom drawing function for the overlay
typedef struct {
    GtkTextView *textview;
    RemoteCursor **cursors;
    int hovered_cursor_index;
} DrawCallbackData;


// --- Color Helper ---

// Converts HSV (Hue, Saturation, Value) to RGB color model.
// h is [0, 360], s is [0, 1], v is [0, 1]
static void hsv_to_rgb(float h, float s, float v, GdkRGBA *color) {
    float C = v * s;
    float X = C * (1 - fabs(fmod(h / 60.0, 2) - 1));
    float m = v - C;
    float r_prime, g_prime, b_prime;

    if (h >= 0 && h < 60) {
        r_prime = C; g_prime = X; b_prime = 0;
    } else if (h >= 60 && h < 120) {
        r_prime = X; g_prime = C; b_prime = 0;
    } else if (h >= 120 && h < 180) {
        r_prime = 0; g_prime = C; b_prime = X;
    } else if (h >= 180 && h < 240) {
        r_prime = 0; g_prime = X; b_prime = C;
    } else if (h >= 240 && h < 300) {
        r_prime = X; g_prime = 0; b_prime = C;
    } else {
        r_prime = C; g_prime = 0; b_prime = X;
    }

    color->red = r_prime + m;
    color->green = g_prime + m;
    color->blue = b_prime + m;
    color->alpha = 1.0;
}

// Generates a random, bright, saturated color.
static void generate_bright_color(GdkRGBA *color) {
    float hue = (rand() % 360);
    float saturation = 0.8 + (rand() % 20) / 100.0; // 0.8 to 1.0
    float value = 0.7 + (rand() % 20) / 100.0;      // 0.7 to 0.9
    hsv_to_rgb(hue, saturation, value, color);
}


// --- Status Bar Update Functions ---

// Function to update the word and character count in the status bar.
static void update_word_and_char_count(GtkTextBuffer *buffer, AppWidgets *widgets) {
    int word_count = 0;
    int char_count = 0;
    char *text;
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    
    char_count = g_utf8_strlen(text, -1);
    
    if (text && *text) {
        gchar **words = g_strsplit_set(text, " \n\t", -1);
        gchar **ptr = words;
        while(*ptr) {
            if(**ptr != '\0') {
                word_count++;
            }
            ptr++;
        }
        g_strfreev(words);
    }
    g_free(text);
    
    char wc_str[64];
    g_snprintf(wc_str, sizeof(wc_str), "%d Words, %d Chars", word_count, char_count);
    gtk_label_set_text(widgets->word_count_label, wc_str);
}

// Function to update the cursor position (line and column) in the status bar.
static void update_cursor_position(GtkTextBuffer *buffer, AppWidgets *widgets) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    
    int line = gtk_text_iter_get_line(&iter) + 1;
    int col = gtk_text_iter_get_line_offset(&iter) + 1;
    
    char pos_str[32];
    g_snprintf(pos_str, sizeof(pos_str), "Ln %d, Col %d", line, col);
    gtk_label_set_text(widgets->cursor_pos_label, pos_str);
}

// --- Signal Callbacks for Status Bar ---

static void on_mark_set_cb(GtkTextBuffer *buffer, const GtkTextIter *location, GtkTextMark *mark, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    if (mark == gtk_text_buffer_get_insert(buffer)) {
        update_cursor_position(buffer, widgets);
    }
}

static void on_buffer_changed_cb(GtkTextBuffer *buffer, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    update_word_and_char_count(buffer, widgets);
    update_cursor_position(buffer, widgets);
}

// --- Remote Cursor Drawing and Simulation ---

// This function is called to draw the custom remote cursors on the overlay.
static void draw_cursors_overlay(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    DrawCallbackData *data = (DrawCallbackData *)user_data;
    GtkTextView *textview = data->textview;
    RemoteCursor **cursors = data->cursors;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview);

    for (int i = 0; cursors[i] != NULL; ++i) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, cursors[i]->cursor_mark);

        GdkRectangle location;
        gtk_text_view_get_iter_location(textview, &iter, &location);
        
        int x = location.x;
        int y = location.y;
        int h = location.height;

        // --- Draw the cursor line ---
        gdk_cairo_set_source_rgba(cr, &cursors[i]->color);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x, y + h);
        cairo_stroke(cr);

        // --- Draw the name label ONLY if hovered ---
        if (data->hovered_cursor_index == i) {
            PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), cursors[i]->user_name);
            int text_width, text_height;
            pango_layout_get_pixel_size(layout, &text_width, &text_height);
            
            int label_padding = 4;
            int label_width = text_width + (label_padding * 2);
            int label_height = text_height + (label_padding * 2);
            int label_x = x;
            int label_y = y - label_height;

            if (label_y < 0) { // If label would go offscreen at the top, draw it below.
                label_y = y + h;
            }

            // Draw label background
            cairo_rectangle(cr, label_x, label_y, label_width, label_height);
            cairo_fill(cr);

            // Draw user name text (white for good contrast)
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_move_to(cr, label_x + label_padding, label_y + label_padding);
            pango_cairo_show_layout(cr, layout);
            
            g_object_unref(layout);
        }
    }
}

// This function moves the remote cursors and updates their selections randomly.
static gboolean move_remote_cursors(gpointer user_data) {
    TimerCallbackData *data = (TimerCallbackData*)user_data;
    RemoteCursor **cursors = data->cursors;
    GtkWidget *overlay_area = data->overlay_area;
    
    GtkTextBuffer *buffer = gtk_text_mark_get_buffer(cursors[0]->cursor_mark);
    GtkTextIter iter1, iter2;
    int count = gtk_text_buffer_get_char_count(buffer);

    if (count == 0) return G_SOURCE_CONTINUE;

    for (int i = 0; cursors[i] != NULL; ++i) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_remove_tag_by_name(buffer, cursors[i]->selection_tag_name, &start, &end);

        int pos1 = rand() % count;
        int pos2 = rand() % count;

        gtk_text_buffer_get_iter_at_offset(buffer, &iter1, pos1);
        gtk_text_buffer_get_iter_at_offset(buffer, &iter2, pos2);
        
        gtk_text_buffer_move_mark(buffer, cursors[i]->cursor_mark, &iter1);
        gtk_text_buffer_move_mark(buffer, cursors[i]->selection_bound_mark, &iter2);

        if (pos1 != pos2) {
            if (gtk_text_iter_get_offset(&iter1) > gtk_text_iter_get_offset(&iter2)) {
                 gtk_text_buffer_apply_tag_by_name(buffer, cursors[i]->selection_tag_name, &iter2, &iter1);
            } else {
                 gtk_text_buffer_apply_tag_by_name(buffer, cursors[i]->selection_tag_name, &iter1, &iter2);
            }
        }
    }

    gtk_widget_queue_draw(overlay_area); // Trigger a redraw to show new cursor positions
    return G_SOURCE_CONTINUE;
}

// Callback to redraw the overlay when the user scrolls or text view size changes
static void on_view_changed(GtkWidget *widget, gpointer user_data) {
    GtkWidget *overlay_area = GTK_WIDGET(user_data);
    gtk_widget_queue_draw(overlay_area);
}

// Callback for mouse motion over the drawing area to detect hovering.
static void on_overlay_motion(GtkEventControllerMotion *controller, double x, double y, gpointer user_data) {
    DrawCallbackData *data = (DrawCallbackData *)user_data;
    int previously_hovered = data->hovered_cursor_index;
    data->hovered_cursor_index = -1;

    for (int i = 0; data->cursors[i] != NULL; ++i) {
        GtkTextIter iter;
        GdkRectangle location;
        gtk_text_buffer_get_iter_at_mark(gtk_text_view_get_buffer(data->textview), &iter, data->cursors[i]->cursor_mark);
        gtk_text_view_get_iter_location(data->textview, &iter, &location);
        
        // Create a small, hittable area around the cursor line
        GdkRectangle hover_rect = { location.x - 2, location.y, 4, location.height };
        if (gdk_rectangle_contains_point(&hover_rect, x, y)) {
            data->hovered_cursor_index = i;
            break;
        }
    }
    
    if (data->hovered_cursor_index != previously_hovered) {
        gtk_widget_queue_draw(GTK_WIDGET(g_object_get_data(G_OBJECT(controller), "overlay_area_to_draw")));
    }
}

// Callback for when the mouse leaves the drawing area.
static void on_overlay_leave(GtkEventControllerMotion *controller, gpointer user_data) {
    DrawCallbackData *data = (DrawCallbackData *)user_data;
    if (data->hovered_cursor_index != -1) {
        data->hovered_cursor_index = -1;
        gtk_widget_queue_draw(GTK_WIDGET(g_object_get_data(G_OBJECT(controller), "overlay_area_to_draw")));
    }
}

// --- Save Functionality ---

static gboolean hide_save_notification(gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    gtk_revealer_set_reveal_child(widgets->save_notification_revealer, FALSE);
    return G_SOURCE_REMOVE;
}

static void show_save_notification(AppWidgets *widgets) {
    gtk_revealer_set_reveal_child(widgets->save_notification_revealer, TRUE);
    g_timeout_add_seconds(3, hide_save_notification, widgets);
}

// Writes the content of the text buffer to a given GFile.
static gboolean save_buffer_to_file(GFile *file, AppWidgets *widgets) {
    GError *error = NULL;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    const char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    char *path = g_file_get_path(file);
    gboolean success = g_file_set_contents(path, text, -1, &error);
    if (error) {
        g_warning("Error writing to file: %s", error->message);
        g_error_free(error);
    } else {
        // Only show notification and grab focus on successful save
        show_save_notification(widgets);
    }
    g_free(path);
    g_free((void *)text);
    
    // BUG FIX: Return focus to the text view after saving.
    gtk_widget_grab_focus(GTK_WIDGET(widgets->textview));

    return success;
}

// Callback for the "Save As" dialog.
static void save_dialog_cb(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    AppWidgets *widgets = (AppWidgets *)user_data;
    GFile *file;
    GError *error = NULL;

    file = gtk_file_dialog_save_finish(dialog, res, &error);
    if (error) {
        g_warning("Error saving file: %s", error->message);
        g_error_free(error);
        return;
    }
    if (file == NULL) return; // User cancelled

    if (save_buffer_to_file(file, widgets)) {
        // If save was successful, update the current_file
        if (widgets->current_file) {
            g_object_unref(widgets->current_file);
        }
        widgets->current_file = g_object_ref(file); // Store a reference to the new file
    }

    g_object_unref(file); // Unref the file from save_finish
}

// Callback for the main Save button.
static void on_save_button_clicked(GtkWidget *widget, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;

    if (widgets->current_file != NULL) {
        // File already exists, just save to it
        save_buffer_to_file(widgets->current_file, widgets);
    } else {
        // No file saved yet, show "Save As" dialog
        GtkFileDialog *dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, "Save File");
        gtk_file_dialog_set_initial_name(dialog, "Untitled document.txt");
        gtk_file_dialog_save(dialog, widgets->window, NULL, save_dialog_cb, user_data);
        g_object_unref(dialog);
    }
}

// --- Formatting Helper Functions ---

static void apply_tag_to_selection(const char *tag_name, AppWidgets *widgets) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, &end);
    }
}

static void toggle_tag_on_selection(const char *tag_name, AppWidgets *widgets) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GtkTextIter start, end;
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(tag_table, tag_name);
    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) return;
    if (gtk_text_iter_has_tag(&start, tag)) {
        gtk_text_buffer_remove_tag_by_name(buffer, tag_name, &start, &end);
    } else {
        gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, &end);
    }
}

typedef struct {
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
} RemoveTagData;

static void remove_bg_tag_if_needed(GtkTextTag *tag, gpointer data) {
    RemoveTagData *remove_data = (RemoveTagData*)data;
    const char *tag_name;
    g_object_get(tag, "name", &tag_name, NULL);
    if (g_str_has_prefix(tag_name, "background-rgba")) {
        gtk_text_buffer_remove_tag_by_name(remove_data->buffer, tag_name, &remove_data->start, &remove_data->end);
    }
    g_free((void*)tag_name);
}

// --- Formatting Button Callbacks ---

static void on_remove_highlight_clicked(GtkWidget *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    RemoveTagData remove_data;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &remove_data.start, &remove_data.end)) return;
    remove_data.buffer = buffer;
    gtk_text_tag_table_foreach(tag_table, remove_bg_tag_if_needed, &remove_data);
}

static void on_bold_button_clicked(GtkWidget *button, gpointer user_data) {
    toggle_tag_on_selection("bold", (AppWidgets*)user_data);
}

static void on_italic_button_clicked(GtkWidget *button, gpointer user_data) {
    toggle_tag_on_selection("italic", (AppWidgets*)user_data);
}

static void on_underline_button_clicked(GtkWidget *button, gpointer user_data) {
    toggle_tag_on_selection("underline", (AppWidgets*)user_data);
}

static void on_color_notify(GObject *object, GParamSpec *pspec, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    GtkColorDialogButton *button = GTK_COLOR_DIALOG_BUTTON(object);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    const GdkRGBA *rgba_ptr = gtk_color_dialog_button_get_rgba(button);
    char *hex_color = gdk_rgba_to_string(rgba_ptr);
    char tag_name[64];
    const char* property_to_set = g_object_get_data(object, "property");
    g_snprintf(tag_name, sizeof(tag_name), "%s_%s", property_to_set, hex_color);
    g_free(hex_color);
    if (gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), tag_name) == NULL) {
        gtk_text_buffer_create_tag(buffer, tag_name, property_to_set, rgba_ptr, NULL);
    }
    apply_tag_to_selection(tag_name, widgets);
}


// --- Change Logging ---
// This entire section can be removed for the final implementation.

// Helper to append a formatted string to the log buffer.
static void log_change(AppWidgets *widgets, const char *format, ...) {
    if (!widgets->log_buffer) return;
    char *message;
    va_list args;
    va_start(args, format);
    message = g_strdup_vprintf(format, args);
    va_end(args);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(widgets->log_buffer, &end);
    gtk_text_buffer_insert(widgets->log_buffer, &end, message, -1);
    gtk_text_buffer_insert(widgets->log_buffer, &end, "\n", -1);
    g_free(message);
}

// Callback for the "insert-text" signal.
static void on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, char *text, int len, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    int offset = gtk_text_iter_get_offset(location);
    // Escape special characters in the inserted text for logging
    char *escaped_text = g_strescape(text, NULL);
    log_change(widgets, "[INSERT] at offset %d: \"%s\"", offset, escaped_text);
    g_free(escaped_text);
}

// Callback for the "delete-range" signal.
static void on_buffer_delete_range(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    int start_offset = gtk_text_iter_get_offset(start);
    int end_offset = gtk_text_iter_get_offset(end);
    log_change(widgets, "[DELETE] from offset %d to %d", start_offset, end_offset);
}

// Callback for the "apply-tag" signal.
static void on_buffer_apply_tag(GtkTextBuffer *buffer, GtkTextTag *tag, GtkTextIter *start, GtkTextIter *end, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    const char *tag_name;
    g_object_get(tag, "name", &tag_name, NULL);
    int start_offset = gtk_text_iter_get_offset(start);
    int end_offset = gtk_text_iter_get_offset(end);
    // We don't want to log the simulated user selection tags.
    if (g_strcmp0(tag_name, "sel_alex") != 0 && g_strcmp0(tag_name, "sel_jordan") != 0) {
        log_change(widgets, "[APPLY_TAG] '%s' from %d to %d", tag_name, start_offset, end_offset);
    }
    g_free((void*)tag_name);
}

// Callback for the "remove-tag" signal.
static void on_buffer_remove_tag(GtkTextBuffer *buffer, GtkTextTag *tag, GtkTextIter *start, GtkTextIter *end, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    const char *tag_name;
    g_object_get(tag, "name", &tag_name, NULL);
    int start_offset = gtk_text_iter_get_offset(start);
    int end_offset = gtk_text_iter_get_offset(end);
    // We don't want to log the simulated user selection tags.
    if (g_strcmp0(tag_name, "sel_alex") != 0 && g_strcmp0(tag_name, "sel_jordan") != 0) {
        log_change(widgets, "[REMOVE_TAG] '%s' from %d to %d", tag_name, start_offset, end_offset);
    }
    g_free((void*)tag_name);
}


// --- Main Application Cleanup ---

static void cleanup_widgets_data(gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    if (widgets->current_file) {
        g_object_unref(widgets->current_file);
    }
    g_free(widgets);
}

// --- Main Application Setup ---

static void activate(GtkApplication *app, gpointer user_data) {
    // This is called when the app is run with no files. Create a blank window.
    create_editor_window(app, NULL);
}

// This is called when the app is launched with files to open from the command line.
static void on_app_open(GApplication *app, GFile **files, int n_files, const char *hint, gpointer user_data) {
    for (int i = 0; i < n_files; i++) {
        create_editor_window(GTK_APPLICATION(app), files[i]);
    }
}

static void create_editor_window(GtkApplication *app, GFile *file_to_open) {
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const char *css = "headerbar button { background-image: none; background-color: #E0E0E0; border: 1px solid #BDBDBD; box-shadow: 0 1px 1px rgba(0,0,0,0.1); } "
                      "headerbar button:hover { background-color: #D3D3D3; } "
                      "headerbar button:active { background-color: #C6C6C6; box-shadow: inset 0 1px 2px rgba(0,0,0,0.1); } "
                      ".notification-label { background-color: rgba(40, 40, 40, 0.8); color: white; padding: 10px; border-radius: 15px; }";
    gtk_css_provider_load_from_string(css_provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Collaborative Text Editor");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_vbox);

    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    GtkWidget *save_button = gtk_button_new_with_label("Save");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), save_button);

    GtkWidget *format_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), format_box);
    
    GtkWidget *bold_button = gtk_button_new_with_label("B"); gtk_box_append(GTK_BOX(format_box), bold_button);
    GtkWidget *italic_button = gtk_button_new_with_label("I"); gtk_box_append(GTK_BOX(format_box), italic_button);
    GtkWidget *underline_button = gtk_button_new_with_label("U"); gtk_box_append(GTK_BOX(format_box), underline_button);
    GtkWidget *remove_highlight_button = gtk_button_new_with_label("No Highlight"); gtk_box_append(GTK_BOX(format_box), remove_highlight_button);
    GtkWidget *text_color_button = gtk_color_dialog_button_new(gtk_color_dialog_new());
    g_object_set_data(G_OBJECT(text_color_button), "property", "foreground-rgba");
    gtk_box_append(GTK_BOX(format_box), text_color_button);
    GtkWidget *highlight_color_button = gtk_color_dialog_button_new(gtk_color_dialog_new());
    g_object_set_data(G_OBJECT(highlight_color_button), "property", "background-rgba");
    gtk_box_append(GTK_BOX(format_box), highlight_color_button);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), overlay);

    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scrolled_window);

    GtkWidget *textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), textview);
    
    // --- Setup Save Notification ---
    GtkWidget *save_notification_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(save_notification_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_widget_set_valign(save_notification_revealer, GTK_ALIGN_END);
    gtk_widget_set_halign(save_notification_revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(save_notification_revealer, 20);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), save_notification_revealer);
    GtkWidget *save_notification_label = gtk_label_new("Changes saved! :D");
    gtk_widget_add_css_class(save_notification_label, "notification-label");
    gtk_revealer_set_child(GTK_REVEALER(save_notification_revealer), save_notification_label);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(buffer, "underline", "underline", PANGO_UNDERLINE_SINGLE, NULL);
    
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(status_bar, 6); gtk_widget_set_margin_end(status_bar, 6); gtk_widget_set_margin_bottom(status_bar, 6);
    gtk_box_append(GTK_BOX(main_vbox), status_bar);

    GtkWidget *word_count_label = gtk_label_new("0 Words, 0 Chars");
    gtk_box_append(GTK_BOX(status_bar), word_count_label);
    
    GtkWidget *spacer = gtk_label_new(NULL); // This spacer will separate word count and user list
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(status_bar), spacer);

    AppWidgets *widgets = g_new(AppWidgets, 1);
    widgets->textview = GTK_TEXT_VIEW(textview);
    widgets->window = GTK_WINDOW(window);
    widgets->word_count_label = GTK_LABEL(word_count_label);
    widgets->current_file = NULL; // Initialize current file to NULL
    widgets->save_notification_revealer = GTK_REVEALER(save_notification_revealer);
    widgets->log_buffer = NULL; // Will be set by the logging window

    // --- Load File Content (if a file was provided) ---
    if (file_to_open != NULL) {
        char *contents = NULL;
        gsize length = 0;
        GError *error = NULL;

        if (g_file_load_contents(file_to_open, NULL, &contents, &length, NULL, &error)) {
            gtk_text_buffer_set_text(buffer, contents, length);
            g_free(contents);
            widgets->current_file = g_object_ref(file_to_open); // Store the file
        } else {
            g_warning("Failed to open file: %s", error->message);
            g_error_free(error);
        }
    }
    
    // --- Setup Remote Cursors ---
    GtkTextIter start_iter;
    gtk_text_buffer_get_start_iter(buffer, &start_iter);
    RemoteCursor *alex_cursor = g_new(RemoteCursor, 1);
    alex_cursor->user_name = "Alex";
    generate_bright_color(&alex_cursor->color);
    alex_cursor->cursor_mark = gtk_text_mark_new(NULL, TRUE);
    alex_cursor->selection_bound_mark = gtk_text_mark_new(NULL, FALSE);
    gtk_text_buffer_add_mark(buffer, alex_cursor->cursor_mark, &start_iter);
    gtk_text_buffer_add_mark(buffer, alex_cursor->selection_bound_mark, &start_iter);
    RemoteCursor *jordan_cursor = g_new(RemoteCursor, 1);
    jordan_cursor->user_name = "Jordan";
    generate_bright_color(&jordan_cursor->color);
    jordan_cursor->cursor_mark = gtk_text_mark_new(NULL, TRUE);
    jordan_cursor->selection_bound_mark = gtk_text_mark_new(NULL, FALSE);
    gtk_text_buffer_add_mark(buffer, jordan_cursor->cursor_mark, &start_iter);
    gtk_text_buffer_add_mark(buffer, jordan_cursor->selection_bound_mark, &start_iter);

    GdkRGBA alex_bg = alex_cursor->color; alex_bg.alpha = 0.3;
    g_snprintf(alex_cursor->selection_tag_name, sizeof(alex_cursor->selection_tag_name), "sel_alex");
    gtk_text_buffer_create_tag(buffer, alex_cursor->selection_tag_name, "background-rgba", &alex_bg, NULL);
    GdkRGBA jordan_bg = jordan_cursor->color; jordan_bg.alpha = 0.3;
    g_snprintf(jordan_cursor->selection_tag_name, sizeof(jordan_cursor->selection_tag_name), "sel_jordan");
    gtk_text_buffer_create_tag(buffer, jordan_cursor->selection_tag_name, "background-rgba", &jordan_bg, NULL);
    
    RemoteCursor **cursors = g_new(RemoteCursor*, 3);
    cursors[0] = alex_cursor;
    cursors[1] = jordan_cursor;
    cursors[2] = NULL;

    // --- Add User List to Status Bar ---
    GtkWidget *user_list_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(status_bar), user_list_box);
    for (int i=0; cursors[i] != NULL; ++i) {
        char hex_str[8];
        g_snprintf(hex_str, 8, "#%02x%02x%02x", (int)(cursors[i]->color.red*255), (int)(cursors[i]->color.green*255), (int)(cursors[i]->color.blue*255));
        char *markup = g_markup_printf_escaped("<span foreground='%s'>● %s</span>", hex_str, cursors[i]->user_name);
        GtkWidget *user_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(user_label), markup);
        gtk_box_append(GTK_BOX(user_list_box), user_label);
        g_free(markup);
    }

    GtkWidget *cursor_pos_label = gtk_label_new("Ln 1, Col 1");
    gtk_widget_set_margin_start(cursor_pos_label, 12);
    gtk_box_append(GTK_BOX(status_bar), cursor_pos_label);
    widgets->cursor_pos_label = GTK_LABEL(cursor_pos_label);


    // --- Setup Cursor Drawing Overlay ---
    GtkWidget *cursor_overlay_area = gtk_drawing_area_new();
    // This makes the drawing area transparent to input events, fixing text selection.
    gtk_widget_set_can_target(cursor_overlay_area, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), cursor_overlay_area);
    DrawCallbackData *draw_data = g_new(DrawCallbackData, 1);
    draw_data->textview = GTK_TEXT_VIEW(textview);
    draw_data->cursors = cursors;
    draw_data->hovered_cursor_index = -1; // Initially, no cursor is hovered
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(cursor_overlay_area), draw_cursors_overlay, draw_data, NULL);

    TimerCallbackData *timer_data = g_new(TimerCallbackData, 1);
    timer_data->cursors = cursors;
    timer_data->overlay_area = cursor_overlay_area;
    g_timeout_add_seconds(7, move_remote_cursors, timer_data); // Changed to 7 seconds
    
    // --- Setup Change Logging Window ---
    GtkWidget *log_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(log_window), "Live Log");
    gtk_window_set_default_size(GTK_WINDOW(log_window), 500, 700);
    gtk_window_set_transient_for(GTK_WINDOW(log_window), GTK_WINDOW(window));

    GtkWidget *log_scrolled_window = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(log_window), log_scrolled_window);
    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scrolled_window), log_view);
    widgets->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    log_change(widgets, "--- Log Started ---");


    // --- Connect Signals ---
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_view_changed), cursor_overlay_area);
    g_signal_connect(hadj, "value-changed", G_CALLBACK(on_view_changed), cursor_overlay_area);
    g_signal_connect(textview, "size-allocate", G_CALLBACK(on_view_changed), cursor_overlay_area);
    
    GtkEventController *motion = gtk_event_controller_motion_new();
    // We pass the drawing area to the callback so it knows what to redraw.
    g_object_set_data(G_OBJECT(motion), "overlay_area_to_draw", cursor_overlay_area);
    g_signal_connect(motion, "motion", G_CALLBACK(on_overlay_motion), draw_data);
    g_signal_connect(motion, "leave", G_CALLBACK(on_overlay_leave), draw_data);
    // Add the controller to the parent overlay, not the drawing area itself.
    gtk_widget_add_controller(overlay, motion);


    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_button_clicked), widgets);
    g_signal_connect(bold_button, "clicked", G_CALLBACK(on_bold_button_clicked), widgets);
    g_signal_connect(italic_button, "clicked", G_CALLBACK(on_italic_button_clicked), widgets);
    g_signal_connect(underline_button, "clicked", G_CALLBACK(on_underline_button_clicked), widgets);
    g_signal_connect(remove_highlight_button, "clicked", G_CALLBACK(on_remove_highlight_clicked), widgets);
    g_signal_connect(text_color_button, "notify::rgba", G_CALLBACK(on_color_notify), widgets);
    g_signal_connect(highlight_color_button, "notify::rgba", G_CALLBACK(on_color_notify), widgets);
    g_signal_connect(buffer, "changed", G_CALLBACK(on_buffer_changed_cb), widgets);
    g_signal_connect(buffer, "mark-set", G_CALLBACK(on_mark_set_cb), widgets);

    // --- Connect Logging Signals ---
    g_signal_connect(buffer, "insert-text", G_CALLBACK(on_buffer_insert_text), widgets);
    g_signal_connect(buffer, "delete-range", G_CALLBACK(on_buffer_delete_range), widgets);
    g_signal_connect(buffer, "apply-tag", G_CALLBACK(on_buffer_apply_tag), widgets);
    g_signal_connect(buffer, "remove-tag", G_CALLBACK(on_buffer_remove_tag), widgets);


    // Connect signals to free memory when the window is closed
    g_signal_connect(window, "destroy", G_CALLBACK(cleanup_widgets_data), widgets);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), draw_data);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), timer_data);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), alex_cursor);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), jordan_cursor);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), cursors);

    gtk_window_present(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(log_window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    srand(time(NULL));
    app = gtk_application_new("com.example.c.texteditor", G_APPLICATION_HANDLES_OPEN | G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_app_open), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

