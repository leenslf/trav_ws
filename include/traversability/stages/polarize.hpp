#pragma once
#include <cstddef>
#include <cstdint>
#include "traversability/pipeline_stage.hpp"

class PolarizeStage : public IPipelineStage {
public:
    ~PolarizeStage();
    void init(const PipelineConfig& cfg, FrameData& frame) override;
    void process(FrameData& frame, cudaStream_t stream) override;
    std::string_view name() const noexcept override { return "Polarize"; }

private:
    // Baked config
    float z_threshold_{0.0f};
    float min_range_{0.0f};

    // Pre-allocated scratch buffers (sized to max_n_ in init)
    int      max_n_{0};
    int32_t* d_flags_{nullptr};
    int32_t* d_offsets_{nullptr};
    void*    d_scan_tmp_{nullptr};
    size_t   scan_tmp_bytes_{0};
};
