#pragma once
#ifndef TRAVERSABILITY_RESULT_CONSUMER_HPP
#define TRAVERSABILITY_RESULT_CONSUMER_HPP

#include <cstdint>
#include "traversability/frame_result.hpp"

class IResultConsumer {
public:
    virtual ~IResultConsumer() = default;
    virtual void consume(const FrameResult& frame, uint64_t timestamp_ns) = 0;
};

class NullConsumer : public IResultConsumer {
public:
    void consume(const FrameResult&, uint64_t) override;
};

#endif // TRAVERSABILITY_RESULT_CONSUMER_HPP
