#pragma once

#include "traversability/result_consumer.hpp"

#include <cstdint>
#include <string>

class CommManager;
class Mailer;

constexpr int TRAVMAP_MAILBOX_ID   = 200;
constexpr int IMAGE_MAILBOX_ID     = 201;
constexpr int IMAGE_MAX_SIZE_BYTES = 32768;  // 32 KB — conservative ceiling for 320x180 JPEG
constexpr int POSE_MAILBOX_ID      = 202;

struct TravMap {
    // these numbers are hardcoded in both places for simplicity for now.
    static const int WIDTH  = 19;
    static const int HEIGHT = 17;
    uint8_t cells[HEIGHT][WIDTH];
};

struct PoseMsg {
    CameraPose pose;
    uint8_t    state;
};

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
    Mailer*      pose_mailer_{nullptr};
};
