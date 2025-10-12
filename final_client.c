#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

// IMPORTANT: Change this IP to the actual IP address of your server.
#define SERVER_IP "127.0.0.1" 
#define AUTH_PORT 9000

// =================================================================================
// 1. CORE OT DATA STRUCTURES
// =================================================================================
typedef enum { OP_INSERT, OP_DELETE } OpType;
typedef struct {
    OpType type; int revision; int position; char character; int client_id;
} Operation;
typedef struct {
    char* text; size_t size; size_t capacity;
} Document;
typedef struct {
    int id; Document* doc; int revision; Operation* pending_op;
} Client;

// =================================================================================
// 2. FORWARD DECLARATIONS
// =================================================================================
Operation transform(Operation op_to_transform, Operation against_op);
void apply_op_to_doc(Document* doc, Operation* op);
Document* create_document(const char* initial_text);
void apply_op_to_gtk_buffer(GtkTextBuffer* buffer, Operation* op, gboolean *is_applying_remote_op_flag);

// =================================================================================
// 3. APPLICATION-WIDE WIDGETS AND STATE
// =================================================================================
typedef struct {
    GtkTextView *textview;
    GtkWindow *window;
    GtkTextBuffer *buffer;
    bool is_applying_remote_op; // Flag to prevent signal recursion

    // --- OT and Network State ---
    Client *ot_client;
    int send_sock;
    int recv_sock;
} AppState;

// =================================================================================
// 4. OT CLIENT-SIDE LOGIC
// =================================================================================
void client_receive_op(Client* client, Operation op_from_server) {
    g_print("   [Client %d] Received op for rev %d (from C%d). Client is at rev %d.\n", client->id, op_from_server.revision, op_from_server.client_id, client->revision);
    if (client->pending_op && op_from_server.client_id == client->id) {
        g_print("   [Client %d] -> It's an ACK. Clearing pending state.\n", client->id);
        free(client->pending_op);
        client->pending_op = NULL;
    } else {
        Operation op_to_apply = op_from_server;
        if (client->pending_op) {
            g_print("   [Client %d] -> It's a concurrent op. Transforming...\n", client->id);
            op_to_apply = transform(op_from_server, *(client->pending_op));
            *(client->pending_op) = transform(*(client->pending_op), op_from_server);
        }
        apply_op_to_doc(client->doc, &op_to_apply);
    }
    client->revision++;
    g_print("   [Client %d] Internal Doc State: \"%s\" (New Revision: %d)\n", client->id, client->doc->text, client->revision);
}

// =================================================================================
// 5. NETWORK AND GUI THREADING
// =================================================================================

// Data passed to the GUI update function scheduled by the network thread
typedef struct {
    AppState *app_state;
    Operation op;
} OpUpdateData;

// This function runs IN THE MAIN GTK THREAD to safely update the UI
static gboolean process_server_op_main_thread(gpointer user_data) {
    OpUpdateData *update_data = (OpUpdateData*)user_data;
    AppState *state = update_data->app_state;
    Operation op_from_server = update_data->op;

    // We only update the GUI buffer for ops that did NOT originate from us.
    // Our own ops are already reflected in the buffer due to optimistic local updates.
    if (state->ot_client->id != op_from_server.client_id) {
        // We must transform the incoming op before applying it to the buffer if we have a pending op
        Operation op_to_apply_to_buffer = op_from_server;
        if (state->ot_client->pending_op) {
             op_to_apply_to_buffer = transform(op_from_server, *(state->ot_client->pending_op));
        }
        apply_op_to_gtk_buffer(state->buffer, &op_to_apply_to_buffer, &state->is_applying_remote_op);
    }
    
    // We ALWAYS update our internal OT client state for every op from the server
    client_receive_op(state->ot_client, op_from_server);

    g_free(update_data);
    return G_SOURCE_REMOVE;
}


// This function runs IN A BACKGROUND THREAD to listen for server messages
void* receive_thread_func(void* arg) {
    AppState *state = (AppState*)arg;
    Operation op_from_server;

    while (read(state->recv_sock, &op_from_server, sizeof(Operation)) > 0) {
        // We have received an operation. We cannot touch GTK widgets from this thread.
        // We must schedule a function to run on the main thread.
        OpUpdateData *update_data = g_new(OpUpdateData, 1);
        update_data->app_state = state;
        update_data->op = op_from_server;
        g_idle_add(process_server_op_main_thread, update_data);
    }

    printf("Server disconnected. Closing receive thread.\n");
    // Optionally, you could use g_idle_add here to inform the user the server disconnected.
    return NULL;
}


// =================================================================================
// 6. GTK SIGNAL HANDLERS
// =================================================================================
static void on_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len, gpointer user_data) {
    AppState *state = (AppState*)user_data;
    if (state->is_applying_remote_op) return;
    
    if (len == 1 && state->ot_client->pending_op == NULL) {
        gint pos = gtk_text_iter_get_offset(location);
        g_print("[GUI Event C%d] User inserted '%c' at position %d.\n", state->ot_client->id, text[0], pos);
        
        Operation op = {OP_INSERT, state->ot_client->revision, pos, text[0], state->ot_client->id};
        
        // Optimistic local update
        apply_op_to_doc(state->ot_client->doc, &op);
        state->ot_client->pending_op = malloc(sizeof(Operation));
        memcpy(state->ot_client->pending_op, &op, sizeof(Operation));
        
        // Send to server
        send(state->send_sock, &op, sizeof(Operation), 0);
    }
}

static void on_delete_range(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, gpointer user_data) {
    AppState *state = (AppState*)user_data;
    if (state->is_applying_remote_op) return;

    gint start_pos = gtk_text_iter_get_offset(start);
    gint end_pos = gtk_text_iter_get_offset(end);

    if (end_pos - start_pos == 1 && state->ot_client->pending_op == NULL) {
        g_print("[GUI Event C%d] User deleted character at position %d.\n", state->ot_client->id, start_pos);

        Operation op = {OP_DELETE, state->ot_client->revision, start_pos, ' ', state->ot_client->id};
        
        // Optimistic local update
        apply_op_to_doc(state->ot_client->doc, &op);
        state->ot_client->pending_op = malloc(sizeof(Operation));
        memcpy(state->ot_client->pending_op, &op, sizeof(Operation));
        
        // Send to server
        send(state->send_sock, &op, sizeof(Operation), 0);
    }
}

// =================================================================================
// 7. MAIN APPLICATION SETUP
// =================================================================================

int connect_to_server_socket(const char* ip, int port);

static void create_editor_window(GtkApplication *app, AppState *state) {
    GtkWidget *window, *scrolled_window, *main_vbox, *textview;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Collaborative Editor");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    state->window = GTK_WINDOW(window);

    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_vbox);

    scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), scrolled_window);

    textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), textview);
    state->textview = GTK_TEXT_VIEW(textview);
    state->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    
    // --- Set initial text and connect signals ---
    gtk_text_buffer_set_text(state->buffer, state->ot_client->doc->text, -1);
    g_signal_connect(state->buffer, "insert-text", G_CALLBACK(on_insert_text), state);
    g_signal_connect(state->buffer, "delete-range", G_CALLBACK(on_delete_range), state);

    gtk_widget_show(window);
}


static void activate(GtkApplication *app, gpointer user_data) {
    // --- Phase 1: Authentication and Connection ---
    int auth_sock = connect_to_server_socket(SERVER_IP, AUTH_PORT);
    if (auth_sock < 0) {
        g_printerr("Could not connect to authentication server. Is it running?\n");
        exit(1);
    }
    
    char server_response[1024] = {0};
    read(auth_sock, server_response, 1023);
    close(auth_sock);

    if (strcmp(server_response, "0") == 0) {
        g_printerr("Authentication failed or server is full.\n");
        exit(1);
    }

    // --- Parse response: "port:client_id:initial_text" ---
    char *token;
    int send_port = atoi(strtok(server_response, ":"));
    int client_id = atoi(strtok(NULL, ":"));
    char *initial_text = strtok(NULL, "");
    
    int recv_port = send_port + 1;
    printf("Auth successful! Client ID: %d. Connecting to SEND %d and RECV %d\n", client_id, send_port, recv_port);

    int send_sock = connect_to_server_socket(SERVER_IP, send_port);
    int recv_sock = connect_to_server_socket(SERVER_IP, recv_port);

    if (send_sock < 0 || recv_sock < 0) {
        g_printerr("Failed to connect to service ports.\n");
        exit(1);
    }

    // --- Phase 2: Initialize Application State ---
    AppState *state = g_new(AppState, 1);
    state->is_applying_remote_op = false;
    state->send_sock = send_sock;
    state->recv_sock = recv_sock;
    state->ot_client = g_new(Client, 1);
    state->ot_client->id = client_id;
    state->ot_client->revision = 0;
    state->ot_client->pending_op = NULL;
    state->ot_client->doc = create_document(initial_text);
    
    // --- Phase 3: Create GUI and Start Network Thread ---
    create_editor_window(app, state);
    
    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, receive_thread_func, state) != 0) {
        perror("pthread_create for receiver");
        exit(1);
    }
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    app = gtk_application_new("com.example.ot.client", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}


// =================================================================================
// 8. UTILITY AND OT LOGIC IMPLEMENTATIONS
// =================================================================================

int connect_to_server_socket(const char* ip, int port) {
    int sock;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "\nInvalid address/ Address not supported: %s\n", ip);
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Connection failed to port %d", port);
        perror("");
        close(sock);
        return -1;
    }
    printf("Successfully connected to port %d.\n", port);
    return sock;
}

void apply_op_to_gtk_buffer(GtkTextBuffer* buffer, Operation* op, gboolean *is_applying_remote_op_flag) {
    if (op->position == -1) return;
    *is_applying_remote_op_flag = true; // Disable signal handlers
    GtkTextIter iter;
    if (op->type == OP_INSERT) {
        gtk_text_buffer_get_iter_at_offset(buffer, &iter, op->position);
        char char_str[2] = {op->character, '\0'};
        gtk_text_buffer_insert(buffer, &iter, char_str, 1);
    } else if (op->type == OP_DELETE) {
        GtkTextIter end_iter;
        gtk_text_buffer_get_iter_at_offset(buffer, &iter, op->position);
        gtk_text_buffer_get_iter_at_offset(buffer, &end_iter, op->position + 1);
        gtk_text_buffer_delete(buffer, &iter, &end_iter);
    }
    *is_applying_remote_op_flag = false; // Re-enable signal handlers
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

Document* create_document(const char* initial_text) {
    Document* doc = malloc(sizeof(Document));
    size_t len = strlen(initial_text);
    doc->capacity = len + 16; doc->text = malloc(doc->capacity);
    strcpy(doc->text, initial_text); doc->size = len;
    return doc;
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