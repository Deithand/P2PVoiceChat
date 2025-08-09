#include "network.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int network_init(int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "ERROR opening socket" << std::endl;
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "ERROR on binding" << std::endl;
        close(sockfd);
        return -1;
    }

    return sockfd;
}

ssize_t network_send(int sockfd, const std::string& ip_address, int port, const char* data, size_t data_size) {
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address.c_str(), &dest_addr.sin_addr) <= 0) {
        std::cerr << "ERROR invalid IP address" << std::endl;
        return -1;
    }

    return sendto(sockfd, data, data_size, 0, (const struct sockaddr *) &dest_addr, sizeof(dest_addr));
}

ssize_t network_receive(int sockfd, char* buffer, size_t buffer_size, std::string& from_ip, int& from_port) {
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t n = recvfrom(sockfd, buffer, buffer_size, 0, (struct sockaddr *) &from_addr, &from_len);

    if (n >= 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(from_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        from_ip = ip_str;
        from_port = ntohs(from_addr.sin_port);
    }

    return n;
}

void network_close(int sockfd) {
    close(sockfd);
}
