#pragma once

#include <QObject>
#include <QUdpSocket>
#include <optional>
#include <cstdint>
#include "frame_data.h"

class UdpReceiver : public QObject {
    Q_OBJECT
public:
    explicit UdpReceiver(uint16_t port, QObject* parent = nullptr);

signals:
    void frameReceived(const FrameData& frame);
    void statsUpdated(uint32_t seq, uint32_t dropCount);

private slots:
    void onReadyRead();

private:
    QUdpSocket*              m_socket;
    std::optional<uint32_t>  m_lastSeq;
    uint32_t                 m_dropCount;
};
