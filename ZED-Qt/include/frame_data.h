#pragma once

#include <cstdint>
#include <QVector>
#include <QMetaType>

namespace protocol {

constexpr uint32_t MAGIC = 0x54524156;
constexpr int HEADER_SIZE = 24;

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint32_t seq;
    uint64_t timestamp_ns;
    uint32_t nr;
    uint32_t nt;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 24, "Header must be exactly 24 bytes");

} // namespace protocol

struct FrameData {
    uint32_t seq; // probably not useful while using commmanager
    uint64_t timestamp_ns;
    int nr;
    int nt;
    QVector<float> trav_grid;
    QVector<float> height_map;
    QVector<float> r_edges;
    QVector<float> theta_edges;
};

Q_DECLARE_METATYPE(FrameData)
