#include "global_map/global_map.h"
#include <cmath>
#include <limits>

namespace global_map {

GlobalMap::GlobalMap(int width, int height, float resolution_m,
                     float origin_x, float origin_y, FusionFn fuse)
    : width_(width)
    , height_(height)
    , resolution_m_(resolution_m)
    , origin_x_(origin_x)
    , origin_y_(origin_y)
    , cells_(static_cast<std::size_t>(width * height),
             std::numeric_limits<float>::quiet_NaN())
    , fuse_(std::move(fuse))
{}

bool GlobalMap::world_to_index(float x_world, float y_world, int& ix, int& iy) const {
    ix = static_cast<int>(std::floor((x_world - origin_x_) / resolution_m_));
    iy = static_cast<int>(std::floor((y_world - origin_y_) / resolution_m_));
    return ix >= 0 && ix < width_ && iy >= 0 && iy < height_;
}

void GlobalMap::update_cell(float x_world, float y_world, float value) {
    int ix, iy;
    if (!world_to_index(x_world, y_world, ix, iy)) {
        ++out_of_bounds_count_;
        return;
    }
    const int idx = iy * width_ + ix;
    cells_[idx] = fuse_(cells_[idx], value);
}

const std::vector<float>& GlobalMap::cells()             const { return cells_; }
int                        GlobalMap::width()             const { return width_; }
int                        GlobalMap::height()            const { return height_; }
uint64_t                   GlobalMap::out_of_bounds_count() const { return out_of_bounds_count_; }

} // namespace global_map
