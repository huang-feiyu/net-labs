#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "base64_utils.h"

#define MAX_SIZE 4095

#define swap16(x) ((((x)&0xFF) << 8) | (((x) >> 8) & 0xFF))

char buf[MAX_SIZE + 1];

int send_wrapper(int sockfd, void *buf, int len, int flags, char *error_msg) {
  int ret = -1;
  if ((ret = send(sockfd, buf, len, flags)) == -1) {
    perror(error_msg);
    exit(EXIT_FAILURE);
  }
  printf("\033[1;32m%s\033[0m", (char *)buf);
  return ret;
}

int recv_wrapper(int sockfd, void *buf, int len, int flags, char *error_msg) {
  int r_size = -1;
  if ((r_size = recv(sockfd, buf, len, 0)) == -1) {
    perror(error_msg);
    exit(EXIT_FAILURE);
  }
  char *tmpbuf = (char *)buf;
  tmpbuf[r_size] = '\0';
  printf("%s", tmpbuf);
  return r_size;
}

char *file2str(const char *path) {
  FILE *fp = fopen(path, "r");
  fseek(fp, 0, SEEK_END);
  int fplen = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *content = (char *)malloc(fplen);
  fread(content, 1, fplen, fp);
  fclose(fp);
  return content;
}

// receiver: mail address of the recipient
// subject: mail subject
// msg: content of mail body or path to the file containing mail body
// att_path: path to the attachment
void send_mail(const char *receiver, const char *subject, const char *msg, const char *att_path) {
  const char *end_msg = "\r\n.\r\n";
  const char *host_name = "smtp.qq.com";   // TODO: Specify the mail server domain name
  const unsigned short port = 25;          // SMTP server port
  const char *user = "xxx@qq.com";  // TODO: Specify the user
  const char *pass = "xxx";   // TODO: Specify the password
  const char *from = "xxx@qq.com";  // TODO: Specify the mail address of the sender
  char dest_ip[16];                        // Mail server IP address
  int s_fd;                                // socket file descriptor
  struct hostent *host;
  struct in_addr **addr_list;
  int i = 0;
  int r_size;
  // Get IP from domain name
  if ((host = gethostbyname(host_name)) == NULL) {
    herror("gethostbyname");
    exit(EXIT_FAILURE);
  }

  addr_list = (struct in_addr **)host->h_addr_list;
  while (addr_list[i] != NULL)
    ++i;
  strcpy(dest_ip, inet_ntoa(*addr_list[i - 1]));

  // Create a socket, return the file descriptor to s_fd, and establish a TCP connection to the mail server
  if ((s_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  struct sockaddr_in *servaddr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
  servaddr->sin_family = AF_INET;
  servaddr->sin_port = swap16(port);
  servaddr->sin_addr = (struct in_addr){inet_addr(dest_ip)};
  bzero(servaddr->sin_zero, 8);
  connect(s_fd, (struct sockaddr *)servaddr, sizeof(struct sockaddr_in));

  // Print welcome message
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv");

  // Send EHLO command and print server response
  const char *EHLO = "EHLO huang\r\n";
  send_wrapper(s_fd, (void *)EHLO, strlen(EHLO), 0, "send EHLO");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv EHLO");

  // Authentication. Server response should be printed out.
  const char *AUTH = "AUTH login\r\n";
  send_wrapper(s_fd, (void *)AUTH, strlen(AUTH), 0, "send AUTH");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv username");

  char *user64 = encode_str(user);
  strcat(user64, "\r\n");
  send_wrapper(s_fd, (void *)user64, strlen(user64), 0, "send username");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv password");
  free(user64);

  char *pass64 = encode_str(pass);
  strcat(pass64, "\r\n");
  send_wrapper(s_fd, (void *)pass64, strlen(pass64), 0, "send password");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv AUTH");
  free(pass64);

  // Send MAIL FROM command and print server response
  sprintf(buf, "MAIL FROM:<%s>\r\n", from);
  send_wrapper(s_fd, (void *)buf, strlen(buf), 0, "send MAIL FROM");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv MAIL FROM");

  // Send RCPT TO command and print server response
  sprintf(buf, "RCPT TO:<%s>\r\n", receiver);
  send_wrapper(s_fd, (void *)buf, strlen(buf), 0, "send RCPT TO");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv RCPT TO");

  // Send DATA command and print server response
  send_wrapper(s_fd, "DATA\r\n", 6, 0, "send DATA");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv DATA");

  // Send message data
  sprintf(buf, "From: %s\r\nTo: %s\r\nContent-Type: multipart/mixed; boundary=ludwighuang\r\n", from, receiver);
  if (subject != NULL) strcat(buf, "Subject: "), strcat(buf, subject), strcat(buf, "\r\n\r\n");
  send_wrapper(s_fd, (void *)buf, strlen(buf), 0, "send DATA header");

  if (msg != NULL) {
    sprintf(buf, "--ludwighuang\r\nContent-Type:text/plain\r\n\r\n");
    send_wrapper(s_fd, (void *)buf, strlen(buf), 0, "send DATA msg");
    if (access(msg, F_OK) == 0) {
      char *content = file2str(msg);
      send_wrapper(s_fd, (void *)content, strlen(content), 0, "send DATA msg content");
      free(content);
    } else
      send_wrapper(s_fd, (void *)msg, strlen(msg), 0, "send DATA msg content");
    send_wrapper(s_fd, "\r\n", 2, 0, "");
  }

  if (att_path != NULL) {
    sprintf(buf, "--ludwighuang\r\nContent-Type:application/octet-stream\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; name=%s\r\n\r\n", att_path);
    send_wrapper(s_fd, (void *)buf, strlen(buf), 0, "send DATA attach");
    FILE *fp = fopen(att_path, "r");
    if (fp == NULL) {
      perror("file not exist");
      exit(EXIT_FAILURE);
    }
    FILE *fp64 = fopen("tmp.attach", "w");
    encode_file(fp, fp64);
    fclose(fp);
    fclose(fp64);
    char *attach = file2str("tmp.attach");
    send_wrapper(s_fd, (void *)attach, strlen(attach), 0, "send DATA attachment files");
    free(attach);
  }
  sprintf(buf, "--ludwighuang\r\n");
  send_wrapper(s_fd, (void *)buf, strlen(buf), 0, "send DATA last");

  // Message ends with a single period
  send_wrapper(s_fd, (void *)end_msg, strlen(end_msg), 0, "send .");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv .");

  // Send QUIT command and print server response
  send_wrapper(s_fd, "QUIT\r\n", 6, 0, "send QUIT");
  recv_wrapper(s_fd, (void *)buf, MAX_SIZE, 0, "recv QUIT");

  close(s_fd);
}

int main(int argc, char *argv[]) {
  int opt;
  char *s_arg = NULL;
  char *m_arg = NULL;
  char *a_arg = NULL;
  char *recipient = NULL;
  const char *optstring = ":s:m:a:";
  while ((opt = getopt(argc, argv, optstring)) != -1) {
    switch (opt) {
      case 's':
        s_arg = optarg;
        break;
      case 'm':
        m_arg = optarg;
        break;
      case 'a':
        a_arg = optarg;
        break;
      case ':':
        fprintf(stderr, "Option %c needs an argument.\n", optopt);
        exit(EXIT_FAILURE);
      case '?':
        fprintf(stderr, "Unknown option: %c.\n", optopt);
        exit(EXIT_FAILURE);
      default:
        fprintf(stderr, "Unknown error.\n");
        exit(EXIT_FAILURE);
    }
  }

  if (optind == argc) {
    fprintf(stderr, "Recipient not specified.\n");
    exit(EXIT_FAILURE);
  } else if (optind < argc - 1) {
    fprintf(stderr, "Too many arguments.\n");
    exit(EXIT_FAILURE);
  } else {
    recipient = argv[optind];
    send_mail(recipient, s_arg, m_arg, a_arg);
    exit(0);
  }
}