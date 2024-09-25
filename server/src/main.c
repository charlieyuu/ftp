#include "common.h"

int CONTROL_SOCKET = -1;
char *SERVER_ROOT_PATH = "/tmp";

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGQUIT, handle_signal);
    int port = 21;
    char *root;
    char buff[BUFFER_SIZE];
    memset(buff, 0, BUFFER_SIZE);
    // get port and root from cmd if exists
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0 && (i + 1 != argc)) {
            port = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-root") == 0 && (i + 1 != argc)) {
            root = argv[i + 1];
            // check the root dir and cd to it
            if (access(root, F_OK) < 0) {
                if(mkdir(root, S_IRWXU) < 0) {
                    printf("Error mkdir(): %s(%d)\r\n", strerror(errno), errno);
                    exit(EXIT_FAILURE);
                }
            }
            SERVER_ROOT_PATH = root;
        }
    }
    if (chdir(SERVER_ROOT_PATH) < 0) {
        printf("Error chdir(): %s(%d)\r\n", strerror(errno), errno);
    }
    getcwd(buff, BUFFER_SIZE);
    SERVER_ROOT_PATH = buff;
    // begin server
    start_server(port);
    return 0;
}