#include "comm_receiver.h"

#include <cstdio>
#include <limits>

#include "libcomm.hh"

struct TravMap {
    static const int WIDTH  = 19;
    static const int HEIGHT = 17;
    uint8_t cells[HEIGHT][WIDTH];
};

static constexpr int MAP_MAILBOX_ID        = 200;
static constexpr int IMAGE_MAILBOX_ID      = 201;
static constexpr int IMAGE_MAX_SIZE_BYTES  = 32768;

CommReceiver::CommReceiver(QObject* parent)
    : QObject(parent)
{
    mgr_ = new CommManager();
    if (!mgr_->initPortal("net")) {
        fprintf(stderr, "CommReceiver: could not initialize portal\n");
        return;
    }
    map_box_ = mgr_->createMailbox(sizeof(TravMap), MAP_MAILBOX_ID);
    if (!map_box_) {
        fprintf(stderr, "CommReceiver: could not create map mailbox\n");
        return;
    }
    image_box_ = mgr_->createMailbox(IMAGE_MAX_SIZE_BYTES, IMAGE_MAILBOX_ID);
    if (!image_box_) {
        fprintf(stderr, "CommReceiver: could not create image mailbox\n");
        return;
    }
    pose_box_ = mgr_->createMailbox(sizeof(PoseMsg), POSE_MAILBOX_ID);
    if (!pose_box_) {
        fprintf(stderr, "CommReceiver: could not create pose mailbox\n");
        return;
    }
    running_ = true;
    map_thread_   = std::thread(&CommReceiver::mapLoop, this);
    image_thread_ = std::thread(&CommReceiver::imageLoop, this);
    pose_thread_  = std::thread(&CommReceiver::poseLoop, this);
}

CommReceiver::~CommReceiver()
{
    running_ = false;
    if (map_thread_.joinable())   map_thread_.join();
    if (image_thread_.joinable()) image_thread_.join();
    if (pose_thread_.joinable())  pose_thread_.join();
    if (map_box_)   mgr_->destroyMailbox(map_box_);
    if (image_box_) mgr_->destroyMailbox(image_box_);
    if (pose_box_)  mgr_->destroyMailbox(pose_box_);
    delete mgr_;
}

void CommReceiver::mapLoop()
{
    while (running_) {
        Message* msg = map_box_->waitData(200);
        if (!msg) continue;

        TravMap map;
        if (!msg->getStruct(&map)) {
            fprintf(stderr, "CommReceiver: map message too small\n");
            map_box_->releaseMsg(msg);
            continue;
        }
        map_box_->releaseMsg(msg);

        FrameData frame;
        frame.seq          = map_seq_++;
        frame.timestamp_ns = 0;
        frame.nr           = TravMap::HEIGHT;
        frame.nt           = TravMap::WIDTH;

        frame.trav_grid.resize(TravMap::HEIGHT * TravMap::WIDTH);
        for (int r = 0; r < TravMap::HEIGHT; ++r)
            for (int c = 0; c < TravMap::WIDTH; ++c) {
                const uint8_t cell = map.cells[r][c];
                frame.trav_grid[r * TravMap::WIDTH + c] =
                    (cell == 2u) ? std::numeric_limits<float>::quiet_NaN()
                                 : (cell ? 1.0f : 0.0f);
            }

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
        image_seq_++;
    }
}

void CommReceiver::poseLoop()
{
    while (running_) {
        Message* msg = pose_box_->waitData(200);
        if (!msg) continue;

        PoseMsg pose;
        if (!msg->getStruct(&pose)) {
            fprintf(stderr, "CommReceiver: pose message too small\n");
            pose_box_->releaseMsg(msg);
            continue;
        }
        pose_box_->releaseMsg(msg);

        emit poseReceived(pose);
        pose_seq_++;
    }
}
