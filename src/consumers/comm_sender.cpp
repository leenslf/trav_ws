#include "traversability/consumers/comm_sender.hpp"

#include <cmath>
#include <cstdio>

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
    mailer_ = mgr_->createMailer(dest_spec.c_str(), sizeof(TravMap), COMM_MAILBOX_ID);
    if (!mailer_) {
        fprintf(stderr, "CommMapSender: could not create mailer to %s\n", remote_ip.c_str());
    }
}

CommMapSender::~CommMapSender()
{
    if (mailer_) mgr_->destroyMailer(mailer_);
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
}
