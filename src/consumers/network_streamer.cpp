#include "traversability/consumers/network_streamer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::size_t serialized_size_bytes(const TraversabilityResult& result)
{
    const std::size_t nr = static_cast<std::size_t>(result.r_bins);
    const std::size_t nt = static_cast<std::size_t>(result.theta_bins);
    const std::size_t grid_bytes = nr * nt * sizeof(float);
    return sizeof(PacketHeader)
        + grid_bytes
        + grid_bytes;
}

bool has_valid_layout(const TraversabilityResult& result)
{
    if (result.r_bins < 0 || result.theta_bins < 0) {
        return false;
    }

    const std::size_t nr = static_cast<std::size_t>(result.r_bins);
    const std::size_t nt = static_cast<std::size_t>(result.theta_bins);
    return result.trav_grid.size() == nr * nt
        && result.height_map.size() == nr * nt;
}

std::size_t serialize(unsigned char* dst,
                      const TraversabilityResult& result,
                      uint64_t timestamp_ns,
                      uint32_t seq)
{
    const std::size_t nr = static_cast<std::size_t>(result.r_bins);
    const std::size_t nt = static_cast<std::size_t>(result.theta_bins);
    const std::size_t grid_bytes = nr * nt * sizeof(float);

    PacketHeader header{};
    header.magic = NetworkStreamer::kPacketMagic;
    header.seq = seq;
    header.timestamp_ns = timestamp_ns;
    header.nr = static_cast<uint32_t>(nr);
    header.nt = static_cast<uint32_t>(nt);

    unsigned char* write_ptr = dst;
    std::memcpy(write_ptr, &header, sizeof(header));
    write_ptr += sizeof(header);

    if (grid_bytes != 0u) {
        std::memcpy(write_ptr, result.trav_grid.data(), grid_bytes);
        write_ptr += grid_bytes;

        std::memcpy(write_ptr, result.height_map.data(), grid_bytes);
        write_ptr += grid_bytes;
    }

    return static_cast<std::size_t>(write_ptr - dst);
}

} // namespace

NetworkStreamer::NetworkStreamer()
{
    // Initialize a non-blocking UDP socket and preconfigure the loopback destination.
    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0) {
        std::perror("NetworkStreamer socket");
        return;
    }

    // Reserve enough space for the largest legal UDP payload.
    send_buf_.resize(kMaxUdpPayloadBytes);

    // Send packets over IPv4 loopback to the fixed local port.
    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(kPort);
    destination_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

NetworkStreamer::~NetworkStreamer()
{
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
    }
}

void NetworkStreamer::consume(const FrameResult& frame, uint64_t timestamp_ns)
{
    if (socket_fd_ < 0) {
        return;
    }

    const TraversabilityResult& result = frame.traversability;

    if (!has_valid_layout(result)) {
        errno = EINVAL;
        std::perror("NetworkStreamer result layout");
        return;
    }

    const std::size_t packet_size = serialized_size_bytes(result);

    if (packet_size > send_buf_.size()) {
        errno = EMSGSIZE;
        std::perror("NetworkStreamer packet too large");
        return;
    }

    // Pack the validated result into send_buf_ before transmission.
    const std::size_t written = serialize(send_buf_.data(), result, timestamp_ns, seq_);
    if (written != packet_size) {
        errno = EFAULT;
        std::perror("NetworkStreamer serialization");
        return;
    }

    const ssize_t sent = ::sendto(socket_fd_,
                                  send_buf_.data(),
                                  packet_size,
                                  0,
                                  reinterpret_cast<const sockaddr*>(&destination_),
                                  destination_len_);
    if (sent < 0) {
        std::perror("NetworkStreamer sendto");
        return;
    }
    if (static_cast<std::size_t>(sent) != packet_size) {
        errno = EIO;
        std::perror("NetworkStreamer short send");
        return;
    }

    ++seq_;
}
