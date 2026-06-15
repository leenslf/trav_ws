#include "global_map/local_to_world.h"
#include <cmath>

namespace global_map {

WorldCell local_to_world(
    int i, int j,
    const std::vector<float>& r_edges,
    const std::vector<float>& theta_edges,
    float height_map_value,
    float trav_grid_value,
    float tx, float ty, float tz,
    float qx, float qy, float qz, float qw)
{
    const float r     = (r_edges[i]     + r_edges[i + 1])     * 0.5f;
    const float theta = (theta_edges[j] + theta_edges[j + 1]) * 0.5f;

    const float x_cam = r * std::cos(theta);
    const float y_cam = r * std::sin(theta);
    const float z_cam = height_map_value;

    // Yaw-only rotation — matches the formula used in tilt_compensate.cu.
    const float yaw = std::atan2(2.f * (qw * qz + qx * qy),
                                 1.f - 2.f * (qy * qy + qz * qz));
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);

    return {
        x_cam * cy - y_cam * sy + tx,
        x_cam * sy + y_cam * cy + ty,
        z_cam + tz,
        trav_grid_value
    };
}

} // namespace global_map
