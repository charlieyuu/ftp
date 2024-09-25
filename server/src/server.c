#include "common.h"

void start_server(int port) {
    int connection;
    int listen_socket = create_socket(port);
    if (listen_socket < 0) {
        printf("Error create_socket(): %s(%d)\r\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }
    CONTROL_SOCKET = listen_socket;
    struct sockaddr_in client_address;
    socklen_t len = sizeof(client_address);
    while (1) {
        // accept a connection from listen_socket and create a subprocess to handle this connection
        connection = accept(listen_socket, (struct sockaddr *) &client_address, &len);
        if (connection < 0) {
            printf("Error accept(): %s(%d)\r\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        }
        pid_t pid;
        pid = fork();
        if (pid < 0) {
            printf("Error fork(): %s(%d)\r\n", strerror(errno), errno);
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            handle_client(connection);
            exit(EXIT_SUCCESS);
        } else {
            close(connection);
        }
    }
}