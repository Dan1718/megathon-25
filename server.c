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
#define START_PORT 9000
#define END_PORT 9002
#define MAX_LISTENERS (END_PORT - START_PORT + 1)

// --- Function Prototypes ---
void handle_client(int client_socket, int port);
void print_server_ip();
void sigchld_handler(int s);

int main() {
    int listener_fds[MAX_LISTENERS];
    int listener_count = 0;
    struct sockaddr_in address;
    int opt = 1;

    // Print the server's IP address so clients know where to connect
    print_server_ip();

    // Set up a signal handler to clean up child processes (zombies)
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // 1. Create a listening socket for each port in the range
    for (int port = START_PORT; port <= END_PORT; port++) {
        int server_fd;
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("socket failed");
            continue; // Try next port
        }

        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("bind failed");
            close(server_fd);
            continue; // Try next port
        }

        if (listen(server_fd, 10) < 0) {
            perror("listen failed");
            close(server_fd);
            continue; // Try next port
        }

        printf("Server listening on port %d\n", port);
        listener_fds[listener_count++] = server_fd;
    }

    if (listener_count == 0) {
        fprintf(stderr, "Could not bind to any ports. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // 2. Main loop to accept connections on any of the listening ports
    printf("\n++++ Waiting for connections on ports %d-%d ++++\n", START_PORT, END_PORT);
    while(1) {
        fd_set read_fds;
        int max_fd = 0;
        FD_ZERO(&read_fds);

        // Add all listener sockets to the set to be monitored by select()
        for (int i = 0; i < listener_count; i++) {
            FD_SET(listener_fds[i], &read_fds);
            if (listener_fds[i] > max_fd) {
                max_fd = listener_fds[i];
            }
        }

        // select() blocks until a connection is ready on one of the sockets
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        // Check which listening socket got a connection
        for (int i = 0; i < listener_count; i++) {
            if (FD_ISSET(listener_fds[i], &read_fds)) {
                int new_socket;
                int addrlen = sizeof(address);
                if ((new_socket = accept(listener_fds[i], (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                    perror("accept");
                    continue;
                }

                int current_port = ntohs(address.sin_port);
                printf("\nConnection accepted on port %d! Forking new process to handle client.\n", START_PORT + i);
                
                // 3. Fork a new process to handle the client connection
                if (fork() == 0) {
                    // --- This is the CHILD process ---
                    // The child doesn't need the listener sockets, so close them.
                    for(int j = 0; j < listener_count; j++) {
                        close(listener_fds[j]);
                    }
                    // Handle all communication with the client
                    handle_client(new_socket, START_PORT + i);
                    // When done, exit the child process
                    exit(0);
                } else {
                    // --- This is the PARENT process ---
                    // The parent doesn't need the new client socket, so close it.
                    close(new_socket);
                }
            }
        }
    }

    // Cleanup (in a real server, this would be in a shutdown sequence)
    for(int i = 0; i < listener_count; i++) {
        close(listener_fds[i]);
    }
    return 0;
}


/**
 * @brief Handles all communication for a single connected client.
 * This function runs inside a dedicated child process.
 */
void handle_client(int client_socket, int port) {
    char buffer[1024];
    long valread;

    while((valread = read(client_socket, buffer, 1024)) > 0) {
        buffer[valread] = '\0'; // Null-terminate the string
        printf("[Client on Port %d says]: %s\n", port, buffer);
        
        // --- Interactive part for server operator ---
        // NOTE: If multiple clients send messages at the same time, these
        // prompts will get mixed up on the server's console. This is a
        // limitation of having multiple processes read from one stdin.
        printf("Enter response for client on port %d: ", port);
        char response[1024];
        fgets(response, sizeof(response), stdin); // Read a line from the server's keyboard
        
        send(client_socket, response, strlen(response), 0);
    }

    if (valread == 0) {
        printf("Client on port %d disconnected.\n", port);
    } else {
        perror("read");
    }

    // Close the client socket and the child process will exit.
    close(client_socket);
}


/**
 * @brief Finds and prints the server's non-loopback IPv4 address.
 */
void print_server_ip() {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    printf("Server IP addresses:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) { // We are interested in IPv4 addresses
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
            // strcmp checks if the address is the local loopback, which we want to ignore
            if (strcmp(host, "127.0.0.1") != 0) {
                 printf("  - %s (on interface %s)\n", host, ifa->ifa_name);
            }
        }
    }
    freeifaddrs(ifaddr);
}

/**
 * @brief Signal handler to clean up ("reap") terminated child processes.
 * This prevents them from becoming "zombies".
 */
void sigchld_handler(int s) {
    // waitpid() might overwrite errno, so we save and restore it
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}