#include "global_map/local_to_world.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using global_map::local_to_world;
using global_map::WorldCell;

static const float kPi = static_cast<float>(M_PI);

static bool near(float a, float b, float tol = 1e-5f) {
    return std::fabs(a - b) <= tol;
}

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n",                \
                         __FILE__, __LINE__, #cond);                 \
            std::exit(1);                                            \
        }                                                            \
    } while (0)

int main() {
    // All four tests use a single 1-bin grid:
    //   r_edges     = {0.5, 1.5}     =>  r     = 1.0 m
    //   theta_edges = {-pi/4, pi/4}  =>  theta = 0 rad  (straight ahead)
    //   => x_cam = 1.0, y_cam = 0.0
    const std::vector<float> re = {0.5f, 1.5f};
    const std::vector<float> te = {-kPi / 4.f, kPi / 4.f};

    // 1. Identity pose: no rotation, no translation.
    //    Expected: x_world = 1.0, y_world = 0.0, z_world = 0.0
    {
        WorldCell c = local_to_world(0, 0, re, te,
                                     /*height_map_value=*/0.f,
                                     /*trav_grid_value=*/0.f,
                                     /*tx=*/0.f, /*ty=*/0.f, /*tz=*/0.f,
                                     /*qx=*/0.f, /*qy=*/0.f, /*qz=*/0.f, /*qw=*/1.f);
        CHECK(near(c.x_world, 1.f));
        CHECK(near(c.y_world, 0.f));
        CHECK(near(c.z_world, 0.f));
        CHECK(near(c.value,   0.f));
        std::puts("PASS: identity pose");
    }

    // 2. Pure translation tx=1, identity rotation.
    //    Expected: x_world = 2.0, y_world = 0.0
    {
        WorldCell c = local_to_world(0, 0, re, te,
                                     /*height_map_value=*/0.f,
                                     /*trav_grid_value=*/1.f,
                                     /*tx=*/1.f, /*ty=*/0.f, /*tz=*/0.f,
                                     /*qx=*/0.f, /*qy=*/0.f, /*qz=*/0.f, /*qw=*/1.f);
        CHECK(near(c.x_world, 2.f));
        CHECK(near(c.y_world, 0.f));
        CHECK(near(c.value,   1.f));
        std::puts("PASS: pure translation");
    }

    // 3. Pure 90-degree yaw, no translation.
    //    qz = sin(pi/4), qw = cos(pi/4) encodes 90 deg around Z.
    //    yaw = atan2(2*qw*qz, 1-2*qz^2) = atan2(1, 0) = pi/2.
    //    Expected: x_world ~ 0, y_world ~ 1
    {
        const float sz = std::sin(kPi / 4.f);
        const float cz = std::cos(kPi / 4.f);
        WorldCell c = local_to_world(0, 0, re, te,
                                     /*height_map_value=*/0.f,
                                     /*trav_grid_value=*/0.f,
                                     /*tx=*/0.f, /*ty=*/0.f, /*tz=*/0.f,
                                     /*qx=*/0.f, /*qy=*/0.f, /*qz=*/sz, /*qw=*/cz);
        CHECK(near(c.x_world, 0.f, 1e-5f));
        CHECK(near(c.y_world, 1.f, 1e-5f));
        std::puts("PASS: pure 90 deg yaw");
    }

    // 4. NaN pass-through: trav_grid_value = NaN.
    //    WorldCell::value must be NaN; x_world/y_world/z_world must be finite.
    {
        const float nan_val = std::numeric_limits<float>::quiet_NaN();
        WorldCell c = local_to_world(0, 0, re, te,
                                     /*height_map_value=*/0.f,
                                     /*trav_grid_value=*/nan_val,
                                     /*tx=*/0.f, /*ty=*/0.f, /*tz=*/0.f,
                                     /*qx=*/0.f, /*qy=*/0.f, /*qz=*/0.f, /*qw=*/1.f);
        CHECK(std::isnan(c.value));
        CHECK(std::isfinite(c.x_world));
        CHECK(std::isfinite(c.y_world));
        CHECK(std::isfinite(c.z_world));
        std::puts("PASS: NaN pass-through");
    }

    std::puts("\nAll tests passed.");
    return 0;
}
