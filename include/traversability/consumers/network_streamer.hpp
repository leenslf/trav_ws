#pragma once

#include "traversability/result_consumer.hpp"

#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>
#include <vector>

struct PacketHeader {
    uint32_t magic;
    uint32_t seq;
    uint64_t timestamp_ns;
    uint32_t nr;
    uint32_t nt;
};

static_assert(sizeof(PacketHeader) == 24, "PacketHeader must be 24 bytes");

class NetworkStreamer : public IResultConsumer {
public:
    static constexpr uint32_t kPacketMagic = 0x54524156u;
    static constexpr std::size_t kMaxUdpPayloadBytes = 65507u;
    static constexpr uint16_t kPort = 5005;

    NetworkStreamer();
    ~NetworkStreamer() override;

    NetworkStreamer(const NetworkStreamer&) = delete;
    NetworkStreamer& operator=(const NetworkStreamer&) = delete;
    NetworkStreamer(NetworkStreamer&&) = delete;
    NetworkStreamer& operator=(NetworkStreamer&&) = delete;

    void consume(const TraversabilityResult& result, uint64_t timestamp_ns) override;

private:
    sockaddr_in destination_{};
    socklen_t destination_len_{sizeof(sockaddr_in)};
    std::vector<unsigned char> send_buf_;
    uint32_t seq_{0};
    int socket_fd_{-1};
};
