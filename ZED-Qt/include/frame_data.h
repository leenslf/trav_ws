#pragma once

#include <cstdint>
#include <QVector>
#include <QMetaType>


struct FrameData {
    uint32_t seq;
    uint64_t timestamp_ns;
    int nr;
    int nt;               // legacy: widest row's bin count — see row_theta_bins
    // Ragged grid layout (per-radius zoned angular binning): row r has
    // row_theta_bins[r] valid columns starting at row_offset[r] in
    // trav_grid/height_map — NOT a uniform nr x nt grid. Mirrors
    // TraversabilityResult::row_offset/row_theta_bins on the sender side.
    QVector<int>   row_offset;
    QVector<int>   row_theta_bins;
    QVector<float> trav_grid;
    QVector<float> height_map;
    // camera pose — populated from FrameBundle; zero-initialised until first bundle arrives
    float   tx{0}, ty{0}, tz{0};
    float   qx{0}, qy{0}, qz{0}, qw{1};
    uint8_t tracking_state{4};  // 4 = UNAVAILABLE
};

Q_DECLARE_METATYPE(FrameData)

