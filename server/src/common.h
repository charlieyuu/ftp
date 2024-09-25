#define _GNU_SOURCE
#ifndef SERVER_H_
#define SERVER_H_

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <memory.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <ifaddrs.h>
#define BUFFER_SIZE 1024

extern int CONTROL_SOCKET;
extern char *SERVER_ROOT_PATH;

typedef struct Command
{
    char command[5];
    char arg[BUFFER_SIZE];
} Command;

typedef struct State
{
    int mode;
    int username_ok;
    int logged_in;
    char *message;
    int connection;
    int passive_socket;
    int port_socket;
    int port_ip[4];
    int port_port[2];
    int renaming;
    char *old_file;
    int fpid;
    int start_position;
} State;

typedef enum mode_list {
    NORM_MODE, PORT_MODE, PASV_MODE,
} mode_list;

typedef enum command_list {
    USER, PASS, PORT, PASV, SIZE, REST, RETR, STOR, SYST, TYPE, QUIT, ABOR,
    MKD, CWD, PWD, LIST, RMD, RNFR, RNTO,
} command_list; 

void handle_signal(int);
void start_server(int);
int create_socket(int);
void handle_client(int);
int handle_commands(Command*, State*);
int check_permission_and_sequence(Command*, State*);
int lookup_command(char*);
void write_message(State*);
char *convert_to_ftp_path(char*);
char *convert_to_local_path(char*);
char *get_ip();
int connect_port(State*);
int send_file(FILE*, int);
int recv_file(FILE*, int);
int get_file_size(char*);

// handle.c commands
void handle_user(Command*, State*);
void handle_pass(Command*, State*);
void handle_port(Command*, State*);
void handle_pasv(Command*, State*);
void handle_size(Command*, State*);
void handle_rest(Command*, State*);
void handle_retr(Command*, State*);
void handle_stor(Command*, State*);
void handle_syst(Command*, State*);
void handle_type(Command*, State*);
void handle_quit(Command*, State*);
void handle_abor(Command*, State*);
void handle_mkd(Command*, State*);
void handle_cwd(Command*, State*);
void handle_pwd(Command*, State*);
void handle_list(Command*, State*);
void handle_rmd(Command*, State*);
void handle_rnfr(Command*, State*);
void handle_rnto(Command*, State*);


#endif
