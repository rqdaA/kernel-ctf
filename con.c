#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOOPBACK_IP "127.0.0.1"
#define PORT 12345
#define BUFFER_SIZE 1024

void udp_loopback_communication() {
  int sockfd;
  struct sockaddr_in server_addr, client_addr;
  char buffer[BUFFER_SIZE];
  socklen_t addr_len = sizeof(client_addr);

  // ソケットの作成
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  memset(&client_addr, 0, sizeof(client_addr));

  // サーバー情報の設定
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  server_addr.sin_addr.s_addr = inet_addr(LOOPBACK_IP);

  // メッセージの送信
  const char *message = "Hello, UDP Loopback!";
  sendto(sockfd, message, strlen(message), 0,
         (const struct sockaddr *)&server_addr, sizeof(server_addr));
  printf("Message sent: %s\n", message);

  // メッセージの受信
  int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                   (struct sockaddr *)&client_addr, &addr_len);
  if (n < 0) {
    perror("recvfrom failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  buffer[n] = '\0';
  printf("Message received: %s\n", buffer);

  // ソケットの終了
  close(sockfd);
}

int main() {
  udp_loopback_communication();
  return 0;
}
