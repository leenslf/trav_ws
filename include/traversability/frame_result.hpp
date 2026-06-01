#pragma once
#ifndef TRAVERSABILITY_FRAME_RESULT_HPP
#define TRAVERSABILITY_FRAME_RESULT_HPP

#include "traversability/pose.hpp"
#include "traversability/result.hpp"
#include "traversability/image_payload.hpp"
#include <cstdint>

struct FrameResult {
    TraversabilityResult  traversability;
    bool                  has_image{false};
    ImagePayload          image;
    uint64_t              timestamp_ns{0};
    CameraPose            camera_pose;
    TrackingState         tracking_state{TrackingState::UNAVAILABLE};
};

#endif // TRAVERSABILITY_FRAME_RESULT_HPP
