#pragma once
#include <cstdint>
#include "traversability/pipeline_stage.hpp"

class TraversabilityStage : public IPipelineStage {
public:
    ~TraversabilityStage();
    void init(const PipelineConfig& cfg, FrameData& frame) override;
    void process(FrameData& frame, cudaStream_t stream) override;
    std::string_view name() const noexcept override { return "Traversability"; }

private:
    // Baked config
    float r_min_{0.f}, theta_min_{0.f};
    float dr_{0.f},    dtheta_{0.f};
    float scrit_{0.f}, rcrit_m_{0.f}, hcrit_m_{0.f};
    float danger_threshold_{0.f};
    int   r_bins_{0}, theta_bins_{0};

    // Pre-allocated device scratch + output buffers
    float*   d_height_map_{nullptr};
    float*   d_terrain_{nullptr};
    uint8_t* d_valid_mask_{nullptr};
    float*   d_slope_{nullptr};
    float*   d_roughness_{nullptr};
    float*   d_step_height_{nullptr};
    uint8_t* d_nontraversable_{nullptr};
    uint8_t* d_observed_mask_{nullptr};
    float*   d_trav_grid_{nullptr};

    // Pinned host buffer for async D2H copy
    float* h_trav_grid_{nullptr};
};
