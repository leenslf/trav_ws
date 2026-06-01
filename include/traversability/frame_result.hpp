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
    std::vector<float> trav_grid;   // row-major, rows=r_bins, cols=theta_bins
    std::vector<float> height_map;  // same layout
    std::vector<float> r_edges;     // size = r_bins + 1
    std::vector<float> theta_edges; // size = theta_bins + 1
    int r_bins{0};
    int theta_bins{0};
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
