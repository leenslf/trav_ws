#include "traversability/stages/traversability.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

#include <cuda_runtime.h>

namespace {

static constexpr float kPi    = 3.14159265358979323846f;
static constexpr int   kHalf  = 2;                                    // 5×5 window half-width
static constexpr int   kNCrit = (2 * kHalf + 1) * (2 * kHalf + 1) - 1; // 24 neighbours

// ---------------------------------------------------------------------------
// Device helpers
// ---------------------------------------------------------------------------

__device__ __forceinline__ int reflect_index_dev(int idx, int size) {
    if (size <= 1) return 0;
    if (idx < 0)     return -idx - 1;
    if (idx >= size) return 2 * size - idx - 1;
    return idx;
}

// CAS-based float atomicMax — correct for all finite floats including negatives.
__device__ __forceinline__ void atomicMaxFloat(float* addr, float val) {
    int* addr_as_int = reinterpret_cast<int*>(addr);
    int  old = *addr_as_int, assumed;
    do {
        assumed = old;
        if (__int_as_float(assumed) >= val) return;
        old = atomicCAS(addr_as_int, assumed, __float_as_int(val));
    } while (old != assumed);
}

// ---------------------------------------------------------------------------
// Kernel 0 — initialise a float buffer to a constant value
// ---------------------------------------------------------------------------
__global__ void init_float_kernel(float* buf, int n, float val) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) buf[i] = val;
}

// ---------------------------------------------------------------------------
// Kernel 1 — height-map binning (one thread per input point)
//
// Input: float3 AoS where .x=r, .y=theta, .z=z.
// Output grid: row-major (r_bins × theta_bins), index = r_bin * theta_bins + theta_bin.
// Sentinel −FLT_MAX marks cells that received no points.
// ---------------------------------------------------------------------------
__global__ void build_height_map_kernel(
    const float3* __restrict__ d_points,
    float*        __restrict__ d_height_map,
    int   N,
    int   r_bins,
    int   theta_bins,
    float r_min,
    float theta_min,
    float inv_dr,
    float inv_dtheta)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;

    const float r     = d_points[i].x;
    const float theta = d_points[i].y;
    const float z     = d_points[i].z;

    const int r_bin     = static_cast<int>(floorf((r     - r_min)     * inv_dr));
    const int theta_bin = static_cast<int>(floorf((theta - theta_min) * inv_dtheta));

    if (r_bin < 0 || r_bin >= r_bins || theta_bin < 0 || theta_bin >= theta_bins) return;

    atomicMaxFloat(&d_height_map[r_bin * theta_bins + theta_bin], z);
}

// ---------------------------------------------------------------------------
// Kernel 2 — terrain fill and valid-cell mask (one thread per cell)
//
// Row-major grid: index = i * nc + j  (i = r_idx, j = theta_idx).
// ---------------------------------------------------------------------------
__global__ void fill_terrain_kernel(
    const float*   __restrict__ d_height_map,
    float*         __restrict__ d_terrain,
    uint8_t*       __restrict__ d_valid_mask,
    int   nr, int nc,
    float sentinel)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= nr || j >= nc) return;

    const int   idx   = i * nc + j;
    const float hv    = d_height_map[idx];
    const bool  valid = (hv != sentinel);

    d_valid_mask[idx] = valid ? 1u : 0u;
    d_terrain[idx]    = valid ? hv : -0.3f;
}

// ---------------------------------------------------------------------------
// Kernel 3 — gradient + slope (one thread per cell)
//
// 3-point stencil with arc-length correction for the theta direction.
// Cells exceeding scrit are set to +inf.
// ---------------------------------------------------------------------------
__global__ void gradient_slope_kernel(
    const float* __restrict__ d_terrain,
    float*       __restrict__ d_slope,
    int   nr, int nc,
    float r_min,
    float dr,
    float dtheta,
    float scrit,
    float inf_val)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= nr || j >= nc) return;

    // ∂z/∂r — 3-point stencil along r (rows)
    float dzdx = 0.0f;
    if (nr > 1) {
        if (i == 0) {
            dzdx = (d_terrain[(i + 1) * nc + j] - d_terrain[i * nc + j]) / dr;
        } else if (i == nr - 1) {
            dzdx = (d_terrain[i * nc + j] - d_terrain[(i - 1) * nc + j]) / dr;
        } else {
            dzdx = (d_terrain[(i + 1) * nc + j] - d_terrain[(i - 1) * nc + j]) / (2.0f * dr);
        }
    }

    // ∂z/∂θ — 3-point stencil along theta (cols)
    float dzdy = 0.0f;
    if (nc > 1) {
        if (j == 0) {
            dzdy = (d_terrain[i * nc + (j + 1)] - d_terrain[i * nc + j]) / dtheta;
        } else if (j == nc - 1) {
            dzdy = (d_terrain[i * nc + j] - d_terrain[i * nc + (j - 1)]) / dtheta;
        } else {
            dzdy = (d_terrain[i * nc + (j + 1)] - d_terrain[i * nc + (j - 1)]) / (2.0f * dtheta);
        }
    }

    const float r_centre    = r_min + (static_cast<float>(i) + 0.5f) * dr;
    const float dzdy_metric = dzdy / r_centre;
    float slope = atanf(sqrtf(dzdx * dzdx + dzdy_metric * dzdy_metric));
    if (slope > scrit) slope = inf_val;

    d_slope[i * nc + j] = slope;
}

// ---------------------------------------------------------------------------
// Kernel 4 — 3×3 local std-dev (roughness) with reflect padding (one thread per cell)
//
// Cells exceeding rcrit_m are set to +inf.
// ---------------------------------------------------------------------------
__global__ void roughness_kernel(
    const float* __restrict__ d_terrain,
    float*       __restrict__ d_roughness,
    int   nr, int nc,
    float rcrit_m,
    float inf_val)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= nr || j >= nc) return;

    float sum    = 0.0f;
    float sum_sq = 0.0f;

    for (int di = -1; di <= 1; ++di) {
        const int ri = reflect_index_dev(i + di, nr);
        for (int dj = -1; dj <= 1; ++dj) {
            const int cj = reflect_index_dev(j + dj, nc);
            const float v = d_terrain[ri * nc + cj];
            sum    += v;
            sum_sq += v * v;
        }
    }

    const float mean = sum / 9.0f;
    const float var  = fmaxf(0.0f, sum_sq / 9.0f - mean * mean);
    float r = sqrtf(var);
    if (r > rcrit_m) r = inf_val;
    d_roughness[i * nc + j] = r;
}

// ---------------------------------------------------------------------------
// Kernel 5 — 5×5 step-height metric (one thread per cell)
//
// Cartesian distances computed inline from polar grid parameters.
// Cells exceeding hcrit_m are set to +inf.
// ---------------------------------------------------------------------------
__global__ void step_height_kernel(
    const float* __restrict__ d_terrain,
    float*       __restrict__ d_step_height,
    int   nr, int nc,
    float r_min,
    float theta_min,
    float dr,
    float dtheta,
    float hcrit_m,
    float scrit,
    float inf_val)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= nr || j >= nc) return;

    const float r_c = r_min     + (static_cast<float>(i) + 0.5f) * dr;
    const float t_c = theta_min + (static_cast<float>(j) + 0.5f) * dtheta;
    const float x0  = r_c * cosf(t_c);
    const float y0  = r_c * sinf(t_c);
    const float z0  = d_terrain[i * nc + j];

    int   st_count = 0;
    float h_max    = 0.0f;

    for (int di = -kHalf; di <= kHalf; ++di) {
        for (int dj = -kHalf; dj <= kHalf; ++dj) {
            if (di == 0 && dj == 0) continue;

            const int ri = reflect_index_dev(i + di, nr);
            const int cj = reflect_index_dev(j + dj, nc);

            const float r_n = r_min     + (static_cast<float>(ri) + 0.5f) * dr;
            const float t_n = theta_min + (static_cast<float>(cj) + 0.5f) * dtheta;
            const float xn  = r_n * cosf(t_n);
            const float yn  = r_n * sinf(t_n);

            const float dz  = fabsf(z0 - d_terrain[ri * nc + cj]);
            const float dxy = sqrtf((x0 - xn) * (x0 - xn) + (y0 - yn) * (y0 - yn));

            if (dxy == 0.0f) continue;

            const float pair_slope = atan2f(dz, dxy);
            if (dz > hcrit_m && pair_slope > scrit) {
                ++st_count;
                if (dz > h_max) h_max = dz;
            }
        }
    }

    const float scaled = h_max * static_cast<float>(st_count) / static_cast<float>(kNCrit);
    float sh = fminf(h_max, scaled);
    if (sh > hcrit_m) sh = inf_val;
    d_step_height[i * nc + j] = sh;
}

// ---------------------------------------------------------------------------
// Kernel 6 — danger value and nontraversable mask (one thread per cell)
//
// danger = 0.3·slope/scrit + 0.3·roughness/rcrit_m + 0.4·step_height/hcrit_m
// Invalid cells (valid_mask == 0) are not flagged so they don't block ray-cast.
// ---------------------------------------------------------------------------
__global__ void danger_nontraversable_kernel(
    const float*   __restrict__ d_slope,
    const float*   __restrict__ d_roughness,
    const float*   __restrict__ d_step_height,
    const uint8_t* __restrict__ d_valid_mask,
    uint8_t*       __restrict__ d_nontraversable,
    int   nr, int nc,
    float scrit,
    float rcrit_m,
    float hcrit_m,
    float danger_threshold)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= nr || j >= nc) return;

    const int idx = i * nc + j;
    if (!d_valid_mask[idx]) {
        d_nontraversable[idx] = 0u;
        return;
    }

    const float danger = 0.3f * d_slope[idx]      / scrit   +
                         0.3f * d_roughness[idx]   / rcrit_m +
                         0.4f * d_step_height[idx] / hcrit_m;

    d_nontraversable[idx] = (danger > danger_threshold) ? 1u : 0u;
}

// ---------------------------------------------------------------------------
// Kernel 7 — ray-cast mask (one thread per angular column)
//
// Scans radially outward; marks all cells before the first obstacle as free.
// ---------------------------------------------------------------------------
__global__ void ray_cast_kernel(
    const uint8_t* __restrict__ d_nontraversable,
    uint8_t*       __restrict__ d_observed_mask,
    int nr, int nc)
{
    const int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j >= nc) return;

    int closest = -1;
    for (int i = 0; i < nr; ++i) {
        if (d_nontraversable[i * nc + j]) {
            closest = i;
            break;
        }
    }
    for (int i = 0; i < closest; ++i) {
        d_observed_mask[i * nc + j] = 1u;
    }
}

// ---------------------------------------------------------------------------
// Kernel 8 — assemble final trav_grid (one thread per cell)
//
// NaN = unknown, 0 = observed free, 1 = nontraversable.
// Nontraversable always wins when both flags are set.
// ---------------------------------------------------------------------------
__global__ void trav_grid_kernel(
    const uint8_t* __restrict__ d_observed_mask,
    const uint8_t* __restrict__ d_nontraversable,
    float*         __restrict__ d_trav_grid,
    int   nr, int nc,
    float nan_val)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (i >= nr || j >= nc) return;

    const int idx = i * nc + j;
    float val = nan_val;
    if ( d_observed_mask[idx] && !d_nontraversable[idx]) val = 0.0f;
    if (!d_observed_mask[idx] &&  d_nontraversable[idx]) val = 1.0f;
    if ( d_observed_mask[idx] &&  d_nontraversable[idx]) val = 1.0f;
    d_trav_grid[idx] = val;
}

} // namespace

// ---------------------------------------------------------------------------
// TraversabilityStage
// ---------------------------------------------------------------------------

TraversabilityStage::~TraversabilityStage() {
    cudaFree(d_height_map_);
    cudaFree(d_terrain_);
    cudaFree(d_valid_mask_);
    cudaFree(d_slope_);
    cudaFree(d_roughness_);
    cudaFree(d_step_height_);
    cudaFree(d_nontraversable_);
    cudaFree(d_observed_mask_);
    cudaFree(d_trav_grid_);
    cudaFreeHost(h_trav_grid_);
}

void TraversabilityStage::init(const PipelineConfig& cfg, FrameData& frame) {
    const auto& tc = cfg.traversability;

    r_min_            = tc.r_min_m;
    theta_min_        = tc.theta_min_deg * kPi / 180.0f;
    dr_               = tc.polar_grid_size_r_m;
    dtheta_           = tc.polar_grid_size_theta_deg * kPi / 180.0f;
    scrit_            = tc.scrit_deg * kPi / 180.0f;
    rcrit_m_          = tc.rcrit_m;
    hcrit_m_          = tc.hcrit_m;
    danger_threshold_ = tc.danger_threshold;

    const float r_max     = tc.r_max_m;
    const float theta_max = tc.theta_max_deg * kPi / 180.0f;

    // Compute bin counts using the same arange logic as the reference implementation:
    //   arange(start, stop, step) iterates while x < stop.
    //   edge count = iterations; bins = edges - 1.
    r_bins_     = 0;
    theta_bins_ = 0;
    for (float x = r_min_;     x < r_max     + dr_;     x += dr_)     ++r_bins_;
    for (float x = theta_min_; x < theta_max + dtheta_; x += dtheta_) ++theta_bins_;
    --r_bins_;      // edges → bins
    --theta_bins_;
    if (r_bins_     < 0) r_bins_     = 0;
    if (theta_bins_ < 0) theta_bins_ = 0;

    const int cells = r_bins_ * theta_bins_;

    // Fill result metadata (fixed for all frames).
    frame.result.r_bins     = r_bins_;
    frame.result.theta_bins = theta_bins_;

    // Pre-allocate output vector (resized once; overwritten each frame).
    frame.result.trav_grid.resize(static_cast<size_t>(cells));

    if (cells == 0) return;

    const size_t fbytes = static_cast<size_t>(cells) * sizeof(float);
    const size_t bbytes = static_cast<size_t>(cells) * sizeof(uint8_t);

    cudaMalloc(&d_height_map_,     fbytes);
    cudaMalloc(&d_terrain_,        fbytes);
    cudaMalloc(&d_valid_mask_,     bbytes);
    cudaMalloc(&d_slope_,          fbytes);
    cudaMalloc(&d_roughness_,      fbytes);
    cudaMalloc(&d_step_height_,    fbytes);
    cudaMalloc(&d_nontraversable_, bbytes);
    cudaMalloc(&d_observed_mask_,  bbytes);
    cudaMalloc(&d_trav_grid_,      fbytes);

    // Pinned host buffer enables true async D2H copy.
    cudaMallocHost(&h_trav_grid_, fbytes);
}

void TraversabilityStage::process(FrameData& frame, cudaStream_t stream) {
    const int cells = r_bins_ * theta_bins_;
    if (cells == 0) return;

    const int N = frame.polar_count;

    const float sentinel = -std::numeric_limits<float>::max();
    const float nan_val  =  std::numeric_limits<float>::quiet_NaN();
    const float inf_val  =  std::numeric_limits<float>::infinity();

    const size_t fbytes = static_cast<size_t>(cells) * sizeof(float);
    const size_t bbytes = static_cast<size_t>(cells) * sizeof(uint8_t);

    constexpr int kThreads1d = 256;
    const dim3 block2d(16, 16);
    const dim3 grid2d(
        (r_bins_     + static_cast<int>(block2d.x) - 1) / static_cast<int>(block2d.x),
        (theta_bins_ + static_cast<int>(block2d.y) - 1) / static_cast<int>(block2d.y));

    // Kernel 0: reset height_map to sentinel; zero observed_mask.
    init_float_kernel<<<(cells + kThreads1d - 1) / kThreads1d, kThreads1d, 0, stream>>>(
        d_height_map_, cells, sentinel);
    cudaMemsetAsync(d_observed_mask_, 0, bbytes, stream);

    // Kernel 1: scatter polar points → height map.
    if (N > 0) {
        build_height_map_kernel<<<(N + kThreads1d - 1) / kThreads1d, kThreads1d, 0, stream>>>(
            frame.polar_points.ptr, d_height_map_, N,
            r_bins_, theta_bins_,
            r_min_, theta_min_,
            1.0f / dr_, 1.0f / dtheta_);
    }

    // Kernel 2: fill terrain and valid-cell mask.
    fill_terrain_kernel<<<grid2d, block2d, 0, stream>>>(
        d_height_map_, d_terrain_, d_valid_mask_,
        r_bins_, theta_bins_, sentinel);

    // Kernel 3: gradient + slope.
    gradient_slope_kernel<<<grid2d, block2d, 0, stream>>>(
        d_terrain_, d_slope_,
        r_bins_, theta_bins_,
        r_min_, dr_, dtheta_,
        scrit_, inf_val);

    // Kernel 4: 3×3 roughness.
    roughness_kernel<<<grid2d, block2d, 0, stream>>>(
        d_terrain_, d_roughness_,
        r_bins_, theta_bins_,
        rcrit_m_, inf_val);

    // Kernel 5: 5×5 step height.
    step_height_kernel<<<grid2d, block2d, 0, stream>>>(
        d_terrain_, d_step_height_,
        r_bins_, theta_bins_,
        r_min_, theta_min_, dr_, dtheta_,
        hcrit_m_, scrit_, inf_val);

    // Kernel 6: danger value + nontraversable mask.
    danger_nontraversable_kernel<<<grid2d, block2d, 0, stream>>>(
        d_slope_, d_roughness_, d_step_height_, d_valid_mask_,
        d_nontraversable_,
        r_bins_, theta_bins_,
        scrit_, rcrit_m_, hcrit_m_, danger_threshold_);

    // Kernel 7: ray-cast (one thread per angular column).
    ray_cast_kernel<<<(theta_bins_ + kThreads1d - 1) / kThreads1d, kThreads1d, 0, stream>>>(
        d_nontraversable_, d_observed_mask_,
        r_bins_, theta_bins_);

    // Kernel 8: assemble trav_grid.
    trav_grid_kernel<<<grid2d, block2d, 0, stream>>>(
        d_observed_mask_, d_nontraversable_,
        d_trav_grid_,
        r_bins_, theta_bins_, nan_val);

    // Async D2H copy into pinned buffer — enqueued on the same stream so
    // the copy begins only after all preceding kernels complete.
    cudaMemcpyAsync(h_trav_grid_, d_trav_grid_, fbytes, cudaMemcpyDeviceToHost, stream);

    // Sync stream to ensure copy is complete before writing frame.result.
    cudaStreamSynchronize(stream);

    std::memcpy(frame.result.trav_grid.data(), h_trav_grid_, fbytes);
}
