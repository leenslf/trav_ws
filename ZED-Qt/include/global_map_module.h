#pragma once

#include <memory>
#include <QObject>

#include "frame_data.h"
#include "global_map/global_map.h"

// Consumes FrameData signals from CommReceiver and fuses each frame's local
// polar traversability grid into a persistent world-frame GlobalMap.
// No GUI output — Phase 4 will add a visualization widget that reads globalMap().
class GlobalMapModule : public QObject {
    Q_OBJECT
public:
    // Local polar grid extent — must match the PolarGridWidget::Config values
    // in use, since both describe the same incoming FrameData bins. Callers
    // should source these from config.yaml (see setGridConfig()).
    struct GridConfig {
        float r_min_m       = 0.3f;
        float dr_m          = 0.10f;
        float theta_min_deg = -45.0f;
        float dtheta_deg    = 1.0f;
    };

    explicit GlobalMapModule(QObject* parent = nullptr);

    // Overrides the local polar grid extent used to interpret incoming
    // FrameData bins. Call before the first frame is received.
    void setGridConfig(const GridConfig& cfg);

    // Valid only after isInitialized() returns true (i.e. at least one fusable
    // frame has been processed).
    const global_map::GlobalMap& globalMap() const;
    bool isInitialized() const;

    // Returns false if no frame has been received yet.
    // yaw is in radians, counter-clockwise from +x_world.
    bool lastCameraPose(float& tx, float& ty, float& yaw) const;

public slots:
    void onFrameReceived(const FrameData& frame);

private:
    GridConfig grid_cfg_;
    std::unique_ptr<global_map::GlobalMap> map_;
    bool  initialized_{false};
    float last_tx_{0.f}, last_ty_{0.f}, last_yaw_{0.f};
    bool  has_pose_{false};
};
