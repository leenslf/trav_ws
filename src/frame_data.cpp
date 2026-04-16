#include "traversability/frame_data.hpp"
#include <cstdio>

void FrameData::allocate(const PipelineConfig& cfg) {
    // Worst-case point count: every pixel in the camera frame produces
    // a valid depth reading. This is the ceiling for all point cloud
    // buffers regardless of how many points survive downstream filtering.
    const int max_points = cfg.zed.w * cfg.zed.h;

    // --- Input buffer (written by ZEDSource) ---
    // float4 because the ZED SDK yields XYZW (W is unused padding).
    raw_points.allocate(max_points);

    // --- ExtractXYZ output ---
    // Worst case: every raw point is finite. float3 from here onward
    // since W is dropped by ExtractXYZ.
    finite_points.allocate(max_points);

    // --- TiltCompensate output ---
    // Point count is unchanged by rotation — no filtering occurs.
    aligned_points.allocate(max_points);

    // --- VoxelFilter output ---
    // Count is strictly less than or equal to aligned_points after
    // downsampling, but we allocate the full ceiling to avoid
    // any conditional sizing logic here.
    voxel_points.allocate(max_points);

    // --- Polarize output ---
    // Count is further reduced by range and height filtering.
    // Same ceiling rationale as voxel_points.
    polar_points.allocate(max_points);

    // --- TraversabilityResult ---
    // CPU-side vectors. Not allocated here — TraversabilityStage::init()
    // owns this allocation because only that stage knows the grid
    // dimensions derived from TraversabilityConfig. (?)

    fprintf(stderr, "[frame_data] allocated %d buffers x %d points (%.1f MB)\n",
            5, max_points,
            5.0f * max_points * sizeof(float3) / (1024.0f * 1024.0f));
}
