#include "comm_receiver.h"

#include <cstdio>
#include <limits>

#include "libcomm.hh"

CommReceiver::CommReceiver(QObject* parent)
    : QObject(parent)
{
    mgr_ = new CommManager();
    char portal_spec[64];
    snprintf(portal_spec, sizeof(portal_spec), "net: int port=%d;", MAP_LOCAL_PORT);
    if (!mgr_->initPortal(portal_spec)) {
        fprintf(stderr, "CommReceiver: could not initialize portal\n");
        return;
    }
    map_box_ = mgr_->createMailbox(sizeof(FrameBundle), MAP_MAILBOX_ID);
    if (!map_box_) {
        fprintf(stderr, "CommReceiver: could not create map mailbox\n");
        return;
    }
    image_box_ = mgr_->createMailbox(IMAGE_MAX_SIZE_BYTES, IMAGE_MAILBOX_ID);
    if (!image_box_) {
        fprintf(stderr, "CommReceiver: could not create image mailbox\n");
        return;
    }
    running_ = true;
    map_thread_   = std::thread(&CommReceiver::mapLoop, this);
    image_thread_ = std::thread(&CommReceiver::imageLoop, this);
}

CommReceiver::~CommReceiver()
{
    running_ = false;
    if (map_thread_.joinable())   map_thread_.join();
    if (image_thread_.joinable()) image_thread_.join();
    if (map_box_)   mgr_->destroyMailbox(map_box_);
    if (image_box_) mgr_->destroyMailbox(image_box_);
    delete mgr_;
}

void CommReceiver::mapLoop()
{
    while (running_) {
        Message* msg = map_box_->waitData(200);
        if (!msg) continue;

        FrameBundle bundle;
        if (!msg->getStruct(&bundle)) {
            fprintf(stderr, "CommReceiver: bundle message too small\n");
            map_box_->releaseMsg(msg);
            continue;
        }
        map_box_->releaseMsg(msg);

        const int nr = bundle.r_bins;
        const int nt = bundle.theta_bins;

        FrameData frame;
        frame.seq          = map_seq_++;
        frame.timestamp_ns = bundle.timestamp_ns;
        frame.nr           = nr;
        frame.nt           = nt;

        frame.trav_grid.resize(nr * nt);
        for (int r = 0; r < nr; ++r)
            for (int c = 0; c < nt; ++c) {
                const uint8_t cell = bundle.cells[r][c];
                frame.trav_grid[r * nt + c] =
                    (cell == 2u) ? std::numeric_limits<float>::quiet_NaN()
                                 : (cell ? 1.0f : 0.0f);
            }

        frame.tx             = bundle.tx;
        frame.ty             = bundle.ty;
        frame.tz             = bundle.tz;
        frame.qx             = bundle.qx;
        frame.qy             = bundle.qy;
        frame.qz             = bundle.qz;
        frame.qw             = bundle.qw;
        frame.tracking_state = bundle.tracking_state;

        emit frameReceived(frame);
    }
}

void CommReceiver::imageLoop()
{
    while (running_) {
        Message* msg = image_box_->waitData(200);
        if (!msg) continue;

        QByteArray jpeg(reinterpret_cast<const char*>(msg->getData()), msg->getSize());
        image_box_->releaseMsg(msg);

        emit imageReceived(jpeg);
    }
}
