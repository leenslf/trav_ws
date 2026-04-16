#include "traversability/stages/voxel_filter.hpp"

#include <cstdint>
#include <cub/cub.cuh>
#include <cuda_runtime.h>

// ---------------------------------------------------------------------------
// Key encoding — 63-bit packed voxel key (21 bits per axis, ±1 048 575 range)
//   bits [ 0:20] → ix + kOffset21
//   bits [21:41] → iy + kOffset21
//   bits [42:62] → iz + kOffset21
// ---------------------------------------------------------------------------
namespace {

static constexpr uint64_t kOffset21 = 1ULL << 20;
static constexpr uint64_t kMask21   = (1ULL << 21) - 1ULL;

__host__ __device__ __forceinline__
uint64_t encode_voxel(int64_t ix, int64_t iy, int64_t iz) {
    return (static_cast<uint64_t>(iz + static_cast<int64_t>(kOffset21)) << 42) |
           (static_cast<uint64_t>(iy + static_cast<int64_t>(kOffset21)) << 21) |
            static_cast<uint64_t>(ix + static_cast<int64_t>(kOffset21));
}

__host__ __device__ __forceinline__
void decode_voxel(uint64_t key, int64_t& ix, int64_t& iy, int64_t& iz) {
    ix = static_cast<int64_t>( key        & kMask21) - static_cast<int64_t>(kOffset21);
    iy = static_cast<int64_t>((key >> 21) & kMask21) - static_cast<int64_t>(kOffset21);
    iz = static_cast<int64_t>((key >> 42) & kMask21) - static_cast<int64_t>(kOffset21);
}

// Phase 1 — one uint64 voxel key per point (AoS float3 input)
__global__ void compute_keys_kernel(
    const float3*  __restrict__ in,
    uint64_t*      __restrict__ keys,
    int   N,
    float vx, float vy, float vz)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    const int64_t ix = static_cast<int64_t>(floorf(in[i].x / vx));
    const int64_t iy = static_cast<int64_t>(floorf(in[i].y / vy));
    const int64_t iz = static_cast<int64_t>(floorf(in[i].z / vz));
    keys[i] = encode_voxel(ix, iy, iz);
}

// Phase 4 — flag unique voxels that meet the min-points threshold
__global__ void mark_voxels_kernel(
    const int32_t* __restrict__ counts,
    int32_t*       __restrict__ flags,
    int M, int min_points)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M) return;
    flags[i] = (counts[i] >= min_points) ? 1 : 0;
}

// Phase 6 — scatter voxel centres for surviving voxels (AoS float3 output)
__global__ void scatter_centers_kernel(
    const uint64_t* __restrict__ unique_keys,
    const int32_t*  __restrict__ flags,
    const int32_t*  __restrict__ offsets,
    float3*         __restrict__ out,
    int   M,
    float vx, float vy, float vz)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M || !flags[i]) return;
    int64_t ix, iy, iz;
    decode_voxel(unique_keys[i], ix, iy, iz);
    out[offsets[i]] = make_float3((static_cast<float>(ix) + 0.5f) * vx,
                                  (static_cast<float>(iy) + 0.5f) * vy,
                                  (static_cast<float>(iz) + 0.5f) * vz);
}

} // namespace

// ---------------------------------------------------------------------------
// VoxelFilterStage
// ---------------------------------------------------------------------------

VoxelFilterStage::~VoxelFilterStage() {
    cudaFree(d_keys_);
    cudaFree(d_sorted_keys_);
    cudaFree(d_unique_keys_);
    cudaFree(d_counts_);
    cudaFree(d_num_runs_);
    cudaFree(d_flags_);
    cudaFree(d_offsets_);
    cudaFree(d_sort_tmp_);
    cudaFree(d_rle_tmp_);
    cudaFree(d_scan_tmp_);
}

void VoxelFilterStage::init(const PipelineConfig& cfg, FrameData& frame) {
    vx_      = cfg.voxel_filter.voxel_size_x;
    vy_      = cfg.voxel_filter.voxel_size_y;
    vz_      = cfg.voxel_filter.voxel_size_z;
    min_pts_ = cfg.voxel_filter.min_points_per_voxel;

    const int N = frame.raw_points.count;
    max_n_ = N;

    cudaMalloc(&d_keys_,        N * sizeof(uint64_t));
    cudaMalloc(&d_sorted_keys_, N * sizeof(uint64_t));
    cudaMalloc(&d_unique_keys_, N * sizeof(uint64_t));
    cudaMalloc(&d_counts_,      N * sizeof(int32_t));
    cudaMalloc(&d_num_runs_,        sizeof(int32_t));
    cudaMalloc(&d_flags_,       N * sizeof(int32_t));
    cudaMalloc(&d_offsets_,     N * sizeof(int32_t));

    // Query CUB temp buffer sizes for the worst-case N, then allocate once.
    cub::DeviceRadixSort::SortKeys(
        nullptr, sort_tmp_bytes_, d_keys_, d_sorted_keys_, N);
    cudaMalloc(&d_sort_tmp_, sort_tmp_bytes_);

    cub::DeviceRunLengthEncode::Encode(
        nullptr, rle_tmp_bytes_,
        d_sorted_keys_, d_unique_keys_, d_counts_, d_num_runs_, N);
    cudaMalloc(&d_rle_tmp_, rle_tmp_bytes_);

    cub::DeviceScan::ExclusiveSum(
        nullptr, scan_tmp_bytes_, d_flags_, d_offsets_, N);
    cudaMalloc(&d_scan_tmp_, scan_tmp_bytes_);

    frame.voxel_points.allocate(N);
}

void VoxelFilterStage::process(FrameData& frame, cudaStream_t stream) {
    const int N = frame.finite_count;
    if (N == 0) { frame.voxel_count = 0; return; }

    constexpr int kThreads = 256;
    const int blocks_n = (N + kThreads - 1) / kThreads;

    // Phase 1: compute one key per point
    compute_keys_kernel<<<blocks_n, kThreads, 0, stream>>>(
        frame.aligned_points.ptr, d_keys_, N, vx_, vy_, vz_);

    // Phase 2: sort keys so equal voxels are adjacent
    cub::DeviceRadixSort::SortKeys(
        d_sort_tmp_, sort_tmp_bytes_,
        d_keys_, d_sorted_keys_, N, 0, 64, stream);

    // Phase 3: run-length encode → unique keys + per-voxel counts
    cub::DeviceRunLengthEncode::Encode(
        d_rle_tmp_, rle_tmp_bytes_,
        d_sorted_keys_, d_unique_keys_, d_counts_, d_num_runs_, N, stream);

    // Read M (number of unique voxels) — requires stream sync
    cudaStreamSynchronize(stream);
    int32_t M = 0;
    cudaMemcpy(&M, d_num_runs_, sizeof(int32_t), cudaMemcpyDeviceToHost);

    if (M == 0) { frame.voxel_count = 0; return; }

    const int blocks_m = (M + kThreads - 1) / kThreads;

    // Phase 4: flag voxels that pass the min-points threshold
    mark_voxels_kernel<<<blocks_m, kThreads, 0, stream>>>(
        d_counts_, d_flags_, M, min_pts_);

    // Phase 5: exclusive prefix sum → output scatter positions
    cub::DeviceScan::ExclusiveSum(
        d_scan_tmp_, scan_tmp_bytes_, d_flags_, d_offsets_, M, stream);

    // Read K (number of surviving voxels) — requires stream sync
    cudaStreamSynchronize(stream);
    int32_t last_offset = 0, last_flag = 0;
    cudaMemcpy(&last_offset, d_offsets_ + M - 1, sizeof(int32_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(&last_flag,   d_flags_   + M - 1, sizeof(int32_t), cudaMemcpyDeviceToHost);
    const int K = last_offset + last_flag;

    frame.voxel_count = K;
    if (K == 0) return;

    // Phase 6: scatter voxel centres into frame.voxel_points
    scatter_centers_kernel<<<blocks_m, kThreads, 0, stream>>>(
        d_unique_keys_, d_flags_, d_offsets_,
        frame.voxel_points.ptr, M, vx_, vy_, vz_);
}
