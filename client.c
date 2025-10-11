#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define AUTH_PORT 9000
// IMPORTANT: Change this IP to the actual IP address of your server.
#define SERVER_IP "127.0.0.1" 


int connect_to_server(const char* ip, int port);

int main() {
    int auth_sock = 0;
    char buffer[1024] = {0};
    char credentials[100];

    // --- Phase 1: Authentication ---
    printf("--- Attempting to authenticate with server at %s:%d ---\n", SERVER_IP, AUTH_PORT);
    
    auth_sock = connect_to_server(SERVER_IP, AUTH_PORT);
    if (auth_sock < 0) {
        return -1;
    }

    printf("Enter credentials (e.g., user:pass): ");
    if (fgets(credentials, sizeof(credentials), stdin) == NULL) {
        fprintf(stderr, "Error reading credentials.\n");
        close(auth_sock);
        return -1;
    }
    credentials[strcspn(credentials, "\r\n")] = 0;

    send(auth_sock, credentials, strlen(credentials), 0);
    printf("Credentials sent. Waiting for service port pair...\n");

    int valread = read(auth_sock, buffer, 1023);
    if (valread <= 0) {
        fprintf(stderr, "Failed to get response from auth server.\n");
        close(auth_sock);
        return -1;
    }
    buffer[valread] = '\0'; 
    close(auth_sock);

    // --- Phase 2: Connect to the new service port PAIR ---
    int send_port = atoi(buffer);
    if (send_port == 0) {
        printf("\nAuthentication failed or no service ports available. Exiting.\n");
        return -1;
    }
    int recv_port = send_port + 1;

    printf("\n--- Auth successful! Connecting to SEND port %d and RECV port %d ---\n", send_port, recv_port);
    
    int send_sock = connect_to_server(SERVER_IP, send_port);
    if (send_sock < 0) return -1;

    // The server child needs a moment to set up the listening socket on the second port
    sleep(1); 
    
    int recv_sock = connect_to_server(SERVER_IP, recv_port);
    if (recv_sock < 0) {
        close(send_sock);
        return -1;
    }
    
    // --- Phase 3: Fork to handle sending and receiving concurrently ---
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // --- CHILD PROCESS: Handles SENDING data to server ---
        close(recv_sock); // Child doesn't need the receiving socket
        printf("\nType your messages below. Type 'exit' to quit.\nClient> ");
        fflush(stdout);

        char message[1024];
        while (fgets(message, sizeof(message), stdin) != NULL) {
            send(send_sock, message, strlen(message), 0);
            if (strncmp(message, "exit", 4) == 0) {
                break;
            }
            printf("Client> ");
            fflush(stdout);
        }
        printf("Disconnecting sender.\n");
        close(send_sock);

    } else {
        // --- PARENT PROCESS: Handles RECEIVING data from server ---
        close(send_sock); // Parent doesn't need the sending socket
        
        while ((valread = read(recv_sock, buffer, 1023)) > 0) {
            buffer[valread] = '\0';
            // Move cursor to beginning of line, clear it, print message, then reprint prompt
            printf("\r\x1b[KServer> %s", buffer);
            printf("Client> ");
            fflush(stdout);
        }
        
        if (valread == 0) {
            printf("\r\nServer closed the connection.\n");
        } else {
            perror("read");
        }

        // When the loop breaks, the server has disconnected.
        // We must kill the child process that is waiting for keyboard input.
        kill(pid, SIGKILL);
        wait(NULL); // Clean up the killed child
        close(recv_sock);
    }

    return 0;
}


/**
 * @brief Helper function to create a socket and connect to a server.
 */
int connect_to_server(const char* ip, int port) {
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