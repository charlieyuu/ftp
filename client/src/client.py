import socket
import re
import enum
import sys
import os

command_list = ["USER", "PASS", "PORT", "PASV", "RETR", "STOR", "SYST", "TYPE", "QUIT", "ABOR", "MKD", "CWD", "PWD",
                "LIST", "RMD", "RNFR", "RNTO", ]


class Mode(enum.Enum):
    NORM = 0
    PORT = 1
    PASV = 2


class Client:
    def __init__(self):
        self.control_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.data_mode = Mode.NORM
        self.data_socket = None
        self.port_socket = None
        self.pasv_ip = None
        self.pasv_port = None
        self.start_position = 0

    def connect(self, host, port):
        try:
            self.control_socket.connect((host, port))
        except ConnectionRefusedError:
            return -1
        return 0

    def get_reply(self):
        reply = self.control_socket.recv(1024)
        return reply.decode()

    def handle_common(self, command):
        self.control_socket.sendall(command.encode())
        reply = self.get_reply()
        sys.stdout.write(reply)

    def handle_port(self, command):
        ip_port_str = re.search(r'\d+,\d+,\d+,\d+,\d+,\d+', command)
        if ip_port_str is None:
            sys.stderr.write("Invalid PORT command.\n")
            return
        ip_port = ip_port_str[0].split(',')
        ip = '.'.join(ip_port[:4])
        port = 256 * int(ip_port[4]) + int(ip_port[5])
        self.port_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.port_socket.bind((ip, port))
        self.port_socket.listen(5)
        self.control_socket.sendall(command.encode())
        reply = self.get_reply()
        sys.stdout.write(reply)
        self.data_mode = Mode.PORT

    def handle_pasv(self, command):
        self.control_socket.sendall(command.encode())
        reply = self.get_reply()
        if not reply.startswith("227"):
            sys.stdout.write(reply)
            return
        sys.stdout.write(reply)
        ip_port_str = re.search(r'\d+,\d+,\d+,\d+,\d+,\d+', reply)[0].split(',')
        self.pasv_ip = '.'.join(ip_port_str[:4])
        self.pasv_port = 256 * int(ip_port_str[4]) + int(ip_port_str[5])
        self.data_mode = Mode.PASV

    def handle_retr(self, command):
        file_name: str = command.removeprefix("RETR ").removesuffix("\r\n")
        if self.setup_data_connection(command) == -1:
            return
        file_name = os.path.basename(file_name)
        pid = os.fork()
        if pid == -1:
            sys.stderr.write("Fork error.\n")
            self.data_socket.close()
            self.data_socket = None
            self.data_mode = Mode.NORM
            return
        elif pid == 0:
            with open(file_name, "wb") as f:
                if self.start_position != 0:
                    f.seek(self.start_position)
                while True:
                    data = self.data_socket.recv(1024)
                    if not data:
                        break
                    f.write(data)
            self.data_socket.close()
            self.data_socket = None
            self.data_mode = Mode.NORM
            reply = self.get_reply()
            sys.stdout.write(reply + 'ftp> ')
            exit(0)
        else:
            self.data_socket.close()
            self.data_socket = None
            self.data_mode = Mode.NORM

    def handle_stor(self, command):
        file_name = command.removeprefix("STOR ").removesuffix("\r\n")
        if not os.path.exists(file_name):
            sys.stderr.write("No such file or directory.\n")
            return
        if self.setup_data_connection(command) == -1:
            return
        pid = os.fork()
        if pid == -1:
            sys.stderr.write("Fork error.\n")
            self.data_socket.close()
            self.data_socket = None
            self.data_mode = Mode.NORM
            return
        elif pid == 0:
            with open(file_name, "rb") as f:
                if self.start_position != 0:
                    f.seek(self.start_position)
                while True:
                    data = f.read(1024)
                    if not data:
                        break
                    self.data_socket.sendall(data)
            self.data_socket.close()
            self.data_socket = None
            self.data_mode = Mode.NORM
            reply = self.get_reply()
            sys.stdout.write(reply + 'ftp> ')
            exit(0)
        else:
            self.data_socket.close()
            self.data_socket = None
            self.data_mode = Mode.NORM

    def handle_list(self, command):
        if self.setup_data_connection(command) == -1:
            return
        while True:
            data = self.data_socket.recv(1024)
            if not data:
                break
            sys.stdout.write(data.decode())
        self.data_socket.close()
        self.data_socket = None
        self.data_mode = Mode.NORM
        reply = self.get_reply()
        sys.stdout.write(reply)

    def setup_data_connection(self, command):
        if self.data_mode == Mode.NORM:
            sys.stderr.write("No data connection.\n")
            return -1
        self.control_socket.sendall(command.encode())
        if self.data_mode == Mode.PASV:
            self.data_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                self.data_socket.connect((self.pasv_ip, self.pasv_port))
            except ConnectionRefusedError:
                sys.stderr.write("Data connection refused!\n")
                reply = self.get_reply()
                sys.stdout.write(reply)
                return -1
        elif self.data_mode == Mode.PORT:
            self.data_socket, _ = self.port_socket.accept()
            self.port_socket.close()
            self.port_socket = None
        reply = self.get_reply()
        if not reply.startswith("150"):
            sys.stdout.write(reply)
            return -1
        sys.stdout.write(reply)

    def handle_quit(self, command):
        self.control_socket.sendall(command.encode())
        reply = self.get_reply()
        sys.stdout.write(reply)
        self.control_socket.close()


def main(host, port):
    try:
        client = Client()
        if client.connect(host, port) == -1:
            sys.stderr.write("Connection refused!\n")
            exit(-1)
        welcome_msg = client.get_reply()
        sys.stdout.write(welcome_msg)
        while True:
            command = input('ftp> ')
            command += '\r\n'
            cmd = command.split()[0]
            if cmd in command_list:
                if cmd == "PORT":
                    client.handle_port(command)
                elif cmd == "PASV":
                    client.handle_pasv(command)
                elif cmd == "RETR":
                    client.handle_retr(command)
                elif cmd == "STOR":
                    client.handle_stor(command)
                elif cmd == "LIST":
                    client.handle_list(command)
                elif cmd == "QUIT" or cmd == "ABOR":
                    client.handle_quit(command)
                    break
                elif cmd == "REST":
                    client.start_position = int(command.removeprefix("REST ").removesuffix("\r\n"))
                    client.handle_common(command)
                elif cmd == "size":
                    file_name = command.removeprefix("size ").removesuffix("\r\n")
                    file_size = os.path.getsize(file_name)
                    sys.stdout.write('file size: ' + str(file_size) + '\n')
                else:
                    client.handle_common(command)
            else:
                sys.stderr.write("Invalid command.\n")
    except KeyboardInterrupt:
        print('')
        print("Stopping client...", sep='\n')
        client.handle_quit("QUIT\r\n")
        print("Client stopped.")


if __name__ == "__main__":
    args = sys.argv
    host = '127.0.0.1'
    port = 21
    if len(args) == 3:
        host = args[1]
        port = int(args[2])
    elif len(args) == 2:
        port = int(args[1])
    main(host, port)
