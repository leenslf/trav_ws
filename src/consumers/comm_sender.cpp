#include "traversability/consumers/comm_sender.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "libcomm.hh"

CommMapSender::CommMapSender(const std::string& remote_ip, int port)
{
    mgr_ = new CommManager();
    const std::string portal_spec = "net: port=" + std::to_string(port) + ";";
    if (!mgr_->initPortal(portal_spec.c_str())) {
        fprintf(stderr, "CommMapSender: could not initialize portal\n");
        return;
    }
    const std::string dest_spec = "net: machine=" + remote_ip + ";";
    mgr_->openRemote(dest_spec.c_str());
    mailer_ = mgr_->createMailer(dest_spec.c_str(), sizeof(TravMap), TRAVMAP_MAILBOX_ID);
    if (!mailer_) {
        fprintf(stderr, "CommMapSender: could not create travmap mailer to %s\n", remote_ip.c_str());
    }
    image_mailer_ = mgr_->createMailer(dest_spec.c_str(), IMAGE_MAX_SIZE_BYTES, IMAGE_MAILBOX_ID);
    if (!image_mailer_) {
        fprintf(stderr, "CommMapSender: could not create image mailer to %s\n", remote_ip.c_str());
    }
}

CommMapSender::~CommMapSender()
{
    if (mailer_)       mgr_->destroyMailer(mailer_);
    if (image_mailer_) mgr_->destroyMailer(image_mailer_);
    delete mgr_;
}

void CommMapSender::consume(const FrameResult& frame, uint64_t /*timestamp_ns*/)
{
    if (!mailer_) return;

    const TraversabilityResult& result = frame.traversability;

    if (result.r_bins != TravMap::HEIGHT || result.theta_bins != TravMap::WIDTH) {
        fprintf(stderr, "CommMapSender: expected %dx%d grid, got %dx%d\n",
                TravMap::HEIGHT, TravMap::WIDTH, result.r_bins, result.theta_bins);
        return;
    }

    TravMap map{};
    for (int r = 0; r < TravMap::HEIGHT; ++r)
        for (int c = 0; c < TravMap::WIDTH; ++c) {
            const float v = result.trav_grid[r * result.theta_bins + c];
            map.cells[r][c] = std::isnan(v) ? 2u : (v > 0.5f ? 1u : 0u);
        }

    Message* msg = mailer_->createMsg();
    if (msg->setStruct(&map)) {
        mailer_->sendMsg(msg);
    } else {
        mailer_->releaseMsg(msg);
        fprintf(stderr, "CommMapSender: message buffer too small for TravMap\n");
    }

    if (image_mailer_ && frame.has_image && !frame.image.jpeg_bytes.empty()) {
        Message* img_msg = image_mailer_->createMsg();
        if (img_msg) {
            const auto& bytes = frame.image.jpeg_bytes;
            if (bytes.size() <= static_cast<size_t>(IMAGE_MAX_SIZE_BYTES)) {
                std::memcpy(img_msg->getData(), bytes.data(), bytes.size());
                img_msg->setSize(static_cast<int>(bytes.size()));
                image_mailer_->sendMsg(img_msg);
            } else {
                image_mailer_->releaseMsg(img_msg);
                fprintf(stderr, "CommMapSender: JPEG (%zu B) exceeds IMAGE_MAX_SIZE_BYTES — frame dropped\n",
                        bytes.size());
            }
        }
    }
}
