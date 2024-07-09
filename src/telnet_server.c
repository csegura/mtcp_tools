#include <arpa/inet.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

int server_fd;

// Function to handle each client
void *handle_client(void *client_socket) {
  int client_fd = *((int *)client_socket);
  free(client_socket);

  char buffer[BUFFER_SIZE];

  ssize_t num_bytes_read;
  int master_fd, slave_fd;
  pid_t pid;

  // Create a pseudo-terminal
  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == -1) {
    perror("openpty");
    close(client_fd);
    pthread_exit(NULL);
  }

  // Fork a child process
  pid = fork();
  if (pid == -1) {
    perror("fork");
    close(client_fd);
    close(master_fd);
    close(slave_fd);
    pthread_exit(NULL);
  }

  if (pid == 0) { // Child process
    // Close the master side of the PTY
    close(master_fd);

    // Redirect stdin, stdout, and stderr to the slave side of the PTY
    login_tty(slave_fd);

    // execute the shell
    execlp("/bin/sh", "-c", "./shell.sh", NULL);
    perror("execle");
    exit(EXIT_FAILURE);
  } else {
    // Parent process
    // Close the slave side of the PTY
    close(slave_fd);

    // relay data between the client and the shell
    fd_set fds;
    while (1) {
      FD_ZERO(&fds);
      FD_SET(client_fd, &fds);
      FD_SET(master_fd, &fds);
      int max_fd = (client_fd > master_fd) ? client_fd : master_fd;

      if (select(max_fd + 1, &fds, NULL, NULL, NULL) == -1) {
        perror("select");
        break;
      }

      // receive data from the client
      if (FD_ISSET(client_fd, &fds)) {
        num_bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (num_bytes_read <= 0) {
          break;
        }
        buffer[num_bytes_read] = '\0';
        write(master_fd, buffer, num_bytes_read);
      }
      // send data to the client
      if (FD_ISSET(master_fd, &fds)) {
        num_bytes_read = read(master_fd, buffer, sizeof(buffer));
        if (num_bytes_read <= 0) {
          break;
        }
        write(client_fd, buffer, num_bytes_read);
      }
      fflush(stdout);
    }

    // wait for the child process to terminate
    waitpid(pid, NULL, 0);

    // client ip address
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    printf("Client disconnected from %s\n", inet_ntoa(client_addr.sin_addr));

    // close the client and master PTY file descriptors
    close(client_fd);
    close(master_fd);
  }

  pthread_exit(NULL);
}

void handle_sigint(int sig) {
  printf("Shutting down the server...\n");
  close(server_fd);
  exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  int port = 12345;

  // parse command line arguments to get the port if any
  if (argc == 2) {
    port = atoi(argv[1]);
  }

  struct sockaddr_in server_addr, client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  signal(SIGINT, handle_sigint); // Handle SIGINT for graceful shutdown

  // create a socket
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // set up the server address struct
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  // SO_REUSEADDR allows the server to bind to an address that is in a TIME_WAIT
  // state
  int enable = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) ==
      -1) {
    perror("setsockopt");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  // bind the socket to the specified port
  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    perror("bind");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  // listen for incoming connections
  if (listen(server_fd, MAX_CLIENTS) == -1) {
    perror("listen");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("telnet_server running..\n");
  printf("Server is listening on port %d\n", port);

  // accept and handle clients
  while (1) {
    int *client_fd = malloc(sizeof(int));
    if (client_fd == NULL) {
      perror("malloc");
      close(server_fd);
      exit(EXIT_FAILURE);
    }

    *client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (*client_fd == -1) {
      perror("accept");
      free(client_fd);
      continue;
    }

    printf("Client connected from %s\n", inet_ntoa(client_addr.sin_addr));

    pthread_t thread;
    if (pthread_create(&thread, NULL, handle_client, client_fd) != 0) {
      perror("pthread_create");
      free(client_fd);
      continue;
    }

    // detach the thread to handle client independently
    pthread_detach(thread);
  }

  // close the server socket (unreachable code, server exits on SIGINT)
  close(server_fd);

  return 0;
}
