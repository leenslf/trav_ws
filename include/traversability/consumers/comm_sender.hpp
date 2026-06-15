#pragma once

#include "traversability/result_consumer.hpp"

#include <cstdint>
#include <string>

class CommManager;
class Mailer;

constexpr int TRAVMAP_MAILBOX_ID   = 200;
constexpr int IMAGE_MAILBOX_ID     = 201;
constexpr int IMAGE_MAX_SIZE_BYTES = 32768;  // 32 KB — conservative ceiling for 320x180 JPEG
// mailbox 202 retired — pose is now bundled into FrameBundle on mailbox 200

// Wire format for mailbox 200. Fixed-size POD; sent via setStruct/getStruct.
// MUST stay byte-identical with the copy in ZED-Qt/include/comm_receiver.h.
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
static_assert(sizeof(FrameBundle) == 448, "FrameBundle size mismatch — check ZED-Qt copy");

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
