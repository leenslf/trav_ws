#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include "traversability/pipeline_stage.hpp"

// Portable host+device qualifier: expands to the real CUDA qualifiers when
// compiled by nvcc (traversability.cu), and to nothing under a plain host
// compiler (e.g. a unit test built with g++) so the same index-mapping math
// is usable — and testable — from both.
#ifdef __CUDACC__
#define TRAV_HD __host__ __device__
#else
#define TRAV_HD
#endif

// ---------------------------------------------------------------------------
// Zone-based angular binning
//
// A fixed angular bin size produces wildly non-uniform physical cell widths
// (r * delta_theta) across the grid. Zones split the radial axis into runs
// of bins that each get their own angular bin size, chosen so that every
// zone's bins have roughly the same physical arc width.
//
// Pure, host-side, dependency-free (no CUDA, no I/O) so it can be
// unit-tested in isolation.
// ---------------------------------------------------------------------------

// Inputs for zone computation. Angles in degrees, lengths in metres —
// mirrors TraversabilityConfig's units so callers can pass those fields
// through directly.
//
// theta_target_arc_width_m controls zone count: since the ideal bin count
// grows roughly (theta_span_rad / theta_target_arc_width_m) per radial bin,
// setting it equal to polar_grid_size_r_m tends to produce close to one
// zone per radial bin (no meaningful grouping) whenever the angular span
// is wide relative to the radial bin size. Use a few times
// polar_grid_size_r_m (e.g. 3-5x) to keep the zone count small.
struct PolarZoneParams {
    float r_min_m{0.f};
    float r_max_m{0.f};
    float polar_grid_size_r_m{0.f};
    float theta_min_deg{0.f};
    float theta_max_deg{0.f};
    float theta_target_arc_width_m{0.f};
};

// One radial run of bins that share a single angular bin size.
struct PolarZone {
    int   r_start_idx{0};     // inclusive, index into the radial bin axis
    int   r_end_idx{0};       // exclusive
    int   theta_bin_count{0};
    float theta_bin_deg{0.f};
};

namespace polar_zones_detail {

constexpr float kPi = 3.14159265358979323846f;

// Mirrors the arange-based edge/bin-count logic used for the radial axis
// in TraversabilityStage::init: iterate while x < stop, edges -> bins.
inline int compute_bin_count(float start, float stop, float step) {
    if (step <= 0.0f) return 0;
    int edges = 0;
    for (float x = start; x < stop + step; x += step) ++edges;
    const int bins = edges - 1;
    return bins < 0 ? 0 : bins;
}

inline int ideal_theta_bin_count(float theta_span_deg, float r_center, float target_arc_width_m) {
    if (target_arc_width_m <= 0.0f) return 1;
    const float theta_span_rad = theta_span_deg * kPi / 180.0f;
    const int   n = static_cast<int>(std::lround(theta_span_rad * r_center / target_arc_width_m));
    return n < 1 ? 1 : n;
}

} // namespace polar_zones_detail

// Computes piecewise-uniform angular-binning zones over the radial bins
// implied by [r_min_m, r_max_m) stepped by polar_grid_size_r_m, so that
// every zone's angular bins have a physical arc width close to
// theta_target_arc_width_m at that zone's radius.
//
// Returns zones in increasing radial order, covering every radial bin
// exactly once with no gaps or overlaps.
inline std::vector<PolarZone> compute_polar_zones(const PolarZoneParams& p) {
    using namespace polar_zones_detail;

    std::vector<PolarZone> zones;

    const int r_bins = compute_bin_count(p.r_min_m, p.r_max_m, p.polar_grid_size_r_m);
    if (r_bins == 0) return zones;

    const float theta_span_deg = p.theta_max_deg - p.theta_min_deg;

    int zone_start = 0;
    int zone_n     = ideal_theta_bin_count(
        theta_span_deg,
        p.r_min_m + 0.5f * p.polar_grid_size_r_m,
        p.theta_target_arc_width_m);

    for (int i = 1; i <= r_bins; ++i) {
        int n = 0;
        if (i < r_bins) {
            const float r_center = p.r_min_m + (static_cast<float>(i) + 0.5f) * p.polar_grid_size_r_m;
            n = ideal_theta_bin_count(theta_span_deg, r_center, p.theta_target_arc_width_m);
        }
        if (i == r_bins || n != zone_n) {
            PolarZone z;
            z.r_start_idx     = zone_start;
            z.r_end_idx       = i;
            z.theta_bin_count = zone_n;
            z.theta_bin_deg   = theta_span_deg / static_cast<float>(zone_n);
            zones.push_back(z);

            zone_start = i;
            zone_n     = n;
        }
    }

    return zones;
}

// ---------------------------------------------------------------------------
// Zone-seam neighbour mapping
//
// Within a zone, radial/angular neighbour cells are found by ordinary
// index-offset arithmetic (unchanged from the pre-zoning grid). At a zone
// seam — a neighbour row belonging to a different zone, hence a different
// theta_bin_count — the same column index no longer refers to the same
// physical bearing, so the neighbour must instead be picked by nearest
// bin-centre angle. TRAV_HD (host+device) so the mapping math is unit
// testable on the host without launching a kernel.
// ---------------------------------------------------------------------------

TRAV_HD inline int reflect_index(int idx, int size) {
    if (size <= 1) return 0;
    if (idx < 0)     return -idx - 1;
    if (idx >= size) return 2 * size - idx - 1;
    return idx;
}

// Maps bin index j_self (in a row of width n_self, bin size dtheta_self) to
// the index of the closest bin, by bin-centre angle, in a row of width
// n_other/dtheta_other. Both rows span the same [theta_min, theta_max], so
// theta_min cancels and isn't needed. Fast path: identical width (same
// zone) returns j_self unchanged — this is the common case and costs one
// int compare, so ordinary same-zone neighbour lookups pay nothing extra.
TRAV_HD inline int map_theta_to_row(
    int j_self, float dtheta_self, int n_self,
    float dtheta_other, int n_other)
{
    if (n_other == n_self) return j_self;
    const float theta_c = (static_cast<float>(j_self) + 0.5f) * dtheta_self;
    const float raw      = theta_c / dtheta_other - 0.5f;
#ifdef __CUDA_ARCH__
    int j = static_cast<int>(lroundf(raw));
#else
    int j = static_cast<int>(std::lround(raw));
#endif
    if (j < 0)       j = 0;
    if (j >= n_other) j = n_other - 1;
    return j;
}

// Flat index of the (di, dj) neighbour of cell (i, j) in the ragged,
// per-zone-dense grid described by row_offset/row_theta_bins/row_dtheta
// (one entry per radial bin i, populated by TraversabilityStage::init from
// its zone table — see build_row_tables below). di is reflected across the
// grid's radial extent (nr) exactly as before zoning; dj is first reflected
// within row i's own width (matching the pre-zoning column-offset
// semantics), then, only if the neighbour row belongs to a different zone,
// remapped to that row's closest bin by angle.
TRAV_HD inline int neighbor_flat_idx(
    int i, int j, int di, int dj, int nr,
    const int* row_offset, const int* row_theta_bins, const float* row_dtheta)
{
    const int ri     = reflect_index(i + di, nr);
    const int n_self  = row_theta_bins[i];
    const int j_self  = reflect_index(j + dj, n_self);
    const int n_ri    = row_theta_bins[ri];
    const int cj      = map_theta_to_row(j_self, row_dtheta[i], n_self, row_dtheta[ri], n_ri);
    return row_offset[ri] + cj;
}

// Builds the per-radial-bin lookup tables kernels use to address the ragged
// grid: for radial bin i, row_offset[i] is where its (dense, zone-width)
// row starts in the flat cell buffer, row_theta_bins[i] is its zone's
// angular bin count, and row_dtheta[i] its zone's angular bin width
// (radians). total_cells is the flat buffer size; max_theta_bins is the
// widest zone, used to size 2D kernel launches.
struct RowTables {
    std::vector<int>   row_offset;
    std::vector<int>   row_theta_bins;
    std::vector<float> row_dtheta;
    int total_cells{0};
    int max_theta_bins{0};
};

inline RowTables build_row_tables(const std::vector<PolarZone>& zones, int r_bins) {
    RowTables t;
    t.row_offset.resize(r_bins);
    t.row_theta_bins.resize(r_bins);
    t.row_dtheta.resize(r_bins);

    int offset = 0;
    for (const auto& z : zones) {
        const float dtheta_rad = z.theta_bin_deg * polar_zones_detail::kPi / 180.0f;
        if (z.theta_bin_count > t.max_theta_bins) t.max_theta_bins = z.theta_bin_count;
        for (int i = z.r_start_idx; i < z.r_end_idx; ++i) {
            t.row_offset[i]     = offset;
            t.row_theta_bins[i] = z.theta_bin_count;
            t.row_dtheta[i]     = dtheta_rad;
            offset += z.theta_bin_count;
        }
    }
    t.total_cells = offset;
    return t;
}

class TraversabilityStage : public IPipelineStage {
public:
    ~TraversabilityStage();
    void init(const PipelineConfig& cfg, FrameData& frame) override;
    void process(FrameData& frame, cudaStream_t stream) override;
    std::string_view name() const noexcept override { return "Traversability"; }

private:
    // Baked config
    float r_min_{0.f}, theta_min_{0.f};
    float dr_{0.f};
    float scrit_{0.f}, rcrit_m_{0.f}, hcrit_m_{0.f};
    float danger_threshold_{0.f};
    int   r_bins_{0};

    // Zone table (host) and the per-radial-bin lookup tables derived from it
    // (see build_row_tables). Fixed for all frames once init() runs, same as
    // r_bins_ — recomputed only if config changes.
    std::vector<PolarZone> zones_;
    RowTables               row_tables_;

    // Device copies of row_tables_'s arrays — every kernel that needs
    // per-row shape (bin count, offset, angular width) reads these instead
    // of a single global theta_bins/dtheta.
    int*   d_row_offset_{nullptr};
    int*   d_row_theta_bins_{nullptr};
    float* d_row_dtheta_{nullptr};

    // Ray-cast horizon ping-pong buffers: one bool-per-bearing "blocked"
    // array, sized to the widest zone, reused across the per-zone ray-cast
    // kernel launches in process() (see the comment there for why ray-cast
    // is launched per zone instead of as one kernel).
    uint8_t* d_ray_blocked_a_{nullptr};
    uint8_t* d_ray_blocked_b_{nullptr};

    // Pre-allocated device scratch + output buffers — sized to
    // row_tables_.total_cells (the ragged grid's flat cell count), not
    // r_bins_ * theta_bins_.
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
};
