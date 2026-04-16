#pragma once
#ifndef TRAVERSABILITY_RESULT_CONSUMER_HPP
#define TRAVERSABILITY_RESULT_CONSUMER_HPP

#include <cstdint>
#include "traversability/config.hpp"
#include "traversability/result.hpp"

class IResultConsumer {
public:
    virtual ~IResultConsumer() = default;
    virtual void consume(const TraversabilityResult& result,
                         uint64_t timestamp_ns) = 0;
};

class NullConsumer : public IResultConsumer {
public:
    void consume(const TraversabilityResult&, uint64_t) override;
};

class NetworkStreamer : public IResultConsumer {
public:
    explicit NetworkStreamer(const NetworkConfig& cfg);
    ~NetworkStreamer() override;
    void consume(const TraversabilityResult&, uint64_t) override;

private:
    int socket_fd_{-1};
};

#endif // TRAVERSABILITY_RESULT_CONSUMER_HPP
