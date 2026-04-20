#pragma once

#include "traversability/result_consumer.hpp"

#include <cstdint>

struct PacketHeader {
    uint32_t magic;
    uint32_t seq;
    uint64_t timestamp_ns;
    uint32_t nr;
    uint32_t nt;
};

static_assert(sizeof(PacketHeader) == 24, "PacketHeader must be 24 bytes");
