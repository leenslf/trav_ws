#include "global_map/global_map.h"
#include "global_map/fusion_rules.h"
#include "traversability/frame_result.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d  %s\n",                  \
                         __FILE__, __LINE__, #cond);                   \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

// ── helpers ──────────────────────────────────────────────────────────────────

static const float kNaN = std::numeric_limits<float>::quiet_NaN();

static bool near(float a, float b, float tol = 1e-6f) {
    return std::fabs(a - b) <= tol;
}

// ── 1. fusion::overwrite truth table ─────────────────────────────────────────

static void test_overwrite() {
    CHECK(std::isnan(fusion::overwrite(kNaN, kNaN)));   // NaN , NaN  -> NaN
    CHECK(near(fusion::overwrite(kNaN,  0.f),  0.f));   // NaN , 0    -> 0
    CHECK(near(fusion::overwrite(kNaN,  1.f),  1.f));   // NaN , 1    -> 1
    CHECK(near(fusion::overwrite(1.f,  kNaN),  1.f));   // 1   , NaN  -> 1  (preserve)
    CHECK(near(fusion::overwrite(0.f,  kNaN),  0.f));   // 0   , NaN  -> 0  (preserve)
    CHECK(near(fusion::overwrite(0.f,   1.f),  1.f));   // 0   , 1    -> 1  (overwrite)
    CHECK(near(fusion::overwrite(1.f,   0.f),  0.f));   // 1   , 0    -> 0  (overwrite)
    std::puts("PASS: fusion::overwrite truth table");
}

// ── 2. is_fusable ─────────────────────────────────────────────────────────────

static void test_is_fusable() {
    CHECK( fusion::is_fusable(TrackingState::OK));
    CHECK(!fusion::is_fusable(TrackingState::SEARCHING));
    CHECK(!fusion::is_fusable(TrackingState::FPS_TOO_LOW));
    CHECK(!fusion::is_fusable(TrackingState::SEARCHING_FLOOR_PLANE));
    CHECK(!fusion::is_fusable(TrackingState::UNAVAILABLE));
    CHECK( fusion::is_fusable(TrackingState::LOOP_CLOSED));
    std::puts("PASS: is_fusable");
}

// ── shared grid helper ────────────────────────────────────────────────────────
// 10x10, resolution 0.1 m, origin (-0.5, -0.5).
// World (0.0, 0.0) maps to:
//   ix = floor((0.0 - (-0.5)) / 0.1) = floor(5.0) = 5
//   iy = 5
//   flat idx = iy*width + ix = 5*10 + 5 = 55

static global_map::GlobalMap make_grid(global_map::GlobalMap::FusionFn fn) {
    return global_map::GlobalMap(/*width=*/10, /*height=*/10,
                                 /*resolution_m=*/0.1f,
                                 /*origin_x=*/-0.5f, /*origin_y=*/-0.5f,
                                 std::move(fn));
}

// ── 3. basic placement ────────────────────────────────────────────────────────

static void test_basic_placement() {
    // Simple "replace" fusion to isolate placement from overwrite semantics.
    auto grid = make_grid([](float /*existing*/, float n) { return n; });

    grid.update_cell(0.f, 0.f, 0.75f);

    CHECK(near(grid.cells()[55], 0.75f));
    CHECK(grid.out_of_bounds_count() == 0);
    std::puts("PASS: basic placement (ix=5, iy=5, idx=55)");
}

// ── 4. out-of-bounds ─────────────────────────────────────────────────────────

static void test_out_of_bounds() {
    auto grid = make_grid([](float /*e*/, float n) { return n; });

    grid.update_cell(100.f, 100.f, 1.f);   // clearly outside
    grid.update_cell(-10.f,  -10.f, 1.f);  // negative OOB

    CHECK(grid.out_of_bounds_count() == 2);

    // All cells must still be NaN (nothing was written).
    for (float v : grid.cells()) {
        CHECK(std::isnan(v));
    }
    std::puts("PASS: out-of-bounds (count=2, cells untouched)");
}

// ── 5. end-to-end with fusion::overwrite ─────────────────────────────────────

static void test_overwrite_end_to_end() {
    auto grid = make_grid(fusion::overwrite);

    // First write: NaN -> 0.0 (overwrite kicks in)
    grid.update_cell(0.f, 0.f, 0.f);
    CHECK(near(grid.cells()[55], 0.f));

    // Second write: 0.0 -> 1.0 (overwrite)
    grid.update_cell(0.f, 0.f, 1.f);
    CHECK(near(grid.cells()[55], 1.f));

    // Third write: NaN new_value must NOT overwrite existing 1.0
    grid.update_cell(0.f, 0.f, kNaN);
    CHECK(near(grid.cells()[55], 1.f));

    CHECK(grid.out_of_bounds_count() == 0);
    std::puts("PASS: end-to-end with fusion::overwrite");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_overwrite();
    test_is_fusable();
    test_basic_placement();
    test_out_of_bounds();
    test_overwrite_end_to_end();

    std::puts("\nAll tests passed.");
    return 0;
}
