#include "traversability/result_consumer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

NetworkStreamer::NetworkStreamer(const NetworkConfig& cfg) {
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0) {
        throw std::runtime_error("failed to create network streamer socket");
    }

    int reuse_addr = 1;
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
        ::close(socket_fd_);
        throw std::runtime_error("failed to configure network streamer socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(cfg.port));

    if (::bind(socket_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(socket_fd_);
        throw std::runtime_error("failed to bind network streamer socket");
    }

    if (::listen(socket_fd_, 1) < 0) {
        ::close(socket_fd_);
        throw std::runtime_error("failed to listen on network streamer socket");
    }
}

NetworkStreamer::~NetworkStreamer() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
    }
}

void NetworkStreamer::consume(const TraversabilityResult&, uint64_t) {
    // TODO
}
