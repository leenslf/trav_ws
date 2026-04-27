#pragma once

#include "traversability/result_consumer.hpp"

#include <cstdint>
#include <string>

class CommManager;
class Mailer;

constexpr int COMM_MAILBOX_ID = 200;

struct TravMap {
    // these numbers are hardcoded in both places for simplicity for now. 
    static const int WIDTH  = 19;
    static const int HEIGHT = 17;
    uint8_t cells[HEIGHT][WIDTH];
};

class CommMapSender : public IResultConsumer {
public:
    explicit CommMapSender(const std::string& remote_ip, int port = 3000);
    ~CommMapSender() override;

    CommMapSender(const CommMapSender&)            = delete;
    CommMapSender& operator=(const CommMapSender&) = delete;
    CommMapSender(CommMapSender&&)                 = delete;
    CommMapSender& operator=(CommMapSender&&)      = delete;

    void consume(const TraversabilityResult& result, uint64_t timestamp_ns) override;

private:
    CommManager* mgr_{nullptr};
    Mailer*      mailer_{nullptr};
};
