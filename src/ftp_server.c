/*  ftp_server.c
 *   2024 by romheat@gmail.com
 *   A simple FTP server that supports passive mode only.
 *
 *   Usage: ./ftp_server [port] [server_ip]
 *
 *   If no port is provided, the server will listen on port 21.
 *   If no server IP is provided, the server will listen on the local IP
 * address. The server will only accept one connection at a time.
 *
 *   Supported commands:
 *   - USER
 *   - PASS
 *   - PWD
 *   - CWD
 *   - TYPE
 *   - PASV
 *   - NLST
 *   - LIST
 *   - RETR
 *   - STOR
 *   - QUIT
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PORT 21
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 5
#define MAX_PATH 512
#define DEFAULT_PORT 21

typedef struct {
  int control_socket;
  int data_socket;
  struct in_addr client_addr;
  char current_dir[MAX_PATH - 1];
} ClientConnection;

typedef struct {
  const char *command;
  bool (*handler)(ClientConnection *conn, const char *arg);
} FtpCommand;

bool cmd_user(ClientConnection *conn, const char *arg);
bool cmd_pass(ClientConnection *conn, const char *arg);
bool cmd_pwd(ClientConnection *conn, const char *arg);
bool cmd_cwd(ClientConnection *conn, const char *arg);
bool cmd_type(ClientConnection *conn, const char *arg);
bool cmd_pasv(ClientConnection *conn, const char *arg);
bool cmd_nlst(ClientConnection *conn, const char *arg);
bool cmd_dir(ClientConnection *conn, const char *arg);
bool cmd_retr(ClientConnection *conn, const char *arg);
bool cmd_stor(ClientConnection *conn, const char *arg);
bool cmd_quit(ClientConnection *conn, const char *arg);

FtpCommand ftp_commands[] = {
    {"USER", cmd_user}, {"PASS", cmd_pass}, {"PWD", cmd_pwd},
    {"CWD", cmd_cwd},   {"TYPE", cmd_type}, {"PASV", cmd_pasv},
    {"NLST", cmd_nlst}, {"RETR", cmd_retr}, {"STOR", cmd_stor},
    {"QUIT", cmd_quit}, {"LIST", cmd_dir},  {NULL, NULL}};

char server_ip[16] = "";
int server_port = DEFAULT_PORT;

#define MSG_RUNNING "FTP server listening on port %d\n"
#define MSG_NEW_CLIENT "New client connected from %s\n"
#define MSG_WELCOME                                                            \
  "220 Welcome to romheat mini FTP Server (Passive Mode Only)\r\n"
#define MSG_USER_OK "331 User name okay, need password\r\n"
#define MSG_USER_LOGGED "230 User logged in\r\n"
#define MSG_PWD "257 \"%s\" is the current directory\r\n"
#define MSG_CWD_OK "250 Directory successfully changed\r\n"
#define MSG_CWD_FAIL "550 Failed to change directory\r\n"
#define MSG_TYPE_OK "200 Type set to I\r\n"
#define MSG_ENTER_PASV "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n"
#define MSG_LIST_START                                                         \
  "150 Opening ASCII mode data connection for file list\r\n"
#define MSG_LIST_END "226 Transfer complete\r\n"
#define MSG_RETR_START "150 Opening BINARY mode data connection\r\n"
#define MSG_RETR_END "226 Transfer complete\r\n"
#define MSG_STOR_START "150 Opening BINARY mode data connection\r\n"
#define MSG_STOR_END "226 Transfer complete\r\n"
#define MSG_QUIT "221 Goodbye\r\n"
#define MSG_SYNTAX_ERROR "500 Syntax error, command unrecognized\r\n"
#define MSG_NOT_IMPLEMENTED "502 Command not implemented\r\n"
#define MSG_DATA_CONN_FAIL "425 Can't open data connection\r\n"
#define MSG_GOODBYE "221 Goodbye\r\n"

#define ERR_GETCWD_FAIL "getcwd() error"
#define ERR_ACCEPT_FAIL "Accept failed"
#define ERR_SOCKET_FAIL "Socket creation failed"
#define ERR_SETSOCKOPT_FAIL "setsockopt(SO_REUSEADDR) failed"
#define ERR_BIND_FAIL "Bind failed"
#define ERR_LISTEN_FAIL "Listen failed"
#define ERR_SOCKET_FAIL "Socket creation failed"
#define ERR_SEND_FAIL "Error sending response"
#define ERR_OPEN_DIR "Unable to open directory"
#define ERR_OPEN_FILE "Unable to open file"
#define ERR_CREATE_FILE "Unable to create file"
#define ERR_PORT_FAIL "Invalid port number %d\n"
#define ERR_RECV_FAIL "Error receiving data\n"
#define ERR_CLIENT_DISCONNECT "Client disconnected\n"

#define LOG_SERVER_INFO "Server running on %s port %d\n"
#define LOG_CWD "Current working dir: %s\n"
#define LOG_CLOSING "Closing connection from %s\n"
#define LOG_RECEIVED "Received [%s]: %s"
#define LOG_SENT "Sent: %s"

int create_server_socket(int port);
int create_data_socket();
void handle_client(ClientConnection *conn);
bool handle_command(ClientConnection *conn, char *buffer);
void send_response(int socket, const char *format, ...);
void list_directory(int socket, const char *path);
void list_directory_extend(int socket, const char *path);
void send_file(int socket, const char *filename);
void receive_file(int socket, const char *filename);
void change_directory(ClientConnection *conn, const char *path);
void get_local_ip();

int main(int argc, char *argv[]) {

  if (argc == 2) {
    // Only server IP is provided, use default port
    server_port = DEFAULT_PORT;
    strncpy(server_ip, argv[1], 16);
    server_ip[15] = '\0';
  } else if (argc == 3) {
    // Both port and server IP are provided
    server_port = atoi(argv[1]);
    strncpy(server_ip, argv[2], 16);
    server_ip[15] = '\0';

    if (server_port <= 0 || server_port > 65535) {
      fprintf(stderr, ERR_PORT_FAIL, server_port);
      return 1;
    }
  } else {
    get_local_ip();
    // fprintf(stderr, "Usage: %s [port] [server_ip] or %s [server_ip]\n",
    // argv[0], argv[0]); return 1;
  }

  printf(LOG_SERVER_INFO, server_ip, server_port);

  int server_socket = create_server_socket(server_port);
  if (server_socket < 0) {
    exit(EXIT_FAILURE);
  }

  while (1) {
    ClientConnection conn;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    conn.control_socket =
        accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
    if (conn.control_socket < 0) {
      perror(ERR_ACCEPT_FAIL);
      continue;
    }

    printf(MSG_NEW_CLIENT, inet_ntoa(client_addr.sin_addr));
    conn.data_socket = create_data_socket();
    conn.client_addr = client_addr.sin_addr;
    if (getcwd(conn.current_dir, MAX_PATH) != NULL) {

      printf(LOG_CWD, conn.current_dir);
    } else {

      perror(ERR_GETCWD_FAIL);
      return 1;
    }

    if (conn.data_socket < 0) {
      send_response(conn.control_socket, MSG_DATA_CONN_FAIL);
      close(conn.control_socket);
      continue;
    }

    handle_client(&conn);
  }

  close(server_socket);
  return 0;
}

int create_server_socket(int port) {
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {

    perror(ERR_SOCKET_FAIL);
    return -1;
  }

  struct sockaddr_in server_addr = {.sin_family = AF_INET,
                                    .sin_addr.s_addr = INADDR_ANY,
                                    .sin_port = htons(port)};

  int enable = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) < 0) {

    perror(ERR_SETSOCKOPT_FAIL);
    close(server_socket);
    return -1;
  }

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {

    perror(ERR_BIND_FAIL);
    close(server_socket);
    return -1;
  }

  if (listen(server_socket, MAX_CLIENTS) < 0) {

    perror(ERR_LISTEN_FAIL);
    close(server_socket);
    return -1;
  }

  return server_socket;
}

int create_data_socket() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {

    perror(ERR_SOCKET_FAIL);
    return -1;
  }

  struct sockaddr_in addr = {
      .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = 0};

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror(ERR_BIND_FAIL);
    close(sock);
    return -1;
  }

  if (listen(sock, 1) < 0) {
    perror(ERR_LISTEN_FAIL);
    close(sock);
    return -1;
  }

  return sock;
}

void handle_client(ClientConnection *conn) {
  char buffer[BUFFER_SIZE];
  ssize_t bytes_received;

  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  getsockname(conn->data_socket, (struct sockaddr *)&addr, &len);

  send_response(conn->control_socket, MSG_WELCOME);

  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    bytes_received = recv(conn->control_socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received <= 0) {
      printf(bytes_received == 0 ? ERR_CLIENT_DISCONNECT : ERR_RECV_FAIL);
      break;
    }

    buffer[bytes_received] = '\0';
    printf(LOG_RECEIVED, inet_ntoa(conn->client_addr), buffer);

    if (handle_command(conn, buffer)) {
      // Close the connection if handle_command returns true
      break;
    }
    fflush(stdout);
  }

  printf(LOG_CLOSING, inet_ntoa(conn->client_addr));

  close(conn->data_socket);
  close(conn->control_socket);
}

void send_response(int socket, const char *format, ...) {
  char buffer[BUFFER_SIZE];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (send(socket, buffer, strlen(buffer), 0) < 0) {
    perror(ERR_SEND_FAIL);
  }

  printf(LOG_SENT, buffer);
}

void list_directory(int socket, const char *path) {
  DIR *dir;
  struct dirent *entry;
  char buffer[BUFFER_SIZE];

  dir = opendir(path);
  if (dir == NULL) {
    perror(ERR_OPEN_DIR);
    return;
  }

  while ((entry = readdir(dir)) != NULL) {
    // skip . and ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    snprintf(buffer, BUFFER_SIZE, "%s\r\n", entry->d_name);
    send(socket, buffer, strlen(buffer), 0);
  }

  closedir(dir);
}

void list_directory_extend(int socket, const char *path) {
  DIR *dir;
  struct dirent *entry;
  char buffer[BUFFER_SIZE];

  dir = opendir(path);
  if (dir == NULL) {

    perror(ERR_OPEN_DIR);
    return;
  }

  // while ((entry = readdir(dir)) != NULL) {
  //   snprintf(buffer, BUFFER_SIZE, "%s\r\n", entry->d_name);
  //   send(socket, buffer, strlen(buffer), 0);
  // }

  struct stat file_stat;
  char file_path[BUFFER_SIZE];
  char time_buffer[100];
  struct tm *tm_info;

  struct passwd *pw;
  struct group *gr;
  char permissions[11];

  while ((entry = readdir(dir)) != NULL) {
    snprintf(file_path, BUFFER_SIZE, "%s/%s", path, entry->d_name);

    if (stat(file_path, &file_stat) == -1) {
      perror("stat");
      continue;
    }

    // Get the user and group names
    pw = getpwuid(file_stat.st_uid);
    gr = getgrgid(file_stat.st_gid);

    // Get the permissions
    snprintf(permissions, sizeof(permissions), "%c%c%c%c%c%c%c%c%c%c",
             (S_ISDIR(file_stat.st_mode)) ? 'd' : '-',
             (file_stat.st_mode & S_IRUSR) ? 'r' : '-',
             (file_stat.st_mode & S_IWUSR) ? 'w' : '-',
             (file_stat.st_mode & S_IXUSR) ? 'x' : '-',
             (file_stat.st_mode & S_IRGRP) ? 'r' : '-',
             (file_stat.st_mode & S_IWGRP) ? 'w' : '-',
             (file_stat.st_mode & S_IXGRP) ? 'x' : '-',
             (file_stat.st_mode & S_IROTH) ? 'r' : '-',
             (file_stat.st_mode & S_IWOTH) ? 'w' : '-',
             (file_stat.st_mode & S_IXOTH) ? 'x' : '-');

    // send(socket, buffer, strlen(buffer), 0);
    tm_info = localtime(&file_stat.st_mtime);
    strftime(time_buffer, sizeof(time_buffer), "%y-%m-%d %H:%M", tm_info);
    float size = file_stat.st_size / 1024.0;

    snprintf(buffer, BUFFER_SIZE, "%s %s %s \t%s\t%1.fK\t%s\r\n", permissions,
             pw ? pw->pw_name : "", gr ? gr->gr_name : "", time_buffer, size,
             entry->d_name);

    send(socket, buffer, strlen(buffer), 0);
  }

  closedir(dir);
}

void send_file(int socket, const char *filename) {
  int file_fd;
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;

  printf("- sending file %s\n", filename);
  file_fd = open(filename, O_RDONLY);
  if (file_fd == -1) {
    perror(ERR_OPEN_FILE);
    return;
  }

  while ((bytes_read = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
    send(socket, buffer, bytes_read, 0);
  }

  close(file_fd);
}

void change_directory(ClientConnection *conn, const char *path) {
  char new_path[MAX_PATH];
  int result;

  if (path[0] == '/') {
    // Absolute path
    result = snprintf(new_path, MAX_PATH, "%s", path);
    // strncpy(new_path, path, MAX_PATH);
  } else {
    // Relative path
    result = snprintf(new_path, MAX_PATH, "%s/%s", conn->current_dir, path);
  }

  if (result >= MAX_PATH) {
    send_response(conn->control_socket, MSG_CWD_FAIL);
    return;
  }

  // Resolve the path (remove ".." and ".")
  char resolved_path[MAX_PATH];
  if (realpath(new_path, resolved_path) == NULL) {
    send_response(conn->control_socket, MSG_CWD_FAIL);
    return;
  }

  // Check if the directory exists and is accessible
  if (chdir(resolved_path) == 0) {
    strncpy(conn->current_dir, resolved_path, MAX_PATH);
    send_response(conn->control_socket, MSG_CWD_OK);
  } else {
    send_response(conn->control_socket, MSG_CWD_FAIL);
  }
}

void receive_file(int socket, const char *filename) {
  int file_fd;
  char buffer[BUFFER_SIZE];
  ssize_t bytes_received;

  printf("- receiving file %s\n", filename);
  file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file_fd == -1) {
    perror(ERR_CREATE_FILE);
    return;
  }

  while ((bytes_received = recv(socket, buffer, BUFFER_SIZE, 0)) > 0) {
    write(file_fd, buffer, bytes_received);
  }

  close(file_fd);
}

bool cmd_user(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_USER_OK);
  return false;
}

bool cmd_pass(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_USER_LOGGED);
  return false;
}

bool cmd_pwd(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_PWD, conn->current_dir);
  return false;
}

bool cmd_cwd(ClientConnection *conn, const char *arg) {
  change_directory(conn, arg);
  return false;
}

bool cmd_type(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_TYPE_OK);
  return false;
}

bool cmd_pasv(ClientConnection *conn, const char *arg) {
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  getsockname(conn->data_socket, (struct sockaddr *)&addr, &len);
  int data_port = ntohs(addr.sin_port);

  int ip[4];
  sscanf(server_ip, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);

  send_response(conn->control_socket, MSG_ENTER_PASV, ip[0], ip[1], ip[2],
                ip[3], data_port / 256, data_port % 256);

  return false;
}

bool cmd_nlst(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_LIST_START);
  int data_conn = accept(conn->data_socket, NULL, NULL);
  if (data_conn < 0) {
    perror(ERR_ACCEPT_FAIL);
    send_response(conn->control_socket, MSG_DATA_CONN_FAIL);
  } else {
    list_directory(data_conn, conn->current_dir);
    close(data_conn);
    send_response(conn->control_socket, MSG_RETR_END);
  }
  return false;
}

bool cmd_dir(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_LIST_START);
  int data_conn = accept(conn->data_socket, NULL, NULL);
  if (data_conn < 0) {
    perror(ERR_ACCEPT_FAIL);
    send_response(conn->control_socket, MSG_DATA_CONN_FAIL);
  } else {
    list_directory_extend(data_conn, conn->current_dir);
    close(data_conn);
    send_response(conn->control_socket, MSG_RETR_END);
  }
  return false;
}

bool cmd_retr(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_STOR_START);
  int data_conn = accept(conn->data_socket, NULL, NULL);
  if (data_conn < 0) {
    perror(ERR_ACCEPT_FAIL);
    send_response(conn->control_socket, MSG_DATA_CONN_FAIL);
  } else {
    char full_path[MAX_PATH];
    snprintf(full_path, MAX_PATH, "%s/%s", conn->current_dir, arg);
    send_file(data_conn, full_path);
    close(data_conn);
    send_response(conn->control_socket, MSG_RETR_END);
  }
  return false;
}

bool cmd_mretr(ClientConnection *conn, const char *arg) {
  char *token;
  char *saveptr;
  char args_copy[MAX_PATH];

  strncpy(args_copy, arg, MAX_PATH - 1);
  args_copy[MAX_PATH - 1] = '\0';

  token = strtok_r(args_copy, " ", &saveptr);

  while (token != NULL) {
    send_response(conn->control_socket, MSG_STOR_START);
    int data_conn = accept(conn->data_socket, NULL, NULL);
    if (data_conn < 0) {
      perror(ERR_ACCEPT_FAIL);
      send_response(conn->control_socket, MSG_DATA_CONN_FAIL);
    } else {
      char full_path[MAX_PATH];
      snprintf(full_path, MAX_PATH, "%s/%s", conn->current_dir, token);
      send_file(data_conn, full_path);
      close(data_conn);
      send_response(conn->control_socket, MSG_RETR_END);
    }

    token = strtok_r(NULL, " ", &saveptr);
  }

  return false;
}

bool cmd_stor(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_STOR_START);
  int data_conn = accept(conn->data_socket, NULL, NULL);
  if (data_conn < 0) {
    perror(ERR_ACCEPT_FAIL);
    send_response(conn->control_socket, MSG_DATA_CONN_FAIL);
  } else {
    char full_path[MAX_PATH];
    snprintf(full_path, MAX_PATH, "%s/%s", conn->current_dir, arg);
    receive_file(data_conn, full_path);
    close(data_conn);
    send_response(conn->control_socket, MSG_STOR_END);
  }
  return false;
}

bool cmd_quit(ClientConnection *conn, const char *arg) {
  send_response(conn->control_socket, MSG_GOODBYE);
  return true; // Signal that we should close the connection
}

bool handle_command(ClientConnection *conn, char *buffer) {
  char *command = strtok(buffer, " \r\n");
  char *arg = strtok(NULL, "\r\n");

  if (command == NULL) {
    send_response(conn->control_socket, MSG_SYNTAX_ERROR);
    return false;
  }

  for (FtpCommand *cmd = ftp_commands; cmd->command != NULL; cmd++) {
    if (strcasecmp(command, cmd->command) == 0) {
      return cmd->handler(conn, arg);
    }
  }

  send_response(conn->control_socket, MSG_NOT_IMPLEMENTED);
  return false;
}

void get_local_ip() {
  struct ifaddrs *ifaddr, *ifa;
  int family, s;
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    exit(EXIT_FAILURE);
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;

    family = ifa->ifa_addr->sa_family;

    if (family == AF_INET) {
      s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host,
                      NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      if (s != 0) {
        printf("getnameinfo() failed: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
      }

      if (strcmp(ifa->ifa_name, "lo") != 0) {
        strncpy(server_ip, host, 16);
        server_ip[15] = '\0';
        break;
      }
    }
  }

  freeifaddrs(ifaddr);

  if (server_ip[0] == '\0') {
    fprintf(stderr, "Could not find a suitable network interface\n");
    exit(EXIT_FAILURE);
  }
}