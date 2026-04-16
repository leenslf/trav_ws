#include "traversability/stages/extract_xyz.hpp"

#include <cstdint>
#include <cub/cub.cuh>
#include <cuda_runtime.h>

namespace {

// ---------------------------------------------------------------------------
// Kernel 1 — mark valid pixels
// ---------------------------------------------------------------------------
// flags[i] = 1 if pixel i has finite x, y, z; 0 otherwise.
__global__ void mark_valid_kernel(
    const float4* __restrict__ src,
    int32_t*      __restrict__ flags,
    int                        total)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= total) return;
    const float4 p = src[i];
    flags[i] = (isfinite(p.x) && isfinite(p.y) && isfinite(p.z)) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Kernel 2 — scatter valid points to compacted positions
// ---------------------------------------------------------------------------
// offsets[i] is the exclusive prefix sum of flags; it gives the output slot
// for pixel i when flags[i] == 1.
// Output is float3 AoS [x, y, z] written directly into the destination buffer.
__global__ void scatter_valid_kernel(
    const float4*  __restrict__ src,
    const int32_t* __restrict__ flags,
    const int32_t* __restrict__ offsets,
    float3*        __restrict__ dst,
    int                         total)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= total || !flags[i]) return;
    const float4 p = src[i];
    dst[offsets[i]] = make_float3(p.x, p.y, p.z);
}

// ---------------------------------------------------------------------------
// Kernel 3 — pack valid-point count into a single device integer
// ---------------------------------------------------------------------------
// Avoids two separate D2H cudaMemcpy calls (one for last_offset, one for
// last_flag) by combining them on-device into a single value.
__global__ void pack_count_kernel(
    const int32_t* __restrict__ offsets,
    const int32_t* __restrict__ flags,
    int32_t*       __restrict__ out,
    int                         last_idx)
{
    if (threadIdx.x == 0) {
        out[0] = offsets[last_idx] + flags[last_idx];
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ExtractXYZStage
// ---------------------------------------------------------------------------

ExtractXYZStage::~ExtractXYZStage() {
    cudaFree(d_flags_);
    cudaFree(d_offsets_);
    cudaFree(d_count_);
    cudaFree(d_scan_tmp_);
}

void ExtractXYZStage::init(const PipelineConfig& /*cfg*/, FrameData& frame) {
    const int N = frame.raw_points.count;
    max_n_ = N;

    cudaMalloc(&d_flags_,   N * sizeof(int32_t));
    cudaMalloc(&d_offsets_, N * sizeof(int32_t));
    cudaMalloc(&d_count_,       sizeof(int32_t));

    // Query CUB temp buffer size for worst-case N, then allocate once.
    cub::DeviceScan::ExclusiveSum(
        nullptr, scan_tmp_bytes_, d_flags_, d_offsets_, N);
    cudaMalloc(&d_scan_tmp_, scan_tmp_bytes_);

    frame.finite_points.allocate(N);
}

void ExtractXYZStage::process(FrameData& frame, cudaStream_t stream) {
    const int N = frame.raw_count;
    if (N == 0) { frame.finite_count = 0; return; }

    constexpr int kThreads = 256;
    const int blocks = (N + kThreads - 1) / kThreads;

    // Phase 1: mark valid pixels.
    mark_valid_kernel<<<blocks, kThreads, 0, stream>>>(
        frame.raw_points.ptr, d_flags_, N);

    // Phase 2: exclusive prefix sum → output slot per valid pixel.
    cub::DeviceScan::ExclusiveSum(
        d_scan_tmp_, scan_tmp_bytes_, d_flags_, d_offsets_, N, stream);

    // Phase 3: scatter valid points as float3 into frame.finite_points.
    scatter_valid_kernel<<<blocks, kThreads, 0, stream>>>(
        frame.raw_points.ptr, d_flags_, d_offsets_,
        frame.finite_points.ptr, N);

    // Phase 4: pack last_offset + last_flag into d_count_ on-device.
    pack_count_kernel<<<1, 1, 0, stream>>>(
        d_offsets_, d_flags_, d_count_, N - 1);

    // Read back valid count — requires stream sync.
    cudaStreamSynchronize(stream);
    int32_t n_valid = 0;
    cudaMemcpy(&n_valid, d_count_, sizeof(int32_t), cudaMemcpyDeviceToHost);

    frame.finite_count = n_valid;
}
