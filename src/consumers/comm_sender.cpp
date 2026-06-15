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
    mailer_ = mgr_->createMailer(dest_spec.c_str(), sizeof(FrameBundle), TRAVMAP_MAILBOX_ID);
    if (!mailer_) {
        fprintf(stderr, "CommMapSender: could not create bundle mailer to %s\n", remote_ip.c_str());
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

void CommMapSender::consume(const FrameResult& frame, uint64_t timestamp_ns)
{
    if (!mailer_) return;

    const TraversabilityResult& result = frame.traversability;

    if (dims_state_ == 0) {
        if (result.r_bins <= FrameBundle::MAX_R && result.theta_bins <= FrameBundle::MAX_T) {
            dims_state_ = 1;
        } else {
            dims_state_ = -1;
            fprintf(stderr, "CommMapSender: grid %dx%d exceeds MAX_R=%d x MAX_T=%d — refusing to send\n",
                    result.r_bins, result.theta_bins, FrameBundle::MAX_R, FrameBundle::MAX_T);
        }
    }
    if (dims_state_ < 0) return;

    FrameBundle bundle{};
    bundle.timestamp_ns    = timestamp_ns;
    bundle.tx              = frame.camera_pose.tx;
    bundle.ty              = frame.camera_pose.ty;
    bundle.tz              = frame.camera_pose.tz;
    bundle.qx              = frame.camera_pose.qx;
    bundle.qy              = frame.camera_pose.qy;
    bundle.qz              = frame.camera_pose.qz;
    bundle.qw              = frame.camera_pose.qw;
    bundle.tracking_state  = static_cast<uint8_t>(frame.tracking_state);
    bundle.r_bins          = result.r_bins;
    bundle.theta_bins      = result.theta_bins;

    for (int r = 0; r < result.r_bins; ++r)
        for (int c = 0; c < result.theta_bins; ++c) {
            const float v = result.trav_grid[r * result.theta_bins + c];
            bundle.cells[r][c] = std::isnan(v) ? 2u : (v > 0.5f ? 1u : 0u);
        }

    Message* msg = mailer_->createMsg();
    if (msg->setStruct(&bundle)) {
        mailer_->sendMsg(msg);
    } else {
        mailer_->releaseMsg(msg);
        fprintf(stderr, "CommMapSender: message buffer too small for FrameBundle\n");
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
