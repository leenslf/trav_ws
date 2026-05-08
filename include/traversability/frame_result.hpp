#pragma once
#ifndef TRAVERSABILITY_FRAME_RESULT_HPP
#define TRAVERSABILITY_FRAME_RESULT_HPP

#include "traversability/result.hpp"
#include "traversability/image_payload.hpp"
#include <cstdint>

struct FrameResult {
    TraversabilityResult  traversability;
    bool                  has_image{false};
    ImagePayload          image;
    uint64_t              timestamp_ns{0};
};

#endif // TRAVERSABILITY_FRAME_RESULT_HPP
