#pragma once
#include <cstddef>
#include <cstdint>
#include "traversability/pipeline_stage.hpp"

class VoxelFilterStage : public IPipelineStage {
public:
    ~VoxelFilterStage();
    void init(const PipelineConfig& cfg, FrameData& frame) override;
    void process(FrameData& frame, cudaStream_t stream) override;
    std::string_view name() const noexcept override { return "VoxelFilter"; }

private:
    // Baked config
    float vx_{1.f}, vy_{1.f}, vz_{1.f};
    int   min_pts_{1};

    // Pre-allocated scratch buffers (sized to max_n_ in init)
    int       max_n_{0};
    uint64_t* d_keys_{nullptr};
    uint64_t* d_sorted_keys_{nullptr};
    uint64_t* d_unique_keys_{nullptr};
    int32_t*  d_counts_{nullptr};
    int32_t*  d_num_runs_{nullptr};
    int32_t*  d_flags_{nullptr};
    int32_t*  d_offsets_{nullptr};

    void*  d_sort_tmp_{nullptr};  std::size_t sort_tmp_bytes_{0};
    void*  d_rle_tmp_{nullptr};   std::size_t rle_tmp_bytes_{0};
    void*  d_scan_tmp_{nullptr};  std::size_t scan_tmp_bytes_{0};
};
