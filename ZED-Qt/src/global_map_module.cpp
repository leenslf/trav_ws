#include "global_map_module.h"

#include "global_map/bin_edges.h"
#include "global_map/fusion_rules.h"
#include "global_map/local_to_world.h"
#include "traversability/frame_result.hpp"

namespace {

// Grid extent constants — must stay in sync with PolarGridWidget::Config defaults.
// If those defaults ever change, update these too (or move them to a shared header).
constexpr float kRMinM       =  0.3f;
constexpr float kDrM         =  0.10f;   // polar_grid_size_r_m
constexpr float kThetaMinDeg = -45.0f;
constexpr float kDthetaDeg   =  12.0f;   // polar_grid_size_theta_deg

// V1 global map: fixed 20 m × 20 m (±10 m around first fusable pose), non-growable.
// Cells that fall outside the initial window are silently counted via
// GlobalMap::out_of_bounds_count().  This is a known v1 limitation.
constexpr float kMapHalfExtent = 10.0f;                                      // metres
constexpr int   kMapCells = static_cast<int>(2.0f * kMapHalfExtent / kDrM); // 200

} // namespace

GlobalMapModule::GlobalMapModule(QObject* parent)
    : QObject(parent)
{}

bool GlobalMapModule::isInitialized() const
{
    return initialized_;
}

const global_map::GlobalMap& GlobalMapModule::globalMap() const
{
    // Caller must ensure isInitialized() is true before calling this.
    return *map_;
}

void GlobalMapModule::onFrameReceived(const FrameData& frame)
{
    // Gate: skip frames whose pose is unreliable.
    if (!fusion::is_fusable(static_cast<TrackingState>(frame.tracking_state)))
        return;

    // Lazy init: use the first fusable frame's (tx, ty) as the map centre.
    if (!initialized_) {
        const float ox = frame.tx - kMapHalfExtent;
        const float oy = frame.ty - kMapHalfExtent;
        map_ = std::make_unique<global_map::GlobalMap>(
            kMapCells, kMapCells, kDrM, ox, oy, fusion::overwrite);
        initialized_ = true;
    }

    const auto r_edges     = make_r_edges(frame.nr, kRMinM, kDrM);
    const auto theta_edges = make_theta_edges(frame.nt, kThetaMinDeg, kDthetaDeg);

    for (int i = 0; i < frame.nr; ++i) {
        for (int j = 0; j < frame.nt; ++j) {
            // TODO(phase3-height): FrameBundle carries no height_map field, so
            // frame.height_map is always empty in the current wire format.
            // z_world will equal tz until height_map is added to FrameBundle
            // and CommReceiver populates this field.  See Phase 3 summary.
            const float h = (frame.height_map.size() == frame.nr * frame.nt)
                                ? frame.height_map[i * frame.nt + j]
                                : 0.0f;

            const global_map::WorldCell wc = global_map::local_to_world(
                i, j, r_edges, theta_edges,
                h,
                frame.trav_grid[i * frame.nt + j],
                frame.tx, frame.ty, frame.tz,
                frame.qx, frame.qy, frame.qz, frame.qw);

            map_->update_cell(wc.x_world, wc.y_world, wc.value);
        }
    }
}
