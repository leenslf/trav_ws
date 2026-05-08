#pragma once

#include <cstdint>
#include <QVector>
#include <QMetaType>


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

