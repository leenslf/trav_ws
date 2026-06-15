#pragma once

#include <vector>

// No Qt, networking, or threading dependencies — pure math module.
// Namespace introduced here because the rest of this codebase uses global scope;
// document this choice when wiring into the integration phase.
namespace global_map {

struct WorldCell {
    float x_world;
    float y_world;
    float z_world;  // z_cam + tz; world-referenced height, may be unused downstream
    float value;    // pass-through of trav_grid value, including NaN
};

// Transform a single polar grid cell (i, j) from the local camera frame into
// the world frame using a yaw-only rotation extracted from the camera quaternion.
// Pitch/roll are deliberately ignored: the local grid is gravity-aligned upstream
// and re-applying them would incorrectly re-tilt it.
//
// r_edges / theta_edges: size bins+1, supplied by the caller (metres / radians).
// height_map_value: passed through as z_cam; not interpreted here.
// trav_grid_value:  passed through as WorldCell::value, including NaN.
// Quaternion convention: Hamilton (ZED SDK), (qx, qy, qz, qw).
WorldCell local_to_world(
    int i, int j,
    const std::vector<float>& r_edges,
    const std::vector<float>& theta_edges,
    float height_map_value,
    float trav_grid_value,
    float tx, float ty, float tz,
    float qx, float qy, float qz, float qw
);

} // namespace global_map
