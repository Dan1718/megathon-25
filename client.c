#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 9002

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    const char *hello = "Hello from client";
    char buffer[1024] = {0};

    // 1. Creating socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 2. Connecting to the server
    // Convert IPv4 addresses from text to binary form
    // "127.0.0.1" is the loopback address, meaning "this same machine".
    // If your server is on another machine, you would put its IP address here.
    if (inet_pton(AF_INET, "10.53.138.156", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // Connect to the server socket
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to server.\n");
    char string[100]; 
    scanf(" %s",string);
    
    send(sock, string, strlen(string), 0);


    // 4. Reading the server's response
    read(sock, buffer, 1024);
    printf("Server response: %s\n", buffer);
    scanf("%s",string);
    send(sock, string, strlen(string), 0);
    

    send(sock, string, strlen(string), 0);


    // 5. Closing the socket
    close(sock);

    return 0;
}