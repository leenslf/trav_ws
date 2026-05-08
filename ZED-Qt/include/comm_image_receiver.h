#pragma once

#include <QByteArray>
#include <QObject>
#include <atomic>
#include <cstdint>
#include <thread>

class CommManager;
class Mailbox;

class CommImageReceiver : public QObject {
    Q_OBJECT
public:
    explicit CommImageReceiver(uint16_t port, QObject* parent = nullptr);
    ~CommImageReceiver() override;

    CommImageReceiver(const CommImageReceiver&)            = delete;
    CommImageReceiver& operator=(const CommImageReceiver&) = delete;

signals:
    void imageReceived(const QByteArray& jpeg);
    void statsUpdated(uint32_t seq, uint32_t dropCount);

private:
    void receiveLoop();

    CommManager*      mgr_{nullptr};
    Mailbox*          box_{nullptr};
    std::atomic<bool> running_{false};
    std::thread       recv_thread_;
    uint32_t          seq_{0};
};
