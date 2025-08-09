#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <cstddef> // For size_t

// Initializes a UDP socket and binds it to the specified port.
// Returns the socket file descriptor, or -1 on error.
int network_init(int port);

// Sends data to the specified address and port.
// Returns the number of bytes sent, or -1 on error.
ssize_t network_send(int sockfd, const std::string& ip_address, int port, const char* data, size_t data_size);

// Receives data from the socket.
// Returns the number of bytes received, or -1 on error.
// The sender's address and port are filled in the provided arguments.
ssize_t network_receive(int sockfd, char* buffer, size_t buffer_size, std::string& from_ip, int& from_port);

// Closes the socket.
void network_close(int sockfd);

#endif // NETWORK_H
