#pragma once

#include <QByteArray>
#include <QObject>
#include <atomic>
#include <cstdint>
#include <thread>
#include "frame_data.h"

static constexpr int POSE_MAILBOX_ID = 202;

struct PoseMsg {
    float   tx, ty, tz;
    float   qx, qy, qz, qw;
    uint8_t state;
};

Q_DECLARE_METATYPE(PoseMsg)

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
    void poseReceived(const PoseMsg& pose);

private:
    void mapLoop();
    void imageLoop();
    void poseLoop();

    CommManager*      mgr_{nullptr};
    Mailbox*          map_box_{nullptr};
    Mailbox*          image_box_{nullptr};
    Mailbox*          pose_box_{nullptr};
    std::atomic<bool> running_{false};
    std::thread       map_thread_;
    std::thread       image_thread_;
    std::thread       pose_thread_;
    uint32_t          map_seq_{0};
    uint32_t          image_seq_{0};
    uint32_t          pose_seq_{0};
};
