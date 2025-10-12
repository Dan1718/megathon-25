#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>

// =================================================================================
// 1. CORE OT DATA STRUCTURES (Brought from your code)
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

// --- Forward Declarations for OT logic ---
Operation transform(Operation op_to_transform, Operation against_op);
void apply_op_to_doc(Document* doc, Operation* op);
Document* create_document(const char* initial_text);

// =================================================================================
// 2. SERVER CONFIGURATION AND GLOBAL STATE
// =================================================================================
#define AUTH_PORT 9000
#define SERVICE_PORT_START 9001
#define MAX_CLIENTS 10

// --- Global OT Server State ---
Server ot_server;
pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Global Client/Connection State ---
typedef struct {
    int send_sock;
    int recv_sock;
    int client_id;
    int is_active;
} ClientConnection;

ClientConnection client_connections[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int next_client_id = 1;


// =================================================================================
// 3. SERVER NETWORKING LOGIC
// =================================================================================

void print_server_ip();
void* client_handler_thread(void* arg);

int main() {
    // --- Initialize OT Server ---
    ot_server.doc = create_document("Hello, world!");
    ot_server.revision = 0;
    ot_server.history_capacity = 100;
    ot_server.history_size = 0;
    ot_server.history = malloc(sizeof(Operation) * ot_server.history_capacity);
    printf("OT Server initialized. Document: \"%s\"\n", ot_server.doc->text);

    // --- Initialize Client Registry ---
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        client_connections[i].is_active = 0;
    }
    
    int auth_listener_fd;
    struct sockaddr_in address;
    int opt = 1;

    print_server_ip();

    // --- Create and bind the authentication socket ---
    if ((auth_listener_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("auth socket failed");
        exit(EXIT_FAILURE);
    }
    setsockopt(auth_listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(AUTH_PORT);
    if (bind(auth_listener_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("auth bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(auth_listener_fd, 5) < 0) {
        perror("auth listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Authentication server listening on port %d\n", AUTH_PORT);

    // --- Main loop to accept new clients ---
    while(1) {
        int auth_socket;
        int addrlen = sizeof(address);
        printf("\n++++ Waiting for new client authentication ++++\n");
        if ((auth_socket = accept(auth_listener_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("auth accept");
            continue;
        }

        printf("Received an authentication request.\n");
        
        // Find a free slot for the new client
        pthread_mutex_lock(&clients_mutex);
        int client_idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!client_connections[i].is_active) {
                client_idx = i;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        if (client_idx == -1) {
            printf("Max clients reached. Rejecting connection.\n");
            send(auth_socket, "0", 1, 0); // Tell client we are full
            close(auth_socket);
            continue;
        }

        // --- Assign service ports and client ID ---
        int service_port_send = SERVICE_PORT_START + (client_idx * 2);
        int client_id = next_client_id++;
        
        char response[128];
        snprintf(response, sizeof(response), "%d:%d:%s", service_port_send, client_id, ot_server.doc->text);
        
        // --- Send port, client_id, and initial document state ---
        send(auth_socket, response, strlen(response), 0);
        close(auth_socket);
        printf("Auth successful. Assigned Client ID %d to port pair starting at %d.\n", client_id, service_port_send);
        printf("Waiting for client to connect to service ports...\n");

        // --- The client will now connect to the service ports. We create a thread to handle it. ---
        pthread_t tid;
        int *p_client_idx = malloc(sizeof(int));
        *p_client_idx = client_idx;
        if (pthread_create(&tid, NULL, client_handler_thread, p_client_idx) != 0) {
            perror("pthread_create");
            free(p_client_idx);
        }
    }

    return 0;
}

// Thread function to manage a single client's lifecycle
void* client_handler_thread(void* arg) {
    int client_idx = *(int*)arg;
    free(arg);

    int service_port_send = SERVICE_PORT_START + (client_idx * 2);
    int service_port_recv = service_port_send + 1;
    
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;

    // --- Setup SEND socket listener ---
    int send_listener = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(send_listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_port = htons(service_port_send);
    bind(send_listener, (struct sockaddr *)&address, sizeof(address));
    listen(send_listener, 1);
    int send_sock = accept(send_listener, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    close(send_listener);

    // --- Setup RECV socket listener ---
    int recv_listener = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(recv_listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_port = htons(service_port_recv);
    bind(recv_listener, (struct sockaddr *)&address, sizeof(address));
    listen(recv_listener, 1);
    int recv_sock = accept(recv_listener, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    close(recv_listener);
    
    if (send_sock < 0 || recv_sock < 0) {
        perror("service port accept");
        return NULL;
    }
    
    printf("Client %d connected successfully to service ports %d & %d.\n", next_client_id-1, service_port_send, service_port_recv);

    // --- Add client to the global registry ---
    pthread_mutex_lock(&clients_mutex);
    client_connections[client_idx].send_sock = send_sock;
    client_connections[client_idx].recv_sock = recv_sock;
    client_connections[client_idx].client_id = next_client_id - 1;
    client_connections[client_idx].is_active = 1;
    pthread_mutex_unlock(&clients_mutex);

    // --- Main loop to process operations from this client ---
    Operation op_from_client;
    while (read(send_sock, &op_from_client, sizeof(Operation)) > 0) {
        
        // --- Process the operation with OT logic ---
        pthread_mutex_lock(&server_mutex);
        
        printf("   [Server] Received op from C%d (rev %d). Server is at rev %d.\n", op_from_client.client_id, op_from_client.revision, ot_server.revision);
        if (op_from_client.revision < ot_server.revision) {
            for (int i = op_from_client.revision; i < ot_server.revision; i++) {
                op_from_client = transform(op_from_client, ot_server.history[i]);
            }
        }
        apply_op_to_doc(ot_server.doc, &op_from_client);
        op_from_client.revision = ot_server.revision;
        ot_server.history[ot_server.history_size++] = op_from_client;
        ot_server.revision++;
        printf("   [Server] New State: \"%s\" (Rev: %d). Broadcasting op.\n", ot_server.doc->text, ot_server.revision);

        pthread_mutex_unlock(&server_mutex);

        // --- Broadcast the approved operation to ALL clients ---
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_connections[i].is_active) {
                send(client_connections[i].recv_sock, &op_from_client, sizeof(Operation), 0);
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    // --- Client disconnected, clean up ---
    printf("Client %d disconnected. Cleaning up.\n", client_connections[client_idx].client_id);
    close(send_sock);
    close(recv_sock);
    pthread_mutex_lock(&clients_mutex);
    client_connections[client_idx].is_active = 0;
    pthread_mutex_unlock(&clients_mutex);

    return NULL;
}


// =================================================================================
// 4. OT CORE LOGIC (Copied from your code)
// =================================================================================
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
    doc->capacity = len + 128; doc->text = malloc(doc->capacity);
    strcpy(doc->text, initial_text); doc->size = len;
    return doc;
}
void apply_op_to_doc(Document* doc, Operation* op) {
    if (op->position == -1) return;
    if (op->type == OP_INSERT) {
        if (doc->size + 1 >= doc->capacity) {
            doc->capacity *= 2;
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
void print_server_ip() {
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];
    if (getifaddrs(&ifaddr) == -1) { perror("getifaddrs"); exit(EXIT_FAILURE); }
    printf("Server IP addresses:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
        getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        if (strcmp(host, "127.0.0.1") != 0) {
             printf("  - %s (on interface %s)\n", host, ifa->ifa_name);
        }
    }
    freeifaddrs(ifaddr);
}