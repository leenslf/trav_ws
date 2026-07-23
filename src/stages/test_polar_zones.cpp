#include "traversability/stages/traversability.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static bool near(float a, float b, float tol = 1e-4f) {
    return std::fabs(a - b) <= tol;
}

// Mirrors compute_polar_zones' internal radial bin count exactly (same
// arange loop), so tests agree with the function under test even where
// float accumulation drifts from a naive round((max-min)/step).
static int expected_r_bins(float start, float stop, float step) {
    int edges = 0;
    for (float x = start; x < stop + step; x += step) ++edges;
    const int bins = edges - 1;
    return bins < 0 ? 0 : bins;
}

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n",                \
                         __FILE__, __LINE__, #cond);                 \
            std::exit(1);                                            \
        }                                                            \
    } while (0)

// Zones must partition [0, r_bins) exactly: contiguous, no gaps, no overlaps.
static void check_partition(const std::vector<PolarZone>& zones, int r_bins) {
    CHECK(!zones.empty());
    CHECK(zones.front().r_start_idx == 0);
    CHECK(zones.back().r_end_idx == r_bins);
    for (size_t k = 0; k + 1 < zones.size(); ++k) {
        CHECK(zones[k].r_end_idx == zones[k + 1].r_start_idx);
    }
    for (const auto& z : zones) {
        CHECK(z.r_start_idx < z.r_end_idx);
        CHECK(z.theta_bin_count >= 1);
    }
}

int main() {
    // Empty range (r_max <= r_min) yields zero radial bins and no zones.
    {
        PolarZoneParams p;
        p.r_min_m = 1.0f;
        p.r_max_m = 1.0f;
        p.polar_grid_size_r_m = 0.1f;
        p.theta_min_deg = -45.0f;
        p.theta_max_deg = 45.0f;
        p.theta_target_arc_width_m = 0.1f;

        const auto zones = compute_polar_zones(p);
        CHECK(zones.empty());
    }

    // Very large target arc width -> every bin wants 1 theta bin -> single zone.
    {
        PolarZoneParams p;
        p.r_min_m = 0.1f;
        p.r_max_m = 3.0f;
        p.polar_grid_size_r_m = 0.1f;
        p.theta_min_deg = -45.0f;
        p.theta_max_deg = 45.0f;
        p.theta_target_arc_width_m = 1000.0f;

        const auto zones = compute_polar_zones(p);
        const int r_bins = expected_r_bins(p.r_min_m, p.r_max_m, p.polar_grid_size_r_m);
        check_partition(zones, r_bins);
        CHECK(zones.size() == 1);
        CHECK(zones[0].theta_bin_count == 1);
        CHECK(near(zones[0].theta_bin_deg, 90.0f));
    }

    // Realistic config: r in [0.1, 3.0] step 0.1, theta in [-45, 45], target 0.1 m.
    // theta_bin_count must grow with radius, and every zone's actual bin arc
    // width (theta_bin_deg * r_center) must stay close to the 0.1 m target.
    {
        PolarZoneParams p;
        p.r_min_m = 0.1f;
        p.r_max_m = 3.0f;
        p.polar_grid_size_r_m = 0.1f;
        p.theta_min_deg = -45.0f;
        p.theta_max_deg = 45.0f;
        p.theta_target_arc_width_m = 0.1f;

        const auto zones = compute_polar_zones(p);
        const int r_bins = expected_r_bins(p.r_min_m, p.r_max_m, p.polar_grid_size_r_m);
        check_partition(zones, r_bins);
        CHECK(zones.size() > 1); // must actually be zoned, not collapse to one bin size

        int prev_n = 0;
        for (const auto& z : zones) {
            CHECK(z.theta_bin_count >= prev_n); // non-decreasing: more bins as r grows
            prev_n = z.theta_bin_count;

            const float r_start_center = p.r_min_m + (static_cast<float>(z.r_start_idx) + 0.5f) * p.polar_grid_size_r_m;
            const float r_end_center   = p.r_min_m + (static_cast<float>(z.r_end_idx - 1) + 0.5f) * p.polar_grid_size_r_m;
            const float arc_start = z.theta_bin_deg * (3.14159265358979323846f / 180.0f) * r_start_center;
            const float arc_end   = z.theta_bin_deg * (3.14159265358979323846f / 180.0f) * r_end_center;

            // Within a zone, actual arc width should stay within ~50% of target —
            // zones are only re-cut when the ideal bin count changes, so drift
            // within a zone is bounded by how much r_center moves across it.
            CHECK(arc_start > 0.5f * p.theta_target_arc_width_m);
            CHECK(arc_end   < 1.5f * p.theta_target_arc_width_m);
        }
    }

    // Very small target arc width -> ideal_n differs almost every bin ->
    // zones should still partition cleanly even in the near-degenerate case.
    {
        PolarZoneParams p;
        p.r_min_m = 0.1f;
        p.r_max_m = 3.0f;
        p.polar_grid_size_r_m = 0.1f;
        p.theta_min_deg = -45.0f;
        p.theta_max_deg = 45.0f;
        p.theta_target_arc_width_m = 0.005f;

        const auto zones = compute_polar_zones(p);
        const int r_bins = expected_r_bins(p.r_min_m, p.r_max_m, p.polar_grid_size_r_m);
        check_partition(zones, r_bins);
    }

    std::printf("test_polar_zones: all checks passed\n");
    return 0;
}
