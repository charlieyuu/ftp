#include "common.h"

const char *command_str_list[] = {
    "USER", "PASS", "PORT", "PASV", "SIZE", "REST", "RETR", "STOR",
    "SYST", "TYPE", "QUIT", "ABOR", "MKD", "CWD",
    "PWD", "LIST", "RMD", "RNFR", "RNTO",
};

int create_socket(int port) {
    int socket_fd;
    struct sockaddr_in addr;
    // create tcp socket
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("Error socket(): %s(%d)\r\n", strerror(errno), errno);
        return -1;
    }
    // set host port and ip
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // bind ip to socket
    if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        printf("Error bind(): %s(%d)\r\n", strerror(errno), errno);
        return -1;
    }
    // start listening
    if (listen(socket_fd, 10) == -1) {
        printf("Error listen(): %s(%d)\r\n", strerror(errno), errno);
        return -1;
    }
    return socket_fd;
}

int lookup_command(char *command) {
    int command_count = sizeof(command_str_list) / sizeof(char *);
    int i;
    for (i = 0; i < command_count; i++) {
        if (strcmp(command, command_str_list[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void write_message(State *state) {
    if (write(state->connection, state->message, strlen(state->message)) == -1) {
        printf("Error write(): %s(%d)\n", strerror(errno), errno);
    }
}

int check_permission_and_sequence(Command *command, State *state) {
    int cmd = lookup_command(command->command);
    if (state->logged_in == 0 && cmd == STOR) {
        state->message = "532 Need account for storing files.\r\n";
        write_message(state);
        return -1;
    }
    if ((state->username_ok == 0 && cmd != USER && cmd != PASS) ||
        (state->username_ok == 1 && state->logged_in == 0 && cmd != PASS)) {
        state->message = "530 Not logged in.\r\n";
        write_message(state);
        return -1;
    }
    if (state->username_ok == 0 && cmd == PASS) {
        state->message = "503 Bad sequence of commands, use USER first.\r\n";
        write_message(state);
        return -1;
    }
    if (state->mode == NORM_MODE && 
        (cmd == RETR || cmd == STOR || cmd == LIST)) {
        state->message = "425 Can't open data connection, use PORT or PASV to create one first.\r\n";
        write_message(state);
        return -1;
    }
    if (state->renaming == 1 && cmd != RNTO) {
        state->message = "503 Bad sequence of commands, use RNTO to finish renaming.\r\n";
        write_message(state);
        return -1;
    }
    if (state->renaming == 0 && cmd == RNTO) {
        state->message = "503 Bad sequence of commands, use RNFR to start renaming.\r\n";
        write_message(state);
        return -1;
    }
    return 0;
}

char *convert_to_local_path(char *path) {
    // convert a path rooted at SERVER_ROOT_PATH to local path
    char *new_path = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    memset(new_path, 0, BUFFER_SIZE);
    if (path[0] == '/') {
        strcpy(new_path, SERVER_ROOT_PATH);
        strcat(new_path, path);
    } else {
        getcwd(new_path, BUFFER_SIZE);
        strcat(new_path, "/");
        strcat(new_path, path);
    }
    return new_path;
}

char *convert_to_ftp_path(char *path) {
    // convert a local path to a path rooted at SERVER_ROOT_PATH
    char *new_path = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    memset(new_path, 0, BUFFER_SIZE);
    if (strstr(path, SERVER_ROOT_PATH) == path) {
        strcpy(new_path, path + strlen(SERVER_ROOT_PATH));
    } else {
        strcpy(new_path, path);
    }
    strcat(new_path, "/");
    return new_path;
}

char *get_ip() {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        printf("Error getifaddrs(): %s(%d)\r\n", strerror(errno), errno);
        return NULL;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        char mask[INET_ADDRSTRLEN];
        memset(mask, 0, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, (void *)&((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr, mask, INET_ADDRSTRLEN);
        if (strcmp(mask, "255.0.0.0") == 0) {
            continue;
        }
        char *res = (char *)malloc(20);
        memset(res, 0, 20);
        inet_ntop(AF_INET, (void *)&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, res, INET_ADDRSTRLEN);
        int len = strlen(res);
        for (int i = 0; i < len; i++) {
            if (res[i] == '.') {
                res[i] = ',';
            }
        }
        return res;
    }
    return NULL;
}

int connect_port(State *state) {
    char buff[BUFFER_SIZE];
    state->port_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(256 * state->port_port[0] + state->port_port[1]);
    sprintf(buff, "%d.%d.%d.%d", state->port_ip[0], state->port_ip[1], state->port_ip[2], state->port_ip[3]);
    if (inet_pton(AF_INET, buff, &addr.sin_addr) <= 0) {
        printf("Error inet_pton(): %s(%d)\r\n", strerror(errno), errno);
        return -1;
    } else if (connect(state->port_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("Error connect(): %s(%d)\r\n", strerror(errno), errno);
        return -1;
    }
    return 0;
}

int send_file(FILE* fp, int connection) {
    int total_bytes = 0;
    int bytes_write = 0;
    char buff[BUFFER_SIZE];
    memset(buff, 0, BUFFER_SIZE);
    while (1) {
        usleep(2);
        bytes_write = fread(buff, sizeof(char), BUFFER_SIZE, fp);
        if (bytes_write <= 0) {
            break;
        }
        if (write(connection, buff, bytes_write) < 0) {
            printf("Error write(): %s(%d)\r\n", strerror(errno), errno);
            return -1;
        }
        total_bytes += bytes_write;
        memset(buff, 0, BUFFER_SIZE);
    }
    return bytes_write;
}

int recv_file(FILE* fp, int connection) {
    int total_bytes = 0;
    int bytes_read = 0;
    char buff[BUFFER_SIZE];
    memset(buff, 0, BUFFER_SIZE);
    while (1) {
        usleep(2);
        bytes_read = read(connection, buff, BUFFER_SIZE);
        if (bytes_read <= 0) {
            break;
        }
        if (fwrite(buff, sizeof(char), bytes_read, fp) < bytes_read) {
            printf("Error fwrite(): %s(%d)\r\n", strerror(errno), errno);
            return -1;
        }
        total_bytes += bytes_read;
        memset(buff, 0, BUFFER_SIZE);
    }
    return bytes_read;
}

int get_file_size(char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) == -1) {
        printf("Error stat(): %s(%d)\r\n", strerror(errno), errno);
        return -1;
    }
    return statbuf.st_size;
}