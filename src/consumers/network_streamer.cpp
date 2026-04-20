#include "traversability/consumers/network_streamer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <unordered_map>
#include <vector>

namespace {

constexpr uint32_t kPacketMagic = 0x54524156u;
constexpr std::size_t kMaxUdpPayloadBytes = 65507u;

struct NetworkStreamerState {
    sockaddr_in destination{};
    socklen_t destination_len{sizeof(destination)};
    std::vector<unsigned char> send_buf;
    uint32_t seq{0};
};

std::mutex g_state_mutex;
std::unordered_map<const NetworkStreamer*, NetworkStreamerState> g_states;

std::size_t serialized_size_bytes(const TraversabilityResult& result)
{
    const std::size_t nr = static_cast<std::size_t>(result.r_bins);
    const std::size_t nt = static_cast<std::size_t>(result.theta_bins);
    const std::size_t grid_bytes = nr * nt * sizeof(float);
    return sizeof(PacketHeader)
        + grid_bytes
        + grid_bytes
        + (nr + 1u) * sizeof(float)
        + (nt + 1u) * sizeof(float);
}

bool has_valid_layout(const TraversabilityResult& result)
{
    if (result.r_bins < 0 || result.theta_bins < 0) {
        return false;
    }

    const std::size_t nr = static_cast<std::size_t>(result.r_bins);
    const std::size_t nt = static_cast<std::size_t>(result.theta_bins);
    return result.trav_grid.size() == nr * nt
        && result.height_map.size() == nr * nt
        && result.r_edges.size() == nr + 1u
        && result.theta_edges.size() == nt + 1u;
}

std::size_t serialize(unsigned char* dst,
                      const TraversabilityResult& result,
                      uint64_t timestamp_ns,
                      uint32_t seq)
{
    const std::size_t nr = static_cast<std::size_t>(result.r_bins);
    const std::size_t nt = static_cast<std::size_t>(result.theta_bins);
    const std::size_t grid_bytes = nr * nt * sizeof(float);
    const std::size_t r_edges_bytes = (nr + 1u) * sizeof(float);
    const std::size_t theta_edges_bytes = (nt + 1u) * sizeof(float);

    PacketHeader header{};
    header.magic = kPacketMagic;
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

    if (r_edges_bytes != 0u) {
        std::memcpy(write_ptr, result.r_edges.data(), r_edges_bytes);
        write_ptr += r_edges_bytes;
    }

    if (theta_edges_bytes != 0u) {
        std::memcpy(write_ptr, result.theta_edges.data(), theta_edges_bytes);
        write_ptr += theta_edges_bytes;
    }

    return static_cast<std::size_t>(write_ptr - dst);
}

} // namespace

NetworkStreamer::NetworkStreamer(const NetworkConfig& cfg)
{
    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0) {
        std::perror("NetworkStreamer socket");
        return;
    }

    if (cfg.port <= 0 || cfg.port > 65535) {
        errno = EINVAL;
        std::perror("NetworkStreamer port");
        ::close(socket_fd_);
        socket_fd_ = -1;
        return;
    }

    NetworkStreamerState state;
    state.send_buf.resize(kMaxUdpPayloadBytes);
    state.destination.sin_family = AF_INET;
    state.destination.sin_port = htons(static_cast<uint16_t>(cfg.port));
    state.destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_states.emplace(this, std::move(state));
}

NetworkStreamer::~NetworkStreamer()
{
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_states.erase(this);
    }

    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
    }
}

void NetworkStreamer::consume(const TraversabilityResult& result, uint64_t timestamp_ns)
{
    if (socket_fd_ < 0) {
        return;
    }

    if (!has_valid_layout(result)) {
        errno = EINVAL;
        std::perror("NetworkStreamer result layout");
        return;
    }

    const std::size_t packet_size = serialized_size_bytes(result);

    std::lock_guard<std::mutex> lock(g_state_mutex);
    auto it = g_states.find(this);
    if (it == g_states.end()) {
        errno = ENOENT;
        std::perror("NetworkStreamer state");
        return;
    }

    auto& state = it->second;
    if (packet_size > state.send_buf.size()) {
        errno = EMSGSIZE;
        std::perror("NetworkStreamer packet too large");
        return;
    }

    const std::size_t written = serialize(state.send_buf.data(), result, timestamp_ns, state.seq);
    if (written != packet_size) {
        errno = EFAULT;
        std::perror("NetworkStreamer serialization");
        return;
    }

    const ssize_t sent = ::sendto(socket_fd_,
                                  state.send_buf.data(),
                                  packet_size,
                                  0,
                                  reinterpret_cast<const sockaddr*>(&state.destination),
                                  state.destination_len);
    if (sent < 0) {
        std::perror("NetworkStreamer sendto");
        return;
    }
    if (static_cast<std::size_t>(sent) != packet_size) {
        errno = EIO;
        std::perror("NetworkStreamer short send");
        return;
    }

    ++state.seq;
}
