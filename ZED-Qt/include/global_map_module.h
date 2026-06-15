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
    explicit GlobalMapModule(QObject* parent = nullptr);

    // Valid only after isInitialized() returns true (i.e. at least one fusable
    // frame has been processed).
    const global_map::GlobalMap& globalMap() const;
    bool isInitialized() const;

public slots:
    void onFrameReceived(const FrameData& frame);

private:
    std::unique_ptr<global_map::GlobalMap> map_;
    bool initialized_{false};
};
