#include "comm_image_receiver.h"

#include <cstdio>

#include "libcomm.hh"

static constexpr int COMM_IMAGE_MAILBOX_ID = 201;
static constexpr int IMAGE_MAX_SIZE_BYTES  = 32768;

CommImageReceiver::CommImageReceiver(uint16_t /*port*/, QObject* parent)
    : QObject(parent)
{
    mgr_ = new CommManager();
    if (!mgr_->initPortal("net")) {
        fprintf(stderr, "CommImageReceiver: could not initialize portal\n");
        return;
    }
    box_ = mgr_->createMailbox(IMAGE_MAX_SIZE_BYTES, COMM_IMAGE_MAILBOX_ID);
    if (!box_) {
        fprintf(stderr, "CommImageReceiver: could not create mailbox\n");
        return;
    }
    running_ = true;
    recv_thread_ = std::thread(&CommImageReceiver::receiveLoop, this);
}

CommImageReceiver::~CommImageReceiver()
{
    running_ = false;
    if (recv_thread_.joinable())
        recv_thread_.join();
    if (box_) mgr_->destroyMailbox(box_);
    delete mgr_;
}

void CommImageReceiver::receiveLoop()
{
    while (running_) {
        Message* msg = box_->waitData(200);
        if (!msg) continue;

        QByteArray jpeg(reinterpret_cast<const char*>(msg->getData()), msg->getSize());
        box_->releaseMsg(msg);

        emit imageReceived(jpeg);
        emit statsUpdated(seq_++, 0);
    }
}
