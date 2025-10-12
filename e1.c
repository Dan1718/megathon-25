#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <gtk/gtk.h>

// =================================================================================
// 1. CORE OT DATA STRUCTURES (Unchanged)
// =================================================================================
typedef enum { OP_INSERT, OP_DELETE } OpType;
typedef struct {
    OpType type; int revision; int position; char character; int client_id;
} Operation;
typedef struct {
    char* text; size_t size; size_t capacity;
} Document;
typedef struct {
    Document* doc; Operation* history; int revision; size_t history_size; size_t history_capacity;
} Server;
typedef struct {
    int id; Document* doc; int revision; Operation* pending_op;
} Client;

// =================================================================================
// 2. GTK INTEGRATION DATA STRUCTURES (Unchanged)
// =================================================================================
typedef struct {
    Client* ot_client;
    GtkTextBuffer* buffer;
    bool is_applying_remote_op; 
} CollaborativeClient;

Server server;
CollaborativeClient client1_collab;
CollaborativeClient client2_collab;

// =================================================================================
// 3. FORWARD DECLARATIONS (Unchanged)
// =================================================================================
Operation transform(Operation op_to_transform, Operation against_op);
void client_receive_op(Client* client, Operation op_from_server);
Operation server_receive_op(Server* server, Operation op_from_client);
void apply_op_to_doc(Document* doc, Operation* op);
Document* create_document(const char* initial_text);
void free_document(Document* doc);
void apply_op_to_gtk_buffer(CollaborativeClient* collab_client, Operation* op);

// =================================================================================
// 4. OT CORE LOGIC (Unchanged)
// =================================================================================
Operation server_receive_op(Server* server, Operation op_from_client) {
    g_print("   [Server] Received op from C%d (based on rev %d). Server is at rev %d.\n", op_from_client.client_id, op_from_client.revision, server->revision);
    if (op_from_client.revision < server->revision) {
        for (int i = op_from_client.revision; i < server->revision; i++) {
            op_from_client = transform(op_from_client, server->history[i]);
        }
    }
    apply_op_to_doc(server->doc, &op_from_client);
    op_from_client.revision = server->revision;
    if (server->history_size >= server->history_capacity) {
        server->history_capacity *= 2;
        server->history = realloc(server->history, sizeof(Operation) * server->history_capacity);
    }
    server->history[server->history_size++] = op_from_client;
    server->revision++;
    g_print("   [Server] New State: \"%s\" (New Revision: %d). Broadcasting op.\n", server->doc->text, server->revision);
    return op_from_client;
}

void client_receive_op(Client* client, Operation op_from_server) {
    g_print("   [Client %d] Received op for rev %d (from C%d). Client is at rev %d.\n", client->id, op_from_server.revision, op_from_server.client_id, client->revision);
    Operation op_to_apply = op_from_server;
    if (client->pending_op) {
        if (op_from_server.client_id == client->id) {
            g_print("   [Client %d] -> It's an ACK. Clearing pending state.\n", client->id);
            free(client->pending_op);
            client->pending_op = NULL;
        } else {
            op_to_apply = transform(op_from_server, *(client->pending_op));
            *(client->pending_op) = transform(*(client->pending_op), op_from_server);
        }
    }
    apply_op_to_doc(client->doc, &op_to_apply);
    client->revision++;
    g_print("   [Client %d] Internal Doc State: \"%s\" (New Revision: %d)\n", client->id, client->doc->text, client->revision);
}

Operation transform(Operation op_to_transform, Operation against_op) {
    if (op_to_transform.type == OP_INSERT && against_op.type == OP_INSERT) {
        if (op_to_transform.position > against_op.position || (op_to_transform.position == against_op.position && op_to_transform.client_id > against_op.client_id)) {
            op_to_transform.position++;
        }
    } else if (op_to_transform.type == OP_DELETE && against_op.type == OP_DELETE) {
        if (op_to_transform.position > against_op.position) op_to_transform.position--;
        else if (op_to_transform.position == against_op.position) op_to_transform.position = -1;
    } else if (op_to_transform.type == OP_INSERT && against_op.type == OP_DELETE) {
        if (op_to_transform.position > against_op.position) op_to_transform.position--;
    } else if (op_to_transform.type == OP_DELETE && against_op.type == OP_INSERT) {
        if (op_to_transform.position >= against_op.position) op_to_transform.position++;
    }
    return op_to_transform;
}

// =================================================================================
// 5. GTK INTEGRATION METHODS (Unchanged)
// =================================================================================
void apply_op_to_gtk_buffer(CollaborativeClient* collab_client, Operation* op) {
    if (op->position == -1) return;
    collab_client->is_applying_remote_op = true;
    GtkTextIter iter;
    if (op->type == OP_INSERT) {
        gtk_text_buffer_get_iter_at_offset(collab_client->buffer, &iter, op->position);
        char char_str[2] = {op->character, '\0'};
        gtk_text_buffer_insert(collab_client->buffer, &iter, char_str, 1);
    } else if (op->type == OP_DELETE) {
        GtkTextIter end_iter;
        gtk_text_buffer_get_iter_at_offset(collab_client->buffer, &iter, op->position);
        gtk_text_buffer_get_iter_at_offset(collab_client->buffer, &end_iter, op->position + 1);
        gtk_text_buffer_delete(collab_client->buffer, &iter, &end_iter);
    }
    collab_client->is_applying_remote_op = false;
}

void send_op_and_distribute(Operation op) {
    g_print("\n--- Sending Op from C%d ---\n", op.client_id);
    Operation approved_op = server_receive_op(&server, op);
    client_receive_op(client1_collab.ot_client, approved_op);
    if (client1_collab.ot_client->id != approved_op.client_id) {
         apply_op_to_gtk_buffer(&client1_collab, &approved_op);
    }
    client_receive_op(client2_collab.ot_client, approved_op);
    if (client2_collab.ot_client->id != approved_op.client_id) {
        apply_op_to_gtk_buffer(&client2_collab, &approved_op);
    }
    g_print("--- Verification ---\n");
    g_print("Server:   \"%s\"\n", server.doc->text);
    g_print("Client 1: \"%s\"\n", client1_collab.ot_client->doc->text);
    g_print("Client 2: \"%s\"\n", client2_collab.ot_client->doc->text);
    //assert(strcmp(server.doc->text, client1_collab.ot_client->doc->text) == 0);
    //assert(strcmp(server.doc->text, client2_collab.ot_client->doc->text) == 0);
    g_print("SUCCESS: All internal documents converged.\n\n");
}

// =================================================================================
// 6. GTK SIGNAL HANDLERS (Unchanged)
// =================================================================================
static void on_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len, gpointer user_data) {
    CollaborativeClient* collab_client = (CollaborativeClient*)user_data;
    if (collab_client->is_applying_remote_op) return;
    if (len == 1) {
        gint pos = gtk_text_iter_get_offset(location);
        g_print("[GUI Event C%d] User inserted '%c' at position %d.\n", collab_client->ot_client->id, text[0], pos);
        Operation op = {OP_INSERT, collab_client->ot_client->revision, pos, text[0], collab_client->ot_client->id};
        apply_op_to_doc(collab_client->ot_client->doc, &op);
        collab_client->ot_client->pending_op = malloc(sizeof(Operation));
        memcpy(collab_client->ot_client->pending_op, &op, sizeof(Operation));
        send_op_and_distribute(op);
    }
}
static void on_delete_range(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, gpointer user_data) {
    CollaborativeClient* collab_client = (CollaborativeClient*)user_data;
    if (collab_client->is_applying_remote_op) return;
    gint start_pos = gtk_text_iter_get_offset(start);
    gint end_pos = gtk_text_iter_get_offset(end);
    if (end_pos - start_pos == 1) {
        g_print("[GUI Event C%d] User deleted character at position %d.\n", collab_client->ot_client->id, start_pos);
        Operation op = {OP_DELETE, collab_client->ot_client->revision, start_pos, ' ', collab_client->ot_client->id};
        apply_op_to_doc(collab_client->ot_client->doc, &op);
        collab_client->ot_client->pending_op = malloc(sizeof(Operation));
        memcpy(collab_client->ot_client->pending_op, &op, sizeof(Operation));
        send_op_and_distribute(op);
    }
}

// =================================================================================
// 7. MAIN APPLICATION SETUP (UPDATED FOR GTK 4)
// =================================================================================
static void activate(GtkApplication* app, gpointer user_data) {
    GtkWidget *window, *box, *scrolled_window1, *scrolled_window2, *view1, *view2, *label1, *label2;

    // Create Window
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "OT GTK 4 Integration");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    // Create main container box
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_window_set_child(GTK_WINDOW(window), box); // Use gtk_window_set_child for the top-level window

    // --- Create Client 1's TextView ---
    label1 = gtk_label_new("Client 1 View");
    view1 = gtk_text_view_new();
    scrolled_window1 = gtk_scrolled_window_new(); // No arguments in GTK 4
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window1), view1); // Set the child of the scrolled window
    
    // Add widgets to the box
    gtk_box_append(GTK_BOX(box), label1); // Use gtk_box_append
    gtk_box_append(GTK_BOX(box), scrolled_window1); // Use gtk_box_append

    // --- Create Client 2's TextView ---
    label2 = gtk_label_new("Client 2 View");
    view2 = gtk_text_view_new();
    scrolled_window2 = gtk_scrolled_window_new(); // No arguments in GTK 4
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window2), view2); // Set the child
    
    // Add widgets to the box
    gtk_box_append(GTK_BOX(box), label2); // Use gtk_box_append
    gtk_box_append(GTK_BOX(box), scrolled_window2); // Use gtk_box_append
    
    // --- OT and GTK Initialization ---
    const char* initial_text = "Hello";
    server.doc = create_document(initial_text);
    server.revision = 0;
    server.history_size = 0;
    server.history_capacity = 10;
    server.history = malloc(sizeof(Operation) * server.history_capacity);

    Client* client1_ot = malloc(sizeof(Client));
    client1_ot->id = 1;
    client1_ot->doc = create_document(initial_text);
    client1_ot->revision = 0;
    client1_ot->pending_op = NULL;

    Client* client2_ot = malloc(sizeof(Client));
    client2_ot->id = 2;
    client2_ot->doc = create_document(initial_text);
    client2_ot->revision = 0;
    client2_ot->pending_op = NULL;
    
    client1_collab.ot_client = client1_ot;
    client1_collab.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view1));
    client1_collab.is_applying_remote_op = false;
    gtk_text_buffer_set_text(client1_collab.buffer, initial_text, -1);

    client2_collab.ot_client = client2_ot;
    client2_collab.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view2));
    client2_collab.is_applying_remote_op = false;
    gtk_text_buffer_set_text(client2_collab.buffer, initial_text, -1);

    g_signal_connect(client1_collab.buffer, "insert-text", G_CALLBACK(on_insert_text), &client1_collab);
    g_signal_connect(client1_collab.buffer, "delete-range", G_CALLBACK(on_delete_range), &client1_collab);
    
    g_signal_connect(client2_collab.buffer, "insert-text", G_CALLBACK(on_insert_text), &client2_collab);
    g_signal_connect(client2_collab.buffer, "delete-range", G_CALLBACK(on_delete_range), &client2_collab);

    gtk_widget_show(window); // Use gtk_widget_show, not gtk_widget_show_all
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    // Use the recommended G_APPLICATION_DEFAULT_FLAGS for GTK 4
    app = gtk_application_new("org.example.otgtk4", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    // Cleanup
    free(server.history);
    free_document(server.doc);
    if(client1_collab.ot_client) {
        free_document(client1_collab.ot_client->doc);
        if(client1_collab.ot_client->pending_op) free(client1_collab.ot_client->pending_op);
        free(client1_collab.ot_client);
    }
    if(client2_collab.ot_client) {
        free_document(client2_collab.ot_client->doc);
        if(client2_collab.ot_client->pending_op) free(client2_collab.ot_client->pending_op);
        free(client2_collab.ot_client);
    }

    return status;
}

// =================================================================================
// 8. UTILITY FUNCTIONS (Unchanged)
// =================================================================================
Document* create_document(const char* initial_text) {
    Document* doc = malloc(sizeof(Document));
    size_t len = strlen(initial_text);
    doc->capacity = len + 16; doc->text = malloc(doc->capacity);
    strcpy(doc->text, initial_text); doc->size = len;
    return doc;
}
void free_document(Document* doc) {
    if (doc) { free(doc->text); free(doc); }
}
void apply_op_to_doc(Document* doc, Operation* op) {
    if (op->position == -1) return;
    if (op->type == OP_INSERT) {
        if (doc->size + 1 >= doc->capacity) {
            doc->capacity = (doc->capacity == 0) ? 16 : doc->capacity * 2;
            doc->text = realloc(doc->text, doc->capacity);
        }
        memmove(&doc->text[op->position + 1], &doc->text[op->position], doc->size - op->position + 1);
        doc->text[op->position] = op->character; doc->size++;
    } else if (op->type == OP_DELETE) {
        if (op->position < doc->size) {
            memmove(&doc->text[op->position], &doc->text[op->position + 1], doc->size - op->position);
            doc->size--;
        }
    }
}