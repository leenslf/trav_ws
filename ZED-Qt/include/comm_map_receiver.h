#pragma once

#include <QObject>
#include <atomic>
#include <cstdint>
#include <thread>
#include "frame_data.h"

class CommManager;
class Mailbox;

class CommMapReceiver : public QObject {
    Q_OBJECT
public:
    explicit CommMapReceiver(uint16_t port, QObject* parent = nullptr);
    ~CommMapReceiver() override;

    CommMapReceiver(const CommMapReceiver&)            = delete;
    CommMapReceiver& operator=(const CommMapReceiver&) = delete;

signals:
    void frameReceived(const FrameData& frame);
    void statsUpdated(uint32_t seq, uint32_t dropCount);

private:
    void receiveLoop();

    CommManager*      mgr_{nullptr};
    Mailbox*          box_{nullptr};
    std::atomic<bool> running_{false};
    std::thread       recv_thread_;
    uint32_t          seq_{0};
};
