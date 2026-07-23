#pragma once
#ifndef TRAVERSABILITY_FRAME_RESULT_HPP
#define TRAVERSABILITY_FRAME_RESULT_HPP

#include <cstdint>
#include <vector>

enum class TrackingState {
    OK,
    SEARCHING,
    FPS_TOO_LOW,
    SEARCHING_FLOOR_PLANE,
    UNAVAILABLE,
    LOOP_CLOSED,
};

struct CameraPose {
    float tx{0}, ty{0}, tz{0};         // translation in metres
    float qx{0}, qy{0}, qz{0}, qw{1}; // orientation quaternion
};

struct TraversabilityResult {
    // Zone-based angular binning (see PolarZone / compute_polar_zones in
    // traversability/stages/traversability.hpp) means each radial row can
    // have a different angular bin count, so trav_grid/height_map are NOT a
    // uniform r_bins x theta_bins row-major grid — they're a ragged grid:
    // r_bins dense rows of varying width, packed back-to-back. Decode a
    // cell at (r, t) as trav_grid[row_offset[r] + t], valid only for
    // 0 <= t < row_theta_bins[r].
    //
    // theta_bins is a LEGACY field kept for consumers that still assume a
    // single uniform stride (`r * theta_bins + t`) — it's set to the widest
    // row's bin count, but using it as a stride for every row is WRONG for
    // any row narrower than that. comm_sender now decodes the ragged layout
    // correctly (per-row width travels with it in FrameBundle::row_theta_bins,
    // consumed by ZED-Qt's comm_receiver/polar_grid_widget). disk_write_consumer
    // and network_streamer have not been updated yet: network_streamer already
    // guarded its size check before this change; disk_write_consumer was given
    // the same guard (result.trav_grid.size() == r_bins*theta_bins) so a zoned
    // grid makes it skip the frame instead of indexing past the end of
    // trav_grid. Use row_offset/row_theta_bins to decode a zoned grid correctly.
    std::vector<float> trav_grid;
    // are we still sending this
    std::vector<float> height_map;   // same layout as trav_grid
    int r_bins{0};
    int theta_bins{0};               // legacy — see comment above
    std::vector<int> row_offset;     // size r_bins: flat start index of row r
    std::vector<int> row_theta_bins; // size r_bins: valid column count of row r
};

struct ImagePayload {
    bool                  valid{false};
    int                   width{0};
    int                   height{0};
    std::vector<uint8_t>  jpeg_bytes;
};

struct FrameResult {
    TraversabilityResult  traversability;
    bool                  has_image{false};
    ImagePayload          image;
    uint64_t              timestamp_ns{0};
    CameraPose            camera_pose;
    TrackingState         tracking_state{TrackingState::UNAVAILABLE};
};

#endif // TRAVERSABILITY_FRAME_RESULT_HPP
