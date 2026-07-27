#pragma once

#include "traversability/result_consumer.hpp"

#include <cstdint>
#include <string>

class CommManager;
class Mailer;

constexpr int TRAVMAP_MAILBOX_ID   = 200;
constexpr int IMAGE_MAILBOX_ID     = 201;
constexpr int IMAGE_MAX_SIZE_BYTES = 32768;  // 32 KB — conservative ceiling for 320x180 JPEG

constexpr int TRAVMAP_REMOTE_PORT  = 6000;

// Wire format for mailbox 200. Fixed-size POD; sent via setStruct/getStruct.
// MUST stay byte-identical with the copy in ZED-Qt/include/comm_receiver.h.
struct FrameBundle {
    // Ragged-polar-grid sizing (see docs/design/ragged_polar_grid_design.md).
    // Sized for r_max_m up to 20.0 m (r_min_m=0.1, dr=0.25 -> R=80, +headroom -> 88);
    // theta span 90deg at r=20.0 m needs ~126 theta_bins, well under MAX_T.
    static const int MAX_R = 88;    // maximum r_bins supported
    static const int MAX_T = 256;   // maximum theta_bins supported (FOV/theta_deg=1.0 -> 90 bins, plus headroom)

    uint64_t timestamp_ns;                  // frame capture time
    float    tx, ty, tz;                    // camera translation (metres)
    float    qx, qy, qz, qw;               // camera orientation quaternion
    uint8_t  tracking_state;               // ZED TrackingState cast to uint8_t
    // 3 bytes implicit padding before int32_t
    int32_t  r_bins;                      //  = ceil( (r_max_m - r_min_m) / polar_grid_size_r_m )
    int32_t  theta_bins;                  //  = ceil( (theta_max_deg - theta_min_deg) / polar_grid_size_theta_deg )
    uint8_t  cells[MAX_R][MAX_T];          // quantized trav grid: 0=free 1=obstacle 2=unknown
                                            // only [0:r_bins, 0:theta_bins] is valid
};
static_assert(sizeof(FrameBundle) == 22576, "FrameBundle size mismatch — check ZED-Qt copy");

class CommMapSender : public IResultConsumer {
public:
    explicit CommMapSender(const std::string& remote_ip, int port = 3000);
    ~CommMapSender() override;

    CommMapSender(const CommMapSender&)            = delete;
    CommMapSender& operator=(const CommMapSender&) = delete;
    CommMapSender(CommMapSender&&)                 = delete;
    CommMapSender& operator=(CommMapSender&&)      = delete;

    void consume(const FrameResult& frame, uint64_t timestamp_ns) override;

private:
    CommManager* mgr_{nullptr};
    Mailer*      mailer_{nullptr};
    Mailer*      image_mailer_{nullptr};
    int          dims_state_{0};  // 0=unchecked, 1=valid, -1=invalid
};
