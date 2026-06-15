#include "global_map_module.h"
#include <QCoreApplication>
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

// Build a minimal 1-cell FrameData (nr=1, nt=1) at the given pose.
// height_map is left empty (matches what CommReceiver actually produces).
static FrameData make_frame(float tx, float ty, uint8_t tracking_state, float trav_value)
{
    FrameData f;
    f.seq           = 0;
    f.timestamp_ns  = 0;
    f.nr            = 1;
    f.nt            = 1;
    f.trav_grid.resize(1);
    f.trav_grid[0]  = trav_value;
    // height_map intentionally not resized — tests the fallback path in
    // GlobalMapModule (height_map absent from wire format).
    f.tx = tx;   f.ty = ty;   f.tz = 0.f;
    f.qx = 0.f;  f.qy = 0.f; f.qz = 0.f; f.qw = 1.f;
    f.tracking_state = tracking_state;
    return f;
}

// ── 1. Bad tracking state — map stays uninitialized ──────────────────────────
static void test_skip_bad_tracking_state()
{
    GlobalMapModule module;
    // tracking_state = 4 = UNAVAILABLE
    module.onFrameReceived(make_frame(0.f, 0.f, 4, 0.f));
    CHECK(!module.isInitialized());
    std::puts("PASS: bad tracking state skipped, map not initialized");
}

// ── 2. Good frame initializes map and writes exactly one cell ─────────────────
//
// Grid params: 200×200, resolution=0.10, origin=(-10,-10) (tx=ty=0).
// nr=1, nt=1, identity pose → local_to_world maps to:
//   r_centre   = (0.3+0.4)/2          = 0.35 m
//   theta_centre = (-45 + -33)/2 deg  = -39 deg
//   x_cam = 0.35*cos(-39°) ≈ 0.2720
//   y_cam = 0.35*sin(-39°) ≈ -0.2202
//   yaw=0 → x_world = x_cam, y_world = y_cam
//   ix = floor((0.2720 + 10) / 0.10) = 102
//   iy = floor((-0.2202 + 10) / 0.10) = 97
//   flat idx = 97*200 + 102 = 19502
static void test_basic_write()
{
    GlobalMapModule module;
    module.onFrameReceived(make_frame(0.f, 0.f, /*OK=*/0, /*trav=*/0.f));

    CHECK(module.isInitialized());

    const auto& cells = module.globalMap().cells();
    CHECK(cells.size() == 200u * 200u);
    CHECK(module.globalMap().out_of_bounds_count() == 0);

    // Count non-NaN cells — exactly 1 (the single local grid cell).
    int non_nan = 0;
    for (float v : cells)
        if (!std::isnan(v)) ++non_nan;
    CHECK(non_nan == 1);

    // Verify the specific flat index derived above.
    CHECK(cells[19502] == 0.f);

    std::puts("PASS: single cell written at flat index 19502");
}

// ── 3. Overwrite semantics end-to-end ────────────────────────────────────────
static void test_overwrite_end_to_end()
{
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    GlobalMapModule module;

    // First frame: NaN → 0.0 (traversable)
    module.onFrameReceived(make_frame(0.f, 0.f, 0, 0.f));
    CHECK(module.globalMap().cells()[19502] == 0.f);

    // Second frame, same pose: 0.0 → 1.0 (obstacle)
    module.onFrameReceived(make_frame(0.f, 0.f, 0, 1.f));
    CHECK(module.globalMap().cells()[19502] == 1.f);

    // Third frame, NaN trav_value: must NOT overwrite the known 1.0
    module.onFrameReceived(make_frame(0.f, 0.f, 0, kNaN));
    CHECK(module.globalMap().cells()[19502] == 1.f);

    // Overall: still exactly one non-NaN cell.
    int non_nan = 0;
    for (float v : module.globalMap().cells())
        if (!std::isnan(v)) ++non_nan;
    CHECK(non_nan == 1);

    std::puts("PASS: overwrite semantics end-to-end (0→1, NaN preserves 1)");
}

// ── 4. Out-of-bounds frame is counted, cells unchanged ───────────────────────
static void test_out_of_bounds_after_init()
{
    GlobalMapModule module;
    // Init at (0,0): map spans (-10,-10) to (10,10).
    module.onFrameReceived(make_frame(0.f, 0.f, 0, 0.f));

    // A frame whose robot position is far outside the initial window.
    // The single local cell will project way outside the grid.
    module.onFrameReceived(make_frame(50.f, 50.f, 0, 1.f));

    CHECK(module.globalMap().out_of_bounds_count() >= 1u);
    // The original cell at 19502 must be unchanged.
    CHECK(module.globalMap().cells()[19502] == 0.f);

    std::puts("PASS: out-of-bounds frame counted, in-bounds cells unchanged");
}

int main(int argc, char* argv[])
{
    // QCoreApplication is required for QObject construction.
    QCoreApplication app(argc, argv);

    qRegisterMetaType<FrameData>("FrameData");

    test_skip_bad_tracking_state();
    test_basic_write();
    test_overwrite_end_to_end();
    test_out_of_bounds_after_init();

    std::puts("\nAll smoke tests passed.");
    return 0;
}
