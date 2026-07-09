#include "global_map_module.h"

#include "global_map/bin_edges.h"
#include "global_map/fusion_rules.h"
#include "global_map/local_to_world.h"
#include "traversability/frame_result.hpp"

namespace {

// V1 global map: fixed 10 m × 10 m (±5 m around first fusable pose), non-growable.
// Cells that fall outside the initial window are silently counted via
// GlobalMap::out_of_bounds_count().  This is a known v1 limitation.
// TODO: hardcoded for now (10m x 10m). Make configurable (e.g. via
// PipelineConfig / config.yaml) once the global map size needs to vary.
constexpr float kMapHalfExtent = 5.0f;   // metres
constexpr float kMapResM        = 0.25f;  // global-map cell size (independent of kDrM)
constexpr int   kMapCells = static_cast<int>(2.0f * kMapHalfExtent / kMapResM); // 40

} // namespace

GlobalMapModule::GlobalMapModule(QObject* parent)
    : QObject(parent)
{}

void GlobalMapModule::setGridConfig(const GridConfig& cfg)
{
    grid_cfg_ = cfg;
}

bool GlobalMapModule::isInitialized() const
{
    return initialized_;
}

const global_map::GlobalMap& GlobalMapModule::globalMap() const
{
    // Caller must ensure isInitialized() is true before calling this.
    return *map_;
}

bool GlobalMapModule::lastCameraPose(float& tx, float& ty, float& yaw) const
{
    if (!has_pose_) return false;
    tx  = last_tx_;
    ty  = last_ty_;
    yaw = last_yaw_;
    return true;
}

void GlobalMapModule::onFrameReceived(const FrameData& frame)
{
    last_tx_  = frame.tx;
    last_ty_  = frame.ty;
    last_yaw_ = std::atan2(2.f * (frame.qw * frame.qz + frame.qx * frame.qy),
                           1.f - 2.f * (frame.qy * frame.qy + frame.qz * frame.qz));
    has_pose_ = true;

    // Gate: skip frames whose pose is unreliable.
    if (!fusion::is_fusable(static_cast<TrackingState>(frame.tracking_state)))
        return;

    // Lazy init: use the first fusable frame's (tx, ty) as the map centre.
    if (!initialized_) {
        const float ox = frame.tx - kMapHalfExtent;
        const float oy = frame.ty - kMapHalfExtent;
        map_ = std::make_unique<global_map::GlobalMap>(
            kMapCells, kMapCells, kMapResM, ox, oy, fusion::overwrite);
        initialized_ = true;
    }

    const auto r_edges     = make_r_edges(frame.nr, grid_cfg_.r_min_m, grid_cfg_.dr_m);
    const auto theta_edges = make_theta_edges(frame.nt, grid_cfg_.theta_min_deg, grid_cfg_.dtheta_deg);

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
