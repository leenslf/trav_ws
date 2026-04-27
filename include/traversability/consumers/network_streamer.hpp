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
    // Packet signature: in ASCII, those bytes are 0x54 0x52 0x41 0x56, which spells "TRAV", maybe be overengineered
    static constexpr uint32_t kPacketMagic = 0x54524156u;
    // The largest UDP payload that fits in a normal IPv4 packet.
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
    // IPv4 socket address struct that sendto() will use as the packet destination.
    sockaddr_in destination_{};
    socklen_t destination_len_{sizeof(sockaddr_in)};
    std::vector<unsigned char> send_buf_;
    uint32_t seq_{0};
    int socket_fd_{-1};
};
