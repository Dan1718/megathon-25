// Needed for socket programming functions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9002

int main() {
    int server_fd, new_socket;
    long valread; // Use a signed type to check for -1 on error
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    const char *hello = "Hello from server";

    // --- Socket creation and setup is the same, it was correct ---
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    // --- This is the main server loop that allows it to accept multiple clients (one after another) ---
    while(1) {
        printf("\n++++ Waiting for new connection ++++\n\n");
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE); // In a real server, you might just continue
        }

        printf("Connection accepted.\n");

        // --- Inner loop to communicate with the connected client ---
        while(1) {
            char buffer[1024] = {0}; // Clear buffer for each new message
            valread = read(new_socket, buffer, 1024);

            // FIX: Check the return value of read()
            if (valread <= 0) {
                // If valread is 0, client disconnected gracefully.
                // If valread is < 0, an error occurred.
                printf("Client disconnected or error occurred.\n");
                break; // Exit the inner loop
            }

            printf("Client message: %s\n", buffer);
            send(new_socket, hello, strlen(hello), 0);
        }
        close(new_socket);
    }
    close(server_fd);
    return 0;
}
