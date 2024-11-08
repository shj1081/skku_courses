#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAXLINE 80

typedef enum { SendFileName, Ack, SendFileData, EndOfFile } Operation;

// These pragmas are used to not pad the struct
#pragma pack(push, 1)
typedef struct {
  uint32_t operation;
  char data[MAXLINE];
  uint32_t size;
} Data;
#pragma pack(pop)

// Preparing data to be sent over the network
Data serialize_data(Data* data);
// Preparing data to be used in the program
Data deserialize_data(Data* data);

void handle_exit();
void handle_sigint(int sig);
void setup_signal_handler();

int conn_fd = -1;

void setup_conn_fd(char* host, in_port_t port) {
  struct hostent* host_entry;
  struct sockaddr_in sock_addr;

  /* Create socket */
  if ((conn_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    fprintf(stderr, "socket() failed.\n");
    exit(1);
  }

  /* Get host information */

  if ((host_entry = gethostbyname(host)) == NULL) {
    fprintf(stderr, "invalid hostname %s\n", host);
    exit(1);
  }


  /* Initialize sock_addr */

  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  memcpy(&sock_addr.sin_addr.s_addr, host_entry->h_addr_list[0],
         host_entry->h_length);
  sock_addr.sin_port = htons(port);

  /* Connect to host */
  if (connect(conn_fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) < 0) {
    fprintf(stderr, "connect() failed.\n");
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  atexit(handle_exit);
  setup_signal_handler();

  if (argc < 3) {
    fprintf(
        stderr,
        "Received %d arguments. Please enter host address and port number!\n",
        argc - 1);
    exit(1);
  }

  in_port_t port = (in_port_t)strtol(argv[2], NULL, 10);
  if (errno == ERANGE) {
    fprintf(stderr, "invalid port number %s\n", argv[2]);
    exit(1);
  }

  setup_conn_fd(argv[1], port);

  while (1) {
    Data file_name_data;

    char* file_name = readline("File Name: ");
    add_history(file_name);
    int fd = open(file_name, O_RDONLY);

    if (file_name == NULL || strcmp(file_name, "exit") == 0) {
      break;
    }

    if (fd < 0) {
      fprintf(stderr, "Cannot open '%s' due to `%s`! Please try again!\n",
              file_name, strerror(errno));
      continue;
    }

    /* Initialize file_name_data */
    file_name_data.operation = SendFileName;
    file_name_data.size = strlen(file_name);
    memcpy(file_name_data.data, file_name, strlen(file_name));
    // add null terminator
    file_name_data.data[strlen(file_name)] = '\0';

    Data serialized_file_name_data = serialize_data(&file_name_data); // Serialize file_name_data to send over the network

    /* Send filename to host */
    write(conn_fd, &serialized_file_name_data, sizeof(Data));

    /* Receive ack from host */


    // We assume ack_data's operation is always Ack, so no need to serialize,
    // but in real-world applications, you have to double-check this...
    Data ack_data;
    read(conn_fd, &ack_data, sizeof(Data));

    /* Read the file and send data to host */
    size_t total_chars_sent = 0;

    Data file_data;
    file_data.operation = SendFileData; // Set operation to SendFileData
    ssize_t n_read;

    while (1) {
      // Break if n_read == 0 and exit if n_read < 0
      n_read = read(fd, file_data.data, MAXLINE);

      if (n_read == 0) {
        break;
      } else if (n_read < 0) {
        fprintf(stderr, "Cannot read from '%s' due to `%s`!\n", file_name,
                strerror(errno));
        exit(1);
      }
      
      file_data.size = n_read;
      Data serialized_file_data = serialize_data(&file_data);

      /* Send data to host */
      send(conn_fd, &serialized_file_data, sizeof(Data), 0);
    
      total_chars_sent += file_data.size;
    }

    Data eof_data = {.operation = EndOfFile, .size = 0};
    Data serialized_eof_data = serialize_data(&eof_data);

    /* Send EOF */
    send(conn_fd, &serialized_eof_data, sizeof(Data), 0);

    /* Print number of bytes sent */
    printf("Sent %ld bytes.\n", total_chars_sent);
  }

  close(conn_fd);
  conn_fd = -1;

  return 0;
}

/* Helper functions */

void handle_sigint(int sig) {
  if (sig == SIGINT) {
    exit(0);
  }
}

void handle_exit() {
  if (conn_fd != -1) {
    close(conn_fd);
  }
}

void setup_signal_handler() {
  struct sigaction act;
  act.sa_handler = handle_sigint;
  sigaction(SIGINT, &act, NULL);
}

// Preparing data to be sent over the network
Data serialize_data(Data* data) {
  Data serialized_data;
  serialized_data.operation = htonl((uint32_t)data->operation);
  serialized_data.size = htonl((uint32_t)data->size);
  memcpy(serialized_data.data, data->data, MAXLINE);
  return serialized_data;
}

// Preparing data to be used in the program
Data deserialize_data(Data* data) {
  Data deserialized_data;
  deserialized_data.operation = ntohl(data->operation);
  deserialized_data.size = ntohl(data->size);
  memcpy(deserialized_data.data, data->data, MAXLINE);
  return deserialized_data;
}