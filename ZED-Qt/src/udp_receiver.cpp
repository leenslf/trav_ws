#include "udp_receiver.h"

#include <QHostAddress>
#include <QCoreApplication>
#include <cstring>

UdpReceiver::UdpReceiver(uint16_t port, QObject* parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_lastSeq(std::nullopt)
    , m_dropCount(0)
{
    if (!m_socket->bind(QHostAddress::Any, port)) {
        qCritical("UdpReceiver: cannot bind on port %u: %s",
                  static_cast<unsigned>(port),
                  qPrintable(m_socket->errorString()));
        QCoreApplication::exit(1);
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &UdpReceiver::onReadyRead);
}

void UdpReceiver::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(m_socket->pendingDatagramSize()), Qt::Uninitialized);
        m_socket->readDatagram(datagram.data(), datagram.size());

        if (datagram.size() < protocol::HEADER_SIZE)
            continue;

        protocol::Header hdr;
        std::memcpy(&hdr, datagram.constData(), sizeof(hdr));

        if (hdr.magic != protocol::MAGIC)
            continue;

        const int nr = static_cast<int>(hdr.nr);
        const int nt = static_cast<int>(hdr.nt);
        const qint64 expected = protocol::HEADER_SIZE
                                + static_cast<qint64>(2 * nr * nt + nr + nt + 2) * sizeof(float);
        if (datagram.size() != expected)
            continue;

        if (m_lastSeq.has_value() && hdr.seq != *m_lastSeq + 1)
            m_dropCount += hdr.seq - (*m_lastSeq + 1);
        m_lastSeq = hdr.seq;

        FrameData frame;
        frame.seq          = hdr.seq;
        frame.timestamp_ns = hdr.timestamp_ns;
        frame.nr           = nr;
        frame.nt           = nt;

        frame.trav_grid.resize(nr * nt);
        frame.height_map.resize(nr * nt);
        frame.r_edges.resize(nr + 1);
        frame.theta_edges.resize(nt + 1);

        const char* payload = datagram.constData() + protocol::HEADER_SIZE;
        int offset = 0;

        std::memcpy(frame.trav_grid.data(),   payload + offset, nr * nt * sizeof(float));
        offset += nr * nt * sizeof(float);

        std::memcpy(frame.height_map.data(),  payload + offset, nr * nt * sizeof(float));
        offset += nr * nt * sizeof(float);

        std::memcpy(frame.r_edges.data(),     payload + offset, (nr + 1) * sizeof(float));
        offset += (nr + 1) * sizeof(float);

        std::memcpy(frame.theta_edges.data(), payload + offset, (nt + 1) * sizeof(float));

        emit frameReceived(frame);
        emit statsUpdated(hdr.seq, m_dropCount);
    }
}
