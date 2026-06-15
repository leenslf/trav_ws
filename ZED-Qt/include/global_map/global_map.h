#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace global_map {

// Plain data container for the world-frame traversability grid.
// Fusion rule is injected at construction; the container has no opinion on
// which rule is used (overwrite, max-danger, log-odds, etc.).
class GlobalMap {
public:
    using FusionFn = std::function<float(float existing, float new_value)>;

    GlobalMap(int width, int height, float resolution_m,
              float origin_x, float origin_y, FusionFn fuse);

    // Projects (x_world, y_world) to a grid cell and applies fuse_ in-place.
    // If the position is outside the grid, increments out_of_bounds_count_
    // and returns silently — no exception, no log.
    void update_cell(float x_world, float y_world, float value);

    const std::vector<float>& cells() const;
    int      width()              const;
    int      height()             const;
    float    resolution_m()       const;
    float    origin_x()           const;
    float    origin_y()           const;
    uint64_t out_of_bounds_count() const;

private:
    int   width_, height_;
    float resolution_m_;
    float origin_x_, origin_y_;
    std::vector<float> cells_;          // row-major, size width_*height_, NaN = unknown
    FusionFn   fuse_;
    uint64_t   out_of_bounds_count_{0};

    // Returns true and fills ix/iy if (x_world, y_world) maps inside the grid.
    bool world_to_index(float x_world, float y_world, int& ix, int& iy) const;
};

} // namespace global_map
