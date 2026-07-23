// Host-side unit test for the zone-seam neighbour index-mapping math
// (map_theta_to_row / neighbor_flat_idx / build_row_tables, declared in
// traversability/stages/traversability.hpp). Pure index arithmetic — no
// kernel launch, no CUDA execution — so it exercises exactly the seam
// lookup used by the roughness/step-height/gradient/ray-cast kernels
// without needing a GPU.
#include "traversability/stages/traversability.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n",                \
                         __FILE__, __LINE__, #cond);                 \
            std::exit(1);                                            \
        }                                                            \
    } while (0)

static bool near(float a, float b, float tol = 1e-4f) {
    return std::fabs(a - b) <= tol;
}

int main() {
    // Same-width rows (same zone): fast path returns the index unchanged,
    // regardless of dtheta values (they're irrelevant when n_other==n_self).
    {
        CHECK(map_theta_to_row(3, 5.0f, 10, 5.0f, 10) == 3);
        CHECK(map_theta_to_row(0, 123.0f, 4, 999.0f, 4) == 0);
    }

    // Coarse row (2 bins over a 90deg span -> 45deg/bin) vs a fine
    // neighbour row (4 bins -> 22.5deg/bin) covering the same span.
    const float coarse_dtheta = 45.0f;
    const float fine_dtheta   = 22.5f;

    // Coarse bin 0 spans [0,45), centre 22.5deg -- exactly on the boundary
    // between fine bins 0 ([0,22.5), centre 11.25) and 1 ([22.5,45), centre
    // 33.75) -- a tie, which lround (round-half-away-from-zero) breaks by
    // rounding up to fine bin 1.
    {
        const int j = map_theta_to_row(0, coarse_dtheta, 2, fine_dtheta, 4);
        CHECK(j == 1);
    }

    // Coarse bin 1 (centre 67.5deg) is unambiguously closest to fine bin 3
    // (centre 78.75deg) over fine bin 2 (centre 56.25deg): |67.5-78.75|=11.25
    // vs |67.5-56.25|=11.25 -- also a tie, resolved the same way (rounds up).
    {
        const int j = map_theta_to_row(1, coarse_dtheta, 2, fine_dtheta, 4);
        CHECK(j == 3);
    }

    // Fine -> coarse, unambiguous (non-tied) cases: fine bin 0 (centre
    // 11.25deg) is clearly closest to coarse bin 0 (centre 22.5, |11.25|)
    // over coarse bin 1 (centre 67.5, |56.25|).
    {
        const int j = map_theta_to_row(0, fine_dtheta, 4, coarse_dtheta, 2);
        CHECK(j == 0);
    }
    // Fine bin 3 (centre 78.75deg) is clearly closest to coarse bin 1
    // (centre 67.5, |11.25|) over coarse bin 0 (centre 22.5, |56.25|).
    {
        const int j = map_theta_to_row(3, fine_dtheta, 4, coarse_dtheta, 2);
        CHECK(j == 1);
    }

    // Round-trip sanity: mapping a bin's centre angle into a neighbouring
    // zone and back should never move by more than one bin-width of
    // either row -- this is the property the seam lookup actually needs
    // (angularly *closest*, not exact), constructed directly rather than
    // asserting exact indices for every case.
    for (int j1 = 0; j1 < 2; ++j1) {
        const int j2 = map_theta_to_row(j1, coarse_dtheta, 2, fine_dtheta, 4);
        const float c1 = (j1 + 0.5f) * coarse_dtheta;
        const float c2 = (j2 + 0.5f) * fine_dtheta;
        CHECK(std::fabs(c1 - c2) <= coarse_dtheta * 0.5f + fine_dtheta * 0.5f);
    }

    // Extreme clamp: an index near the top of a wide row, mapped into a
    // 1-bin row, must clamp into range rather than overshoot.
    {
        const int j = map_theta_to_row(9, 9.0f, 10, 90.0f, 1);
        CHECK(j == 0);
    }

    // --- neighbor_flat_idx + build_row_tables, exercised together on a
    // two-zone grid: zone0 rows [0,2) with 2 theta bins (45deg), zone1
    // rows [2,4) with 4 theta bins (22.5deg). ---
    {
        std::vector<PolarZone> zones = {
            PolarZone{0, 2, 2, 45.0f},
            PolarZone{2, 4, 4, 22.5f},
        };
        const int r_bins = 4;
        RowTables t = build_row_tables(zones, r_bins);
        CHECK(t.total_cells == 2 * 2 + 2 * 4); // 12
        CHECK(t.max_theta_bins == 4);
        CHECK(t.row_offset[0] == 0);
        CHECK(t.row_offset[1] == 2);
        CHECK(t.row_offset[2] == 4);
        CHECK(t.row_offset[3] == 8);
        CHECK(t.row_theta_bins[0] == 2 && t.row_theta_bins[1] == 2);
        CHECK(t.row_theta_bins[2] == 4 && t.row_theta_bins[3] == 4);
        CHECK(near(t.row_dtheta[0], 45.0f * 3.14159265f / 180.0f));
        CHECK(near(t.row_dtheta[2], 22.5f * 3.14159265f / 180.0f));

        // Same-zone neighbour (row0 -> row1, both width 2): fast path, cj
        // unchanged from the reflected same-row column.
        {
            const int idx = neighbor_flat_idx(0, 1, 1, 0, r_bins,
                t.row_offset.data(), t.row_theta_bins.data(), t.row_dtheta.data());
            CHECK(idx == t.row_offset[1] + 1); // di=+1 -> ri=1, dj=0 -> cj=1
        }

        // Cross-zone neighbour (row1 -> row2, widths 2 vs 4): must land in
        // row2's own range [row_offset[2], row_offset[2]+4), not reuse the
        // row1-frame column index verbatim (which would be out of range
        // for meaning, though not out of bounds, in row2's wider frame).
        {
            const int idx = neighbor_flat_idx(1, 0, 1, 0, r_bins,
                t.row_offset.data(), t.row_theta_bins.data(), t.row_dtheta.data());
            CHECK(idx >= t.row_offset[2] && idx < t.row_offset[2] + 4);
            // j=0 in row1 (centre 22.5deg, coarse) maps to fine bin 1 (tie, rounds up).
            CHECK(idx == t.row_offset[2] + 1);
        }
    }

    std::printf("test_seam_neighbor: all checks passed\n");
    return 0;
}
