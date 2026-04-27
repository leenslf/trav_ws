#include "comm_map_receiver.h"

#include <cstdio>
#include <limits>
#include <string>

#include "libcomm.hh"

// Must match the sender's TravMap exactly (same dimensions, same MAILBOX_ID)
struct TravMap {
    static const int WIDTH  = 19;
    static const int HEIGHT = 17;
    uint8_t cells[HEIGHT][WIDTH];
};

static constexpr int COMM_MAILBOX_ID = 200;

CommMapReceiver::CommMapReceiver(uint16_t port, QObject* parent)
    : QObject(parent)
{
    mgr_ = new CommManager();
    if (!mgr_->initPortal("net")) {
        fprintf(stderr, "CommMapReceiver: could not initialize portal\n");
        return;
    }
    box_ = mgr_->createMailbox(sizeof(TravMap), COMM_MAILBOX_ID);
    if (!box_) {
        fprintf(stderr, "CommMapReceiver: could not create mailbox\n");
        return;
    }
    running_ = true;
    recv_thread_ = std::thread(&CommMapReceiver::receiveLoop, this);
}

CommMapReceiver::~CommMapReceiver()
{
    running_ = false;
    if (recv_thread_.joinable())
        recv_thread_.join();
    if (box_) mgr_->destroyMailbox(box_);
    delete mgr_;
}

void CommMapReceiver::receiveLoop()
{
    while (running_) {
        // Block for up to 200 ms so the destructor can join promptly
        Message* msg = box_->waitData(200);
        if (!msg) continue;

        TravMap map;
        if (!msg->getStruct(&map)) {
            fprintf(stderr, "CommMapReceiver: message too small for TravMap\n");
            box_->releaseMsg(msg);
            continue;
        }
        box_->releaseMsg(msg);

        FrameData frame;
        frame.seq          = seq_++;
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

        // height_map and edge arrays not used by the current widget
        emit frameReceived(frame);
        emit statsUpdated(frame.seq, 0);
    }
}
