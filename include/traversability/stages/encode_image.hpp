#pragma once
#include "traversability/pipeline_stage.hpp"

class EncodeImageStage : public IPipelineStage {
public:
    void init(const PipelineConfig& cfg, FrameData& frame) override;
    void process(FrameData& frame, cudaStream_t stream) override;
    std::string_view name() const noexcept override { return "EncodeImage"; }

private:
    float scale_{0.25f};
    int   jpeg_quality_{50};
};
