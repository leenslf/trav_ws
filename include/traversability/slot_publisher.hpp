#pragma once
#include "traversability/result.hpp"
#include <cstdint>

// TODO: replace with triple-buffered implementation

struct ResultSlot {
    TraversabilityResult result;
    uint64_t             timestamp_ns{0};
};

class SlotPublisher {
public:
    // Producer side
    ResultSlot& acquire_write_slot() noexcept { return slot_; }
    void        publish(ResultSlot&) noexcept {}   // no-op for now

    // Consumer side
    const ResultSlot& latest() const noexcept { return slot_; }

private:
    ResultSlot slot_;
};
