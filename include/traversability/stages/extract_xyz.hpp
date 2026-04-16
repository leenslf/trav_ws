#pragma once
#include <cstddef>
#include <cstdint>
#include "traversability/pipeline_stage.hpp"

class ExtractXYZStage : public IPipelineStage {
public:
    ~ExtractXYZStage();
    void init(const PipelineConfig& cfg, FrameData& frame) override;
    void process(FrameData& frame, cudaStream_t stream) override;
    std::string_view name() const noexcept override { return "ExtractXYZ"; }

private:
    // Pre-allocated scratch buffers (sized to max_n_ in init)
    int      max_n_{0};
    int32_t* d_flags_{nullptr};
    int32_t* d_offsets_{nullptr};
    int32_t* d_count_{nullptr};   // single-element: packed valid count
    void*    d_scan_tmp_{nullptr};
    size_t   scan_tmp_bytes_{0};
};
