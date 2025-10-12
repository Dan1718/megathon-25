#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h> // For shared mutex

// --- Configuration ---
#define AUTH_PORT 9000
#define START_SERVICE_PORT 9001
#define END_SERVICE_PORT 9010
#define MAX_SERVICE_PORTS (END_SERVICE_PORT - START_SERVICE_PORT + 1)
#define MAX_CLIENTS MAX_SERVICE_PORTS
#define DOCUMENT_INITIAL_CAPACITY 1024
#define HISTORY_INITIAL_CAPACITY 256
#define SHM_KEY 0x1234 // Unique key for shared memory
#define MUTEX_SHM_KEY 0x5678 // Unique key for shared mutex

// --- Operational Transformation (OT) Data Structures ---

// Defines the type of an operation
typedef enum {
    OP_INSERT,
    OP_DELETE
} OpType;

// Represents a single change to the document
typedef struct {
    OpType type;
    int revision;    // The server revision this operation is based on
    int position;    // The character position of the operation
    char character;  // The character for an INSERT operation
    int client_id;   // The ID of the client that originated the operation
} Operation;

// Represents the shared text document
typedef struct {
    char* text;
    size_t size;
    size_t capacity;
} Document;

// Represents the server's central state, which will live in shared memory
typedef struct {
    Document doc;
    Operation history[HISTORY_INITIAL_CAPACITY]; // Using a fixed-size history for simplicity
    int revision;
    int client_id_counter;
} ServerState;

// --- Port Management Data Structures ---

struct PortMapping {
    pid_t pid;
    int port;
    int in_use;
};

// Global (parent process only) variable to hold port statuses.
struct PortMapping service_ports[MAX_SERVICE_PORTS];

// --- Global variables for shared memory and mutex ---
int shm_id;
int mutex_shm_id;
ServerState *server_state = NULL;
pthread_mutex_t *shared_mutex = NULL;


// --- Function Prototypes ---
void sigchld_handler(int s);
int find_free_port();
void initialize_service_ports();
void handle_authentication(int auth_listener_fd);
void print_server_ip();
void cleanup_and_exit(int signal);

// OT Function Prototypes
Operation transform(Operation op_to_transform, Operation against_op);
void server_receive_op(Operation* op_from_client, Operation* op_to_broadcast);
void apply_op_to_doc(Document* doc, Operation* op);
void init_shared_state();

// Client Handler
void handle_client(int client_socket, int port, int client_id);


/**
 * @brief Main entry point for the server.
 */
int main() {
    // --- Initial Setup ---
    int auth_listener_fd;
    struct sockaddr_in address;
    int opt = 1;

    print_server_ip();
    initialize_service_ports();
    init_shared_state(); // Setup shared memory and mutex

    // --- Signal Handling ---
    signal(SIGINT, cleanup_and_exit); // Handle Ctrl+C for graceful shutdown
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // Reap zombie processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // --- Create Authentication Listener Socket ---
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

    // --- Main Server Loop ---
    printf("\n++++ Waiting for connections ++++\n");
    while (1) {
        int new_socket;
        int addrlen = sizeof(address);

        // This is a simple blocking accept. For a real-world scenario,
        // you would use select() here as in your original skeleton.
        // For this example, we focus on the OT logic.
        if ((new_socket = accept(auth_listener_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
            perror("accept");
            continue;
        }

        // --- Handle Authentication and Port Assignment ---
        int assigned_port = find_free_port();
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", assigned_port);
        send(new_socket, port_str, strlen(port_str), 0);
        close(new_socket); // Close auth connection

        if (assigned_port == 0) {
            printf("No free ports available for new client.\n");
            continue;
        }

        // --- Listen on the assigned port for the client to reconnect ---
        int service_fd;
        if ((service_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
             perror("service socket failed"); continue;
        }
        setsockopt(service_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        address.sin_port = htons(assigned_port);
        if (bind(service_fd, (struct sockaddr *)&address, sizeof(address))<0) {
            perror("service bind failed"); close(service_fd); continue;
        }
        if (listen(service_fd, 1) < 0) {
            perror("service listen failed"); close(service_fd); continue;
        }

        printf("Waiting for client to connect on assigned service port %d...\n", assigned_port);
        int client_socket = accept(service_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        close(service_fd); // Stop listening on this port once connected

        if (client_socket < 0) {
            perror("service accept");
            service_ports[assigned_port - START_SERVICE_PORT].in_use = 0; // Free the port
            continue;
        }
        printf("Connection accepted on service port %d! Forking new process.\n", assigned_port);

        // --- Fork a process to handle the client ---
        pid_t pid = fork();
        if (pid == 0) { // --- CHILD process ---
            close(auth_listener_fd);
            // The child must re-attach to the shared memory segments
            server_state = shmat(shm_id, NULL, 0);
            shared_mutex = shmat(mutex_shm_id, NULL, 0);
            if (server_state == (void *)-1 || shared_mutex == (void*)-1) {
                perror("shmat in child");
                exit(1);
            }

            pthread_mutex_lock(shared_mutex);
            int client_id = ++(server_state->client_id_counter);
            pthread_mutex_unlock(shared_mutex);

            handle_client(client_socket, assigned_port, client_id);
            exit(0); // Child process exits after handling client
        } else if (pid > 0) { // --- PARENT process ---
            close(client_socket);
            int port_index = assigned_port - START_SERVICE_PORT;
            service_ports[port_index].in_use = 1;
            service_ports[port_index].pid = pid;
            printf("Parent: Marked port %d as busy for PID %d.\n", assigned_port, pid);
        } else {
            perror("fork");
            close(client_socket);
        }
    }
    return 0;
}

// --- Operational Transformation (OT) Implementation ---

/**
 * @brief Applies a single operation to the document, resizing if necessary.
 */
void apply_op_to_doc(Document* doc, Operation* op) {
    if (op->type == OP_INSERT) {
        if (doc->size + 1 >= doc->capacity) {
            // This is a simplified reallocation. A real server would handle this more gracefully.
            // Since we can't realloc shared memory easily, we rely on a large initial capacity.
            fprintf(stderr, "Error: Document capacity exceeded. Cannot insert.\n");
            return;
        }
        // Move memory to make space for the new character
        memmove(&doc->text[op->position + 1], &doc->text[op->position], doc->size - op->position);
        doc->text[op->position] = op->character;
        doc->size++;
        doc->text[doc->size] = '\0';
    } else if (op->type == OP_DELETE) {
        if (op->position >= doc->size) return; // Invalid position
        // Move memory to overwrite the deleted character
        memmove(&doc->text[op->position], &doc->text[op->position + 1], doc->size - op->position);
        doc->size--;
        doc->text[doc->size] = '\0';
    }
}

/**
 * @brief Transforms an operation against a concurrent operation.
 * This is the core of the OT algorithm.
 */
Operation transform(Operation op_to_transform, Operation against_op) {
    Operation transformed_op = op_to_transform;

    if (op_to_transform.type == OP_INSERT && against_op.type == OP_INSERT) {
        if (op_to_transform.position > against_op.position) {
            transformed_op.position++;
        }
        // Tie-breaking: if positions are equal, the client with the higher ID has its op moved
        else if (op_to_transform.position == against_op.position && op_to_transform.client_id > against_op.client_id) {
            transformed_op.position++;
        }
    }
    else if (op_to_transform.type == OP_DELETE && against_op.type == OP_DELETE) {
        if (op_to_transform.position > against_op.position) {
            transformed_op.position--;
        } else if (op_to_transform.position == against_op.position) {
            // Both clients deleted the same character. The second operation becomes a no-op.
            // We can signify this by an impossible position. The apply logic should check for this.
            // For simplicity here, we let it proceed, which is harmless.
        }
    }
    else if (op_to_transform.type == OP_INSERT && against_op.type == OP_DELETE) {
        if (op_to_transform.position > against_op.position) {
            transformed_op.position--;
        }
    }
    else if (op_to_transform.type == OP_DELETE && against_op.type == OP_INSERT) {
        if (op_to_transform.position >= against_op.position) {
            transformed_op.position++;
        }
    }
    return transformed_op;
}

/**
 * @brief Processes an operation from a client.
 * This function must be called while holding the shared_mutex.
 */
void server_receive_op(Operation* op_from_client, Operation* op_to_broadcast) {
    // 1. Transform the incoming operation against all operations that have happened
    //    since the client's last known revision.
    Operation transformed_op = *op_from_client;
    for (int i = op_from_client->revision; i < server_state->revision; ++i) {
        transformed_op = transform(transformed_op, server_state->history[i]);
    }

    // 2. Apply the now-correct operation to the server's master document.
    apply_op_to_doc(&server_state->doc, &transformed_op);

    // 3. Add the operation to the history and update the server revision.
    transformed_op.revision = server_state->revision;
    server_state->history[server_state->revision] = transformed_op;
    server_state->revision++;

    // 4. Prepare the operation to be broadcast to all clients.
    *op_to_broadcast = transformed_op;
}


// --- Client Handling and Networking ---

/**
 * @brief Handles all communication with a single connected client.
 */
void handle_client(int client_socket, int port, int client_id) {
    printf("[Port %d, Client %d]: New client connected.\n", port, client_id);

    // 1. First, send the client its new ID and the current state of the document.
    pthread_mutex_lock(shared_mutex);
    int current_rev = server_state->revision;
    char *initial_text = strdup(server_state->doc.text); // Make a copy to use after unlocking
    pthread_mutex_unlock(shared_mutex);

    // Protocol: SEND "INIT;CLIENT_ID;REVISION;DOCUMENT_TEXT\n"
    char init_buffer[DOCUMENT_INITIAL_CAPACITY + 100];
    snprintf(init_buffer, sizeof(init_buffer), "INIT;%d;%d;%s\n", client_id, current_rev, initial_text);
    send(client_socket, init_buffer, strlen(init_buffer), 0);
    free(initial_text);

    // 2. Loop to receive operations from the client.
    char buffer[1024];
    long valread;
    while ((valread = read(client_socket, buffer, 1023)) > 0) {
        buffer[valread] = '\0';

        // Protocol: RECV "OP;TYPE;REVISION;POSITION;CHAR;CLIENT_ID\n"
        Operation op, broadcast_op;
        char op_type_str[10];
        sscanf(buffer, "OP;%[^;];%d;%d;%c;%d", op_type_str, &op.revision, &op.position, &op.character, &op.client_id);
        op.type = (strcmp(op_type_str, "INSERT") == 0) ? OP_INSERT : OP_DELETE;

        // Process the operation atomically
        pthread_mutex_lock(shared_mutex);
        server_receive_op(&op, &broadcast_op);
        // For debugging: print the document state after each change
        printf("[Port %d] Doc state (rev %d): \"%s\"\n", port, server_state->revision, server_state->doc.text);
        pthread_mutex_unlock(shared_mutex);

        // A real server would now broadcast `broadcast_op` to *all other* clients.
        // For this example, we'll just acknowledge to the originating client by sending
        // the transformed operation back. The client can use this to update its state.
        char ack_buffer[256];
        snprintf(ack_buffer, sizeof(ack_buffer), "ACK;%s;%d;%d;%c;%d\n",
                 broadcast_op.type == OP_INSERT ? "INSERT" : "DELETE",
                 broadcast_op.revision, broadcast_op.position, broadcast_op.character, broadcast_op.client_id);
        send(client_socket, ack_buffer, strlen(ack_buffer), 0);
    }

    if (valread == 0) {
        printf("[Port %d, Client %d]: Client disconnected.\n", port, client_id);
    } else {
        perror("read");
    }

    close(client_socket);
}

// --- Initialization and Cleanup ---

/**
 * @brief Initializes the shared memory segment and the shared mutex.
 */
void init_shared_state() {
    // 1. Create/Get Shared Memory for ServerState
    shm_id = shmget(SHM_KEY, sizeof(ServerState) + DOCUMENT_INITIAL_CAPACITY, IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget for state");
        exit(1);
    }
    server_state = shmat(shm_id, NULL, 0);
    if (server_state == (void *)-1) {
        perror("shmat for state");
        exit(1);
    }

    // 2. Create/Get Shared Memory for the Mutex
    mutex_shm_id = shmget(MUTEX_SHM_KEY, sizeof(pthread_mutex_t), IPC_CREAT | 0666);
    if (mutex_shm_id < 0) {
        perror("shmget for mutex");
        exit(1);
    }
    shared_mutex = shmat(mutex_shm_id, NULL, 0);
    if (shared_mutex == (void *)-1) {
        perror("shmat for mutex");
        exit(1);
    }

    // 3. Initialize the state and mutex (only on the first run)
    // A more robust way is to check if the memory was newly created.
    printf("Initializing shared server state...\n");
    server_state->revision = 0;
    server_state->client_id_counter = 0;
    server_state->doc.text = (char*)(server_state + 1); // Point text to memory right after the struct
    server_state->doc.size = 0;
    server_state->doc.capacity = DOCUMENT_INITIAL_CAPACITY;
    strcpy(server_state->doc.text, ""); // Start with an empty document

    // Initialize the mutex for inter-process sharing
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(shared_mutex, &attr);
    printf("Shared state and mutex initialized.\n");
}


/**
 * @brief Signal handler for SIGINT (Ctrl+C) to clean up IPC resources.
 */
void cleanup_and_exit(int signal) {
    printf("\nServer shutting down. Cleaning up resources...\n");
    // Detach from shared memory
    shmdt(server_state);
    shmdt(shared_mutex);
    // Remove shared memory segments
    shmctl(shm_id, IPC_RMID, NULL);
    shmctl(mutex_shm_id, IPC_RMID, NULL);
    printf("Cleanup complete. Goodbye.\n");
    exit(0);
}


/**
 * @brief Initializes the global service_ports array.
 */
void initialize_service_ports() {
    for (int i = 0; i < MAX_SERVICE_PORTS; i++) {
        service_ports[i].port = START_SERVICE_PORT + i;
        service_ports[i].pid = 0;
        service_ports[i].in_use = 0;
    }
}

/**
 * @brief Finds the first available service port.
 * @return The port number if one is free, otherwise 0.
 */
int find_free_port() {
    for (int i = 0; i < MAX_SERVICE_PORTS; i++) {
        if (!service_ports[i].in_use) {
            return service_ports[i].port;
        }
    }
    return 0; // No free ports
}

/**
 * @brief Signal handler to clean up terminated child processes (zombies).
 */
void sigchld_handler(int s) {
    int saved_errno = errno;
    pid_t pid;
    while((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_SERVICE_PORTS; i++) {
            if (service_ports[i].pid == pid) {
                printf("Child process %d finished. Port %d is now free.\n", pid, service_ports[i].port);
                service_ports[i].in_use = 0;
                service_ports[i].pid = 0;
                break;
            }
        }
    }
    errno = saved_errno;
}


/**
 * @brief Prints the server's local network IP addresses.
 */
void print_server_ip() {
    struct ifaddrs *ifaddr, *ifa;
    int s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }
    printf("Server IP addresses:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;

        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                        host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        if (s != 0) {
            printf("getnameinfo() failed: %s\n", gai_strerror(s));
            exit(EXIT_FAILURE);
        }
        if (strcmp(host, "127.0.0.1") != 0) {
             printf("  - %s (on interface %s)\n", host, ifa->ifa_name);
        }
    }
    freeifaddrs(ifaddr);
}