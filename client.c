#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define AUTH_PORT 9000
// IMPORTANT: Change this IP to the actual IP address of your server.
// "127.0.0.1" works if the client and server are on the same machine.
//#define SERVER_IP "127.0.0.1" 
#define SERVER_IP "10.53.138.156" // Use this line if your server is at this IP


int connect_to_server(const char* ip, int port);

int main() {
    int auth_sock = 0;
    char buffer[1024] = {0};
    char credentials[100];

    // --- Phase 1: Authentication ---
    printf("--- Attempting to authenticate with server at %s:%d ---\n", SERVER_IP, AUTH_PORT);
    
    // Connect to the authentication port
    auth_sock = connect_to_server(SERVER_IP, AUTH_PORT);
    if (auth_sock < 0) {
        return -1; // Error message is printed inside the function
    }

    // Get credentials from user input
    printf("Enter credentials (e.g., user:pass): ");
    // Use fgets for safer input handling than scanf
    if (fgets(credentials, sizeof(credentials), stdin) == NULL) {
        fprintf(stderr, "Error reading credentials.\n");
        close(auth_sock);
        return -1;
    }
    // Remove trailing newline character from fgets
    credentials[strcspn(credentials, "\r\n")] = 0;

    // Send the credentials to the authentication server
    send(auth_sock, credentials, strlen(credentials), 0);
    printf("Credentials sent. Waiting for a service port...\n");

    // Read the response (the new port number or "0")
    int valread = read(auth_sock, buffer, 1023);
    if (valread <= 0) {
        fprintf(stderr, "Failed to get response from auth server.\n");
        close(auth_sock);
        return -1;
    }
    buffer[valread] = '\0'; // Null-terminate the response

    // The authentication part is done, close the socket.
    close(auth_sock);


    // --- Phase 2: Connect to the new service port ---
    
    // Convert the received port string to an integer
    int service_port = atoi(buffer);

    // Check if authentication was successful
    if (service_port == 0) {
        printf("\nAuthentication failed or no service ports available. Exiting.\n");
        return -1;
    }

    printf("\n--- Authentication successful! Connecting to service port %d ---\n", service_port);
    
    int service_sock = connect_to_server(SERVER_IP, service_port);
    if (service_sock < 0) {
        return -1;
    }

    // Read the initial welcome message from the service port
    memset(buffer, 0, sizeof(buffer)); // Clear buffer
    read(service_sock, buffer, 1023);
    printf("Server says: %s", buffer);

    // --- Phase 3: Interactive Chat Loop ---
    printf("You can now chat with the server. Type 'exit' to quit.\n");
    char message[1024];
    while(1) {
        printf("Client> ");
        if (fgets(message, sizeof(message), stdin) == NULL) {
            break; // Exit on Ctrl+D
        }

        // Check if user wants to exit
        if (strncmp(message, "exit", 4) == 0) {
            break;
        }

        // Send message to the server
        send(service_sock, message, strlen(message), 0);

        // Receive response from the server
        memset(buffer, 0, sizeof(buffer));
        valread = read(service_sock, buffer, 1023);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("Server> %s", buffer);
        } else {
            printf("Server disconnected.\n");
            break;
        }
    }

    // Clean up and close the final connection
    printf("Disconnecting from service port.\n");
    close(service_sock);

    return 0;
}


/**
 * @brief Helper function to create a socket and connect to a server.
 * @param ip The IP address of the server.
 * @param port The port number to connect to.
 * @return The socket file descriptor on success, or -1 on failure.
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