#include <gtk/gtk.h>
#include <gdk/gdk.h>

// A struct to hold pointers to widgets we need in different functions.
typedef struct {
    GtkTextView *textview;
    GtkWindow *window;
} AppWidgets;


// This is the callback function for the asynchronous save operation.
// It is called once the user has chosen a file in the dialog.
static void save_dialog_cb(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    AppWidgets *widgets = (AppWidgets *)user_data;
    GFile *file;
    GError *error = NULL;

    // Call the "_finish" function to get the result of the dialog.
    file = gtk_file_dialog_save_finish(dialog, res, &error);
    if (error) {
        g_warning("Error saving file: %s", error->message);
        g_error_free(error);
        return;
    }

    // If the user cancelled, file will be NULL.
    if (file == NULL) {
        g_object_unref(source_object);
        return;
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GtkTextIter start, end;

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    const char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    // Save the content to the selected file
    g_file_set_contents(g_file_get_path(file), text, -1, &error);
    if (error) {
        g_warning("Error writing to file: %s", error->message);
        g_error_free(error);
    }

    g_free((void *)text);
    g_object_unref(file);
}

// Callback function for when the "Save" button is clicked
static void on_save_button_clicked(GtkWidget *widget, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;

    // GtkFileDialog is the modern, asynchronous way to do file dialogs
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save File");
    gtk_file_dialog_set_initial_name(dialog, "Untitled document.txt");

    // "Save" the file. This shows the dialog and calls our callback function
    // with the result when the user is done.
    gtk_file_dialog_save(dialog, widgets->window, NULL, save_dialog_cb, user_data);
    
    g_object_unref(dialog);
}

// A helper function to apply a named GtkTextTag to the current selection.
static void apply_tag_to_selection(const char *tag_name, AppWidgets *widgets) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GtkTextIter start, end;

    // Check if there is any selected text.
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, &end);
    }
}

// Callback for the "Bold" button.
static void on_bold_button_clicked(GtkWidget *button, gpointer user_data) {
    apply_tag_to_selection("bold", (AppWidgets*)user_data);
}

// Callback for the "Italic" button.
static void on_italic_button_clicked(GtkWidget *button, gpointer user_data) {
    apply_tag_to_selection("italic", (AppWidgets*)user_data);
}

// Callback for the "Underline" button.
static void on_underline_button_clicked(GtkWidget *button, gpointer user_data) {
    apply_tag_to_selection("underline", (AppWidgets*)user_data);
}

// This callback is triggered when the GtkColorDialogButton's color property changes.
static void on_color_notify(GObject *object, GParamSpec *pspec, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    GtkColorDialogButton *button = GTK_COLOR_DIALOG_BUTTON(object);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->textview);
    GdkRGBA color;
    const GdkRGBA *rgba_ptr;
    char *hex_color;
    char tag_name[32];
    const char* property_to_set = g_object_get_data(object, "property");

    // Correct way to get the RGBA value: the function returns a pointer.
    rgba_ptr = gtk_color_dialog_button_get_rgba(button);
    color = *rgba_ptr; // Dereference the pointer to copy the struct data
    
    // Create a unique tag name from the color and property (foreground/background)
    hex_color = gdk_rgba_to_string(&color);
    g_snprintf(tag_name, sizeof(tag_name), "%s_%s", property_to_set, hex_color);
    g_free(hex_color);

    // Create the tag if it doesn't exist
    if (gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), tag_name) == NULL) {
        gtk_text_buffer_create_tag(buffer, tag_name, property_to_set, &color, NULL);
    }
    
    apply_tag_to_selection(tag_name, widgets);
}


// This function is called when the application is activated
static void activate(GtkApplication *app, gpointer user_data) {
    // Create the main window
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Simple Text Editor");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    // Create a HeaderBar
    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    // Create a "Save" button
    GtkWidget *save_button = gtk_button_new_with_label("Save");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), save_button);

    // --- Add formatting buttons ---
    GtkWidget *format_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), format_box);

    GtkWidget *bold_button = gtk_button_new_with_label("B");
    gtk_widget_set_tooltip_text(bold_button, "Bold");
    gtk_box_append(GTK_BOX(format_box), bold_button);

    GtkWidget *italic_button = gtk_button_new_with_label("I");
    gtk_widget_set_tooltip_text(italic_button, "Italic");
    gtk_box_append(GTK_BOX(format_box), italic_button);

    GtkWidget *underline_button = gtk_button_new_with_label("U");
    gtk_widget_set_tooltip_text(underline_button, "Underline");
    gtk_box_append(GTK_BOX(format_box), underline_button);

    // Modern color buttons
    GtkWidget *text_color_button = gtk_color_dialog_button_new(gtk_color_dialog_new());
    gtk_widget_set_tooltip_text(text_color_button, "Text Color");
    gtk_box_append(GTK_BOX(format_box), text_color_button);
    g_object_set_data(G_OBJECT(text_color_button), "property", "foreground-rgba");

    GtkWidget *highlight_color_button = gtk_color_dialog_button_new(gtk_color_dialog_new());
    gtk_widget_set_tooltip_text(highlight_color_button, "Highlight Color");
    gtk_box_append(GTK_BOX(format_box), highlight_color_button);
    g_object_set_data(G_OBJECT(highlight_color_button), "property", "background-rgba");

    // Create a ScrolledWindow to contain the TextView
    GtkWidget *scrolled_window = gtk_scrolled_window_new();

    // Create the TextView where the user can type
    GtkWidget *textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    
    // Get the buffer AFTER creating the textview
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(buffer, "underline", "underline", PANGO_UNDERLINE_SINGLE, NULL);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), textview);
    gtk_window_set_child(GTK_WINDOW(window), scrolled_window);

    AppWidgets *widgets = g_new(AppWidgets, 1);
    widgets->textview = GTK_TEXT_VIEW(textview);
    widgets->window = GTK_WINDOW(window);

    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_button_clicked), widgets);
    g_signal_connect(bold_button, "clicked", G_CALLBACK(on_bold_button_clicked), widgets);
    g_signal_connect(italic_button, "clicked", G_CALLBACK(on_italic_button_clicked), widgets);
    g_signal_connect(underline_button, "clicked", G_CALLBACK(on_underline_button_clicked), widgets);
    
    // Connect to the "notify::rgba" signal for the modern color buttons
    g_signal_connect(text_color_button, "notify::rgba", G_CALLBACK(on_color_notify), widgets);
    g_signal_connect(highlight_color_button, "notify::rgba", G_CALLBACK(on_color_notify), widgets);

    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), widgets);

    gtk_window_present(GTK_WINDOW(window));
}

// The main function where the program starts
int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.example.c.texteditor", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}


