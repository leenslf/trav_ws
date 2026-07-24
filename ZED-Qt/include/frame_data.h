#pragma once

#include <cstdint>
#include <QVector>
#include <QMetaType>


struct FrameData {
    uint32_t seq;
    uint64_t timestamp_ns;
    int nr;
    int nt;
    QVector<float> trav_grid;
    // camera pose — populated from FrameBundle; zero-initialised until first bundle arrives
    float   tx{0}, ty{0}, tz{0};
    float   qx{0}, qy{0}, qz{0}, qw{1};
    uint8_t tracking_state{4};  // 4 = UNAVAILABLE
};

Q_DECLARE_METATYPE(FrameData)

