#pragma once

#include <QByteArray>
#include <QObject>
#include <atomic>
#include <cstdint>
#include <thread>
#include "frame_data.h"

// Wire format for mailbox 200. Fixed-size POD; received via getStruct.
// MUST stay byte-identical with the copy in include/traversability/consumers/comm_sender.hpp.
struct FrameBundle {
    static const int MAX_R = 20;   // maximum r_bins supported
    static const int MAX_T = 20;   // maximum theta_bins supported

    uint64_t timestamp_ns;                  // frame capture time
    float    tx, ty, tz;                    // camera translation (metres)
    float    qx, qy, qz, qw;               // camera orientation quaternion
    uint8_t  tracking_state;               // ZED TrackingState cast to uint8_t
    // 3 bytes implicit padding before int32_t
    int32_t  r_bins;                        // actual r dimension this run (≤ MAX_R)
    int32_t  theta_bins;                    // actual theta dimension this run (≤ MAX_T)
    uint8_t  cells[MAX_R][MAX_T];          // quantized trav grid: 0=free 1=obstacle 2=unknown
                                            // only [0:r_bins, 0:theta_bins] is valid
};
static_assert(sizeof(FrameBundle) == 448, "FrameBundle size mismatch — check comm_sender.hpp copy");

// mailbox 202 retired — pose is now bundled into FrameBundle on mailbox 200
static constexpr int MAP_MAILBOX_ID       = 200;
static constexpr int IMAGE_MAILBOX_ID     = 201;
static constexpr int IMAGE_MAX_SIZE_BYTES = 32768;

// Local portal port. Not the libcomm default (5000) — that collides with
// a local robot process's RHexAPI control port when both run on the same
// machine. MUST stay in sync with TRAVMAP_REMOTE_PORT in comm_sender.hpp.
static constexpr int MAP_LOCAL_PORT       = 6000;

class CommManager;
class Mailbox;

class CommReceiver : public QObject {
    Q_OBJECT
public:
    explicit CommReceiver(QObject* parent = nullptr);
    ~CommReceiver() override;

    CommReceiver(const CommReceiver&)            = delete;
    CommReceiver& operator=(const CommReceiver&) = delete;

signals:
    void frameReceived(const FrameData& frame);
    void imageReceived(const QByteArray& jpeg);

private:
    void mapLoop();
    void imageLoop();

    CommManager*      mgr_{nullptr};
    Mailbox*          map_box_{nullptr};
    Mailbox*          image_box_{nullptr};
    std::atomic<bool> running_{false};
    std::thread       map_thread_;
    std::thread       image_thread_;
    uint32_t          map_seq_{0};
};
