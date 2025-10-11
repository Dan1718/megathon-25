#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <errno.h>

// --- Configuration ---
#define AUTH_PORT 9000
#define START_SERVICE_PORT 9001
#define END_SERVICE_PORT 9002
#define MAX_SERVICE_PORTS (END_SERVICE_PORT - START_SERVICE_PORT + 1)

// --- Data Structures ---
// To track the status of each service port
struct PortMapping {
    pid_t pid;       // The PID of the child process handling the client
    int port;        // The port number
    int in_use;      // 0 if free, 1 if busy
};

// Global variable to hold port statuses.
// A global is used so the signal handler can access it.
struct PortMapping service_ports[MAX_SERVICE_PORTS];


// --- Function Prototypes ---
void handle_client(int client_socket, int port);
void print_server_ip();
void sigchld_handler(int s);
int find_free_port();
void initialize_service_ports();
void handle_authentication(int auth_listener_fd);


int main() {
    int auth_listener_fd;
    int service_listener_fds[MAX_SERVICE_PORTS];
    int service_listener_count = 0;
    struct sockaddr_in address;
    int opt = 1;

    // Print the server's IP address so clients know where to connect
    print_server_ip();
    initialize_service_ports();

    // Set up a signal handler to clean up child processes (zombies)
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // 1. Create the single authentication port listener
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


    // 2. Create a listening socket for each SERVICE port in the range
    for (int port = START_SERVICE_PORT; port <= END_SERVICE_PORT; port++) {
        int server_fd;
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("service socket failed");
            continue;
        }
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        address.sin_port = htons(port);
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("service bind failed");
            close(server_fd);
            continue;
        }
        if (listen(server_fd, 10) < 0) {
            perror("service listen failed");
            close(server_fd);
            continue;
        }
        printf("Service listening on port %d\n", port);
        service_listener_fds[service_listener_count++] = server_fd;
    }

    if (service_listener_count == 0) {
        fprintf(stderr, "Could not bind to any service ports. Exiting.\n");
        exit(EXIT_FAILURE);
    }


    // 3. Main loop to accept connections
    printf("\n++++ Waiting for connections ++++\n");
    while(1) {
        fd_set read_fds;
        int max_fd = auth_listener_fd;
        FD_ZERO(&read_fds);

        // Add auth socket and all service sockets to the set
        FD_SET(auth_listener_fd, &read_fds);
        for (int i = 0; i < service_listener_count; i++) {
            FD_SET(service_listener_fds[i], &read_fds);
            if (service_listener_fds[i] > max_fd) {
                max_fd = service_listener_fds[i];
            }
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        // --- Check for an authentication request ---
        if (FD_ISSET(auth_listener_fd, &read_fds)) {
            handle_authentication(auth_listener_fd);
        }

        // --- Check for a connection on a service port ---
        for (int i = 0; i < service_listener_count; i++) {
            if (FD_ISSET(service_listener_fds[i], &read_fds)) {
                int new_socket;
                int addrlen = sizeof(address);
                if ((new_socket = accept(service_listener_fds[i], (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                    perror("accept");
                    continue;
                }
                
                int current_port = START_SERVICE_PORT + i;
                printf("\nConnection accepted on service port %d! Forking new process.\n", current_port);

                pid_t pid = fork();
                if (pid == 0) {
                    // --- This is the CHILD process ---
                    close(auth_listener_fd); // Child doesn't need any listeners
                    for(int j = 0; j < service_listener_count; j++) {
                        close(service_listener_fds[j]);
                    }
                    handle_client(new_socket, current_port);
                    exit(0);
                } else if (pid > 0) {
                    // --- This is the PARENT process ---
                    close(new_socket); // Parent doesn't need the client socket
                    // Mark this port as in use by the new child
                    service_ports[i].in_use = 1;
                    service_ports[i].pid = pid;
                } else {
                    perror("fork");
                    close(new_socket);
                }
            }
        }
    }
    return 0; // Unreachable
}

/**
 * @brief Handles an incoming authentication request.
 * It is handled directly by the main process without forking.
 */
void handle_authentication(int auth_listener_fd) {
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int auth_socket = accept(auth_listener_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (auth_socket < 0) {
        perror("auth accept");
        return;
    }

    printf("Received an authentication request.\n");
    char buffer[1024] = {0};
    read(auth_socket, buffer, 1023);

    // Remove trailing newline character, if any
    buffer[strcspn(buffer, "\r\n")] = 0;

    // Super simple authentication check
    if (strcmp(buffer, "user:pass") == 0) {
        int free_port = find_free_port();
        if (free_port != 0) {
            printf("Authentication successful. Assigning port %d.\n", free_port);
            char port_str[16];
            snprintf(port_str, sizeof(port_str), "%d", free_port);
            send(auth_socket, port_str, strlen(port_str), 0);
        } else {
            printf("Authentication successful, but no free ports.\n");
            send(auth_socket, "0", 1, 0); // "0" indicates no port available
        }
    } else {
        printf("Authentication failed for received string: \"%s\".\n", buffer);
        send(auth_socket, "0", 1, 0); // "0" indicates failure
    }

    close(auth_socket); // Always close the connection after the attempt
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
 * It also marks the port used by the child as free.
 */
void sigchld_handler(int s) {
    int saved_errno = errno;
    pid_t pid;
    
    // Reap all terminated children
    while((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        // Find which port this child was handling and mark it as free
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


// --- Unchanged Functions from original code ---

/**
 * @brief Handles all communication for a single connected client.
 * This function runs inside a dedicated child process.
 */
void handle_client(int client_socket, int port) {
    char buffer[1024];
    long valread;

    // Greet the client
    char welcome_msg[64];
    snprintf(welcome_msg, sizeof(welcome_msg), "Welcome! You are connected to service port %d\n", port);
    send(client_socket, welcome_msg, strlen(welcome_msg), 0);


    while((valread = read(client_socket, buffer, 1024)) > 0) {
        buffer[valread] = '\0';
        printf("[Client on Port %d says]: %s", port, buffer);
        
        printf("Enter response for client on port %d: ", port);
        char response[1024];
        fgets(response, sizeof(response), stdin);
        
        send(client_socket, response, strlen(response), 0);
    }

    if (valread == 0) {
        printf("Client on port %d disconnected.\n", port);
    } else {
        perror("read");
    }

    close(client_socket);
}


/**
 * @brief Finds and prints the server's non-loopback IPv4 address.
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