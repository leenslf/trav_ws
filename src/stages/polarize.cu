#include "traversability/stages/polarize.hpp"

#include <cstdint>
#include <cub/cub.cuh>
#include <cuda_runtime.h>

namespace {

// ---------------------------------------------------------------------------
// Phase 1 — mark valid points
// ---------------------------------------------------------------------------
// flags[i] = 1 iff |z| < z_threshold AND sqrt(x²+y²) > min_range.
__global__ void mark_polarize_kernel(
    const float3* __restrict__ in,
    int32_t*      __restrict__ flags,
    int   N,
    float z_threshold,
    float min_range)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    const float x = in[i].x;
    const float y = in[i].y;
    const float z = in[i].z;
    const float r = sqrtf(x * x + y * y);
    flags[i] = (fabsf(z) < z_threshold && r > min_range) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Phase 3 — scatter surviving points as [r, θ, z] polar form
// ---------------------------------------------------------------------------
__global__ void scatter_polarize_kernel(
    const float3*  __restrict__ in,
    const int32_t* __restrict__ flags,
    const int32_t* __restrict__ offsets,
    float3*        __restrict__ out,
    int N)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N || !flags[i]) return;
    const float x     = in[i].x;
    const float y     = in[i].y;
    const float z     = in[i].z;
    const float r     = sqrtf(x * x + y * y);
    const float theta = atan2f(y, x);
    out[offsets[i]] = make_float3(r, theta, z);
}

} // namespace

// ---------------------------------------------------------------------------
// PolarizeStage
// ---------------------------------------------------------------------------

PolarizeStage::~PolarizeStage() {
    cudaFree(d_flags_);
    cudaFree(d_offsets_);
    cudaFree(d_scan_tmp_);
}

void PolarizeStage::init(const PipelineConfig& cfg, FrameData& frame) {
    z_threshold_ = cfg.polarize.z_threshold;
    min_range_   = cfg.polarize.min_range;

    const int N = frame.voxel_points.count;
    max_n_ = N;

    cudaMalloc(&d_flags_,   N * sizeof(int32_t));
    cudaMalloc(&d_offsets_, N * sizeof(int32_t));

    // Query CUB temp buffer size for worst-case N, then allocate once.
    cub::DeviceScan::ExclusiveSum(
        nullptr, scan_tmp_bytes_, d_flags_, d_offsets_, N);
    cudaMalloc(&d_scan_tmp_, scan_tmp_bytes_);

    frame.polar_points.allocate(N);
}

void PolarizeStage::process(FrameData& frame, cudaStream_t stream) {
    const int N = frame.voxel_count;
    if (N == 0) { frame.polar_count = 0; return; }

    constexpr int kThreads = 256;
    const int blocks = (N + kThreads - 1) / kThreads;

    // Phase 1: mark valid points
    mark_polarize_kernel<<<blocks, kThreads, 0, stream>>>(
        frame.voxel_points.ptr, d_flags_, N, z_threshold_, min_range_);

    // Phase 2: exclusive prefix sum → scatter positions
    cub::DeviceScan::ExclusiveSum(
        d_scan_tmp_, scan_tmp_bytes_, d_flags_, d_offsets_, N, stream);

    // Read K (number of surviving points) — requires stream sync
    cudaStreamSynchronize(stream);
    int32_t last_offset = 0, last_flag = 0;
    cudaMemcpy(&last_offset, d_offsets_ + N - 1, sizeof(int32_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(&last_flag,   d_flags_   + N - 1, sizeof(int32_t), cudaMemcpyDeviceToHost);
    const int K = last_offset + last_flag;

    frame.polar_count = K;
    if (K == 0) return;

    // Phase 3: scatter surviving points as polar [r, θ, z]
    scatter_polarize_kernel<<<blocks, kThreads, 0, stream>>>(
        frame.voxel_points.ptr, d_flags_, d_offsets_,
        frame.polar_points.ptr, N);
}
