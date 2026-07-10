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

    // Pinned host buffers for async D2H copy
    float* h_trav_grid_{nullptr};
    float* h_terrain_{nullptr};

    // Adaptive per-ring theta-bin merging (see adaptive_theta_merge in
    // TraversabilityConfig). Allocated only when the flag is enabled; stay
    // nullptr/unused otherwise, so this is a no-op addition when off.
    bool   adaptive_theta_merge_{false};
    int*   d_theta_merge_k_{nullptr};    // [r_bins] per-ring merge factor k(i), baked once at init
    float* d_theta_group_map_{nullptr};  // [cells] compact per-(ring,group) max-Z scratch
};
