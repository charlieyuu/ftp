#include "common.h"

void handle_signal(int sig) {
    printf("\nStopping server...\r\n");
    if (CONTROL_SOCKET > 0 && close(CONTROL_SOCKET) < 0) {
        printf("Error close(): %s(%d)\r\n", strerror(errno), errno);
        printf("Try to close all connections manually.\r\n");
        exit(EXIT_FAILURE);
    }
    printf("Server stopped.\r\n");
    exit(EXIT_SUCCESS);
}

void handle_client(int connection) {
    char buffer[BUFFER_SIZE];
    Command *command = malloc(sizeof(Command));
    State *state = malloc(sizeof(State));

    state->connection = connection;
    memset(buffer, 0, BUFFER_SIZE);
    state->message = "220 Anonymous FTP server ready.\r\n";
    write_message(state);
    // begin to receive and handle commands
    while ((read(connection, buffer, BUFFER_SIZE) > 0)) {
        printf("User %d sent command: %s", getpid(), buffer);
        sscanf(buffer, "%s %s", command->command, command->arg);
        if (handle_commands(command, state) < 0) {
            break;
        }
        memset(buffer, 0, BUFFER_SIZE);
        memset(command, 0, sizeof(*command));
    }
    free(command);
    free(state);
    return;
}

int handle_commands(Command *command, State *state) {
    if (check_permission_and_sequence(command, state) < 0) {
        return 0;
    }
    switch (lookup_command(command->command)) {
        case USER:
            handle_user(command, state);
            break;
        case PASS:
            handle_pass(command, state);
            break;
        case PORT:
            handle_port(command, state);
            break;
        case PASV:
            handle_pasv(command, state);
            break;
        case SIZE:
            handle_size(command, state);
            break;
        case REST:
            handle_rest(command, state);
            break;
        case RETR:
            handle_retr(command, state);
            break;
        case STOR:
            handle_stor(command, state);
            break;
        case SYST:
            handle_syst(command, state);
            break;
        case TYPE:
            handle_type(command, state);
            break;
        case QUIT:
            handle_quit(command, state);
            return -1;
        case ABOR:
            handle_abor(command, state);
            return -1;
        case MKD:
            handle_mkd(command, state);
            break;
        case CWD:
            handle_cwd(command, state);
            break;
        case PWD:
            handle_pwd(command, state);
            break;
        case LIST:
            handle_list(command, state);
            break;
        case RMD:
            handle_rmd(command, state);
            break;
        case RNFR:
            handle_rnfr(command, state);
            break;
        case RNTO:
            handle_rnto(command, state);
            break;
        default:
            state->message = "500 Syntax error, command unrecognized.\r\n";
            write_message(state);
    }
    return 0;
}

void handle_user(Command *command, State *state) {
    if (strcmp(command->arg, "anonymous") != 0) {
        state->message = "530 Username must be \'anonymous\'.\r\n";
        write_message(state);
    } else {
        state->username_ok = 1;
        state->message = "331 User name okay, need password.\r\n";
        write_message(state);
    }
}

void handle_pass(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments, use email as password.\r\n";
        write_message(state);
    } else {
        state->message = "230 User logged in, proceed.\r\n";
        write_message(state);
        state->logged_in = 1;
    }
}

void handle_port(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    // get ip and port from arg
    if (sscanf(command->arg, "%d,%d,%d,%d,%d,%d", &state->port_ip[0], &state->port_ip[1], &state->port_ip[2], &state->port_ip[3], &state->port_port[0], &state->port_port[1]) == -1) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
    } else {
        state->message = "200 Command ok.\r\n";
        write_message(state);
        state->mode = PORT_MODE;
    }
}

void handle_pasv(Command *command, State *state) {
    int port[2];
    char buff[BUFFER_SIZE];
    char *response = "227 Entering Passive Mode (%s,%d,%d)\r\n";
    do {
        // generate port
        srand(time(NULL));
        port[0] = 128 + (rand() % 64);
        port[1] = rand() % 0xff;
        // create socket but connect later
        state->passive_socket = create_socket((256 * port[0] + port[1]));
    } while (state->passive_socket < 0);
    sprintf(buff, response, get_ip(), port[0], port[1]);
    state->message = buff;
    state->mode = PASV_MODE;
    write_message(state);
}

void handle_size(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    int size = get_file_size(convert_to_local_path(command->arg));
    if (size < 0) {
        state->message = "550 Requested action not taken, cannot find target file.\r\n";
        write_message(state);
    } else {
        char buff[BUFFER_SIZE];
        memset(buff, 0, BUFFER_SIZE);
        sprintf(buff, "213 %d\r\n", size);
        state->message = buff;
        write_message(state);
    }
}

void handle_rest(Command *command, State *state) {
    state->start_position = atoi(command->arg);
    state->message = "350 Restarting at start_position.\r\n";
    write_message(state);
}

void handle_retr(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    int connection;
    int total_bytes = 0;
    struct sockaddr_in addr;
    socklen_t len;
    FILE* fp = fopen(convert_to_local_path(command->arg), "rb");
    if (fp == NULL) {
        state->message = "550 Failed to open target file.\r\n";
        write_message(state);
        return;
    } else {
        if (state->mode == PORT_MODE) {
            if (connect_port(state) < 0) {
                state->message = "425 No TCP connection was established.\r\n";
                write_message(state);
                return;
            }
            connection = state->port_socket;
        } else {
            connection = accept(state->passive_socket, (struct sockaddr*)&addr, &len);
            close(state->passive_socket);
        }
        state->message = "150 File status okay; about to open data connection.\r\n";
        write_message(state);
        pid_t pid;
        pid = fork();
        if (pid < 0) {
            state->message = "426 The TCP connection was established but then broken by the client or by network failure.\r\n";
            write_message(state);
            fclose(fp);
            close(connection);
            return;
        } else if (pid == 0) {
            if (state->start_position > 0) {
                fseek(fp, state->start_position, SEEK_SET);
            }
            total_bytes = send_file(fp, connection);
            if (total_bytes < 0) {
                state->message = "426 The TCP connection was established but then broken by the client or by network failure.\r\n";
                write_message(state);
            } else {
                state->message = "226 File send OK.\r\n";
                write_message(state);
            }
            fclose(fp);
            close(connection);
            exit(EXIT_SUCCESS);
        } else {
            fclose(fp);
            close(connection);
        }
    }
    state->mode = NORM_MODE;
    state->start_position = 0;
}

void handle_stor(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    int connection;
    int total_bytes = 0;
    struct sockaddr_in addr;
    socklen_t len;
    char file_name[BUFFER_SIZE];
    memset(file_name, 0, BUFFER_SIZE);
    char *file_path = command->arg;
    int i = strlen(file_path) - 1;
    while (i >= 0 && file_path[i] != '/') {
        i--;
    }
    if (i >= 0) {
        strcpy(file_name, file_path + i + 1);
    } else {
        strcpy(file_name, file_path);
    }
    FILE* fp = fopen(file_path, "wb");
    if (fp == NULL) {
        state->message = "550 Failed to open target file.\r\n";
        write_message(state);
        return;
    } else {
        if (state->mode == PORT_MODE) {
            if (connect_port(state) < 0) {
                remove(file_name);
                state->message = "425 No TCP connection was established.\r\n";
                write_message(state);
                return;
            }
            connection = state->port_socket;
        } else {
            connection = accept(state->passive_socket, (struct sockaddr*)&addr, &len);
            close(state->passive_socket);
        }
        state->message = "150 File status okay, about to open data connection.\r\n";
        write_message(state);
        pid_t pid = fork();
        if (pid < 0) {
            remove(file_name);
            state->message = "426 The TCP connection was established but then broken by the client or by network failure.\r\n";
            write_message(state);
            return;
        } else if (pid == 0) {
            if (state->start_position > 0) {
                fseek(fp, state->start_position, SEEK_SET);
            }
            total_bytes = recv_file(fp, connection);
            if (total_bytes < 0) {
                remove(file_name);
                state->message = "426 The TCP connection was established but then broken by the client or by network failure.\r\n";
                write_message(state);
            } else {
                state->message = "226 File send OK.\r\n";
                write_message(state);
            }
            fclose(fp);
            close(connection);
            exit(EXIT_SUCCESS);
        } else {
            fclose(fp);
            close(connection);
        }
    }
    state->mode = NORM_MODE;
    state->start_position = 0;
}

void handle_syst(Command *command, State *state) {
    state->message = "215 UNIX Type: L8\r\n";
    write_message(state);
}

void handle_type(Command *command, State *state) {
    if (strcmp(command->arg, "I") != 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
    } else {
        state->message = "200 Type set to I.\r\n";
        write_message(state);
    }
}

void handle_quit(Command *command, State *state) {
    state->message = "221 Goodbye.\r\n";
    write_message(state);
    printf("User %d disconnected.\r\n", getpid());
    close(state->connection);
}

void handle_abor(Command *command, State *state) {
    handle_quit(command, state);
}

void handle_mkd(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    if (access(command->arg, F_OK) == 0) {
        state->message = "550 Requested action not taken, directory already exists.\r\n";
    } else {
        if (mkdir(convert_to_local_path(command->arg), S_IRWXU) < 0) {
            state->message = "550 Requested action not taken, failed to create directory.\r\n";
            write_message(state);
        } else {
            state->message = "250 The directory was successfully created.\r\n";
            write_message(state);
        }
    }
}

void handle_cwd(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    char* old_cwd = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    getcwd(old_cwd, BUFFER_SIZE);
    if (chdir(convert_to_local_path(command->arg)) < 0) {
        state->message = "550 No such file or directory.\r\n";
        write_message(state);
        return;
    }
    char* new_cwd = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    getcwd(new_cwd, BUFFER_SIZE);
    if (strstr(new_cwd, SERVER_ROOT_PATH) != new_cwd) {
        chdir(old_cwd);
        state->message = "550 Permission denied, you cannot leave root directory.\r\n";
        write_message(state);
    } else {
        state->message = "250 Current directory was successfully changed.\r\n";
        write_message(state);
    }
}

void handle_pwd(Command *command, State *state) {
    char cwd[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    memset(cwd, 0, BUFFER_SIZE);
    memset(response, 0, BUFFER_SIZE);
    getcwd(cwd, BUFFER_SIZE);
    sprintf(response, "257 \"%s\"\r\n", convert_to_ftp_path(cwd));
    state->message = response;
    write_message(state);
}

void handle_list(Command *command, State *state) {
    int connection;
    struct sockaddr_in addr;
    socklen_t len;
    char cmd[BUFFER_SIZE];
    char buff[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    memset(cmd, 0, BUFFER_SIZE);
    memset(buff, 0, BUFFER_SIZE);
    memset(response, 0, BUFFER_SIZE);
    if (strlen(command->arg) > 0) {
        sprintf(cmd, "ls -l %s", convert_to_local_path(command->arg));
    } else {
        sprintf(cmd, "ls -l");
    }
    FILE* fp = popen(cmd, "r");
    if (fp == NULL) {
        state->message = "550 Failed to open target file.\r\n";
        write_message(state);
        return;
    } else {
        if (state->mode == PORT_MODE) {
            if (connect_port(state) < 0) {
                state->message = "425 No TCP connection was established.\r\n";
                write_message(state);
                return;
            }
            connection = state->port_socket;
        } else {
            connection = accept(state->passive_socket, (struct sockaddr*)&addr, &len);
            close(state->passive_socket);
        }
        state->message = "150 File status okay, about to open data connection.\r\n";
        write_message(state);
        while (fgets(buff, BUFFER_SIZE, fp) != NULL) {
            usleep(2);
            write(connection, buff, strlen(buff));
            memset(buff, 0, BUFFER_SIZE);
        }
        state->message = "226 The entire directory was successfully transmitted.\r\n";
        write_message(state);
        pclose(fp);
        close(connection);
    }
    
    state->mode = NORM_MODE;
}

void handle_rmd(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    char cmd[BUFFER_SIZE];
    snprintf(cmd, BUFFER_SIZE, "rm -rf %s", convert_to_local_path(command->arg));
    if (system(cmd) < 0) {
        state->message = "550 Cannot delete the directory.\r\n";
        write_message(state);
    } else {
        state->message = "250 The directory was successfully deleted.\r\n";
        write_message(state);
    }
}

void handle_rnfr(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    char *path = convert_to_local_path(command->arg);
    if (access(path, F_OK) != 0) {
        state->message = "550 Requested action not taken.\r\n";
        write_message(state);
    } else {
        state->old_file = path;
        state->renaming = 1;
        state->message = "350 RNFR accepted. Please supply new name for RNTO.\r\n";
        write_message(state);
    }
}

void handle_rnto(Command *command, State *state) {
    if (strlen(command->arg) == 0) {
        state->message = "501 Syntax error in parameters or arguments.\r\n";
        write_message(state);
        return;
    }
    if (rename(state->old_file, convert_to_local_path(command->arg)) < 0) {
        state->message = "550 Cannot rename the file.\r\n";
        write_message(state);
        printf("Error rename(): %s(%d)\r\n", strerror(errno), errno);
    } else {
        state->message = "250 Successfully rename the file.\r\n";
        write_message(state);
        state->renaming = 0;
    }
}
