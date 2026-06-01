#pragma once
#ifndef TRAVERSABILITY_FRAME_DATA_HPP
#define TRAVERSABILITY_FRAME_DATA_HPP

#include <cstdint>
#include <cuda_runtime.h>
#include <sl/Camera.hpp>
#include "traversability/image_payload.hpp"
#include "traversability/pose.hpp"
#include "traversability/result.hpp"
#include "traversability/config.hpp"

template<typename T>
struct GpuBuffer {
    T*   ptr{nullptr};
    int  count{0};
    bool ptr_owned_{true};  // false when ptr is borrowed from an external allocator (e.g. ZED SDK)

    void allocate(int n) {
        cudaMalloc(&ptr, n * sizeof(T));
        count      = n;
        ptr_owned_ = true;
    }

    ~GpuBuffer() {
        // Only free memory this buffer allocated.
        // Borrowed pointers (ptr_owned_ = false) are owned by the external
        // allocator -- freeing them here would corrupt the ZED SDK's internal state.
        if (ptr && ptr_owned_) cudaFree(ptr);
    }

    GpuBuffer()                            = default;
    GpuBuffer(const GpuBuffer&)            = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&&)                 = default;
    GpuBuffer& operator=(GpuBuffer&&)      = default;
};

struct FrameData {
    // Input
    GpuBuffer<float4>  raw_points;
    int                raw_count{0};
    sl::Mat            image_raw;      // CPU-side BGRA from ZEDSource::capture()
    ImagePayload       image_encoded;  // JPEG output from EncodeImageStage
    // After ExtractXYZ
    GpuBuffer<float3>  finite_points;
    int                finite_count{0};
    // After TiltCompensate
    GpuBuffer<float3>  aligned_points;
    // After VoxelFilter
    GpuBuffer<float3>  voxel_points;
    int                voxel_count{0};
    // After Polarize
    GpuBuffer<float3>  polar_points;
    int                polar_count{0};
    // After Traversability
    TraversabilityResult result;
    // Metadata
    uint64_t           timestamp_ns{0};
    CameraPose         camera_pose;
    TrackingState      tracking_state{TrackingState::UNAVAILABLE};

    // Allocates all GPU buffers once at startup.
    //
    // Sizing strategy: every buffer is sized to the worst-case input —
    // the full camera resolution (width × height points). This guarantees
    // that no allocation ever occurs in the hot path, at the cost of
    // reserving more memory than most frames will use.
    //
    // In practice the point count drops sharply through the pipeline: (currently considering worst case)
    //   raw_points      up to width×height (~2M at 1080p)
    //   finite_points   same ceiling (worst case: all pixels valid)
    //   aligned_points  same ceiling (TiltCompensate does not filter)
    //   voxel_points    much lower (VoxelFilter collapses clusters)
    //   polar_points    lower still (Polarize filters by range and height)
    //
    // The uniform ceiling keeps the allocator simple and the memory
    // layout predictable. Revisit only if memory pressure is observed.
    //
    // TraversabilityResult (CPU-side vectors) is NOT allocated here.
    // It is allocated by TraversabilityStage::init(), which knows the
    // grid dimensions derived from TraversabilityConfig.
    //
    // Must be called once by PipelineRunner::init() before the frame loop.
    void allocate(const PipelineConfig& cfg);
};

#endif // TRAVERSABILITY_FRAME_DATA_HPP
