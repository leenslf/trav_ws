#pragma once
#ifndef TRAVERSABILITY_PIPELINE_STAGE_HPP
#define TRAVERSABILITY_PIPELINE_STAGE_HPP

#include <string_view>
#include <cuda_runtime.h>
#include "traversability/config.hpp"
#include "traversability/frame_data.hpp"

class IPipelineStage {
public:
    virtual ~IPipelineStage() = default;
    virtual void init(const PipelineConfig& cfg, FrameData& frame) = 0;
    virtual void process(FrameData& frame, cudaStream_t stream) = 0;
    virtual std::string_view name() const noexcept = 0;
};

#endif // TRAVERSABILITY_PIPELINE_STAGE_HPP
