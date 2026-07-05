// micro-redis — rung 0

#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  // 1. Create the server socket (IPv4, TCP, default protocol)
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    std::cerr << "Failed to create socket\n";
    return 1;
  }

  const uint16_t port = 6380;

  // 2. Bind the socket to an IP and Port
  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  int opt = 1; // 1 to enable, 0 to disable
  // Configure the socket to reuse the local address/port immediately
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno)
              << "\n";
    close(server_fd);
    return 1;
  }

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "Bind failed, errcode: " << errno << " ("
              << std::strerror(errno) << ")\n";
    close(server_fd);
    return 1;
  }

  // 3. Listen for connections
  if (listen(server_fd, 2) < 0) {
    std::cerr << "Listen failed, errcode: " << errno << " ("
              << std::strerror(errno) << ")\n";
    close(server_fd);
    return 1;
  }

  std::string msg = std::format("Server listening on port {}...\n", port);
  std::cout << msg;

  // 4. Accept an incoming client connection
  sockaddr_in client_addr{};
  socklen_t client_len = sizeof(client_addr);
  int client_fd =
      accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
  if (client_fd < 0) {
    std::cerr << "Accept failed, errcode: " << errno << " ("
              << std::strerror(errno) << ")\n";
    close(server_fd);
    return 1;
  }
  std::cout << "Client connected!\n";

  // 5. Read data sent by the client
  char buffer[1024] = {0};
  ssize_t bytes_read;
  while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytes_read] = '\0';

    std::cout << "Received from client: " << buffer << "\n";

    // 6. echo response back to the client
    send(client_fd, buffer, bytes_read, MSG_NOSIGNAL);
  }

  // 7. Cleanup and close the file descriptors
  close(client_fd);
  close(server_fd);

  return 0;
}
