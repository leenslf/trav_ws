#pragma once
#ifndef TRAVERSABILITY_SLOT_PUBLISHER_HPP
#define TRAVERSABILITY_SLOT_PUBLISHER_HPP

#include "traversability/result.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// Slot state machine for the fixed-size latest-value mailbox:
//   Free -> Writing -> Published -> Reading -> Free
// A Published slot may also be recycled directly back to Writing when a newer
// result supersedes an unread one.
enum class SlotState {
    Free,
    Writing,
    Published,
    Reading,
};

// One storage slot inside SlotPublisher. The producer writes result payload and
// timestamp while the slot is in Writing, then readers observe it after publish.
struct ResultSlot {
    TraversabilityResult result;
    uint64_t             timestamp_ns{0};
    std::atomic<SlotState> state{SlotState::Free};
};

// Fixed-size latest-value handoff between the pipeline thread and a consumer.
//
// Important: despite the name, this is not a fan-out pub/sub queue. A slot is
// moved into Reading for one consumer at a time, and slow readers may miss
// intermediate publications. The 3-slot layout relies on a single producer and
// a single active reader lease.
class SlotPublisher {
public:
    // Reserves a slot for the producer. The caller owns the returned slot until
    // publish() is called. Selection prefers an unused slot and otherwise
    // reuses a stale Published slot, which drops the unread result it held.
    ResultSlot& acquire_write_slot() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t start = next_scan_index();
        if (ResultSlot* slot = find_slot(SlotState::Free, start)) {
            slot->state.store(SlotState::Writing);
            return *slot;
        }
        if (ResultSlot* slot = find_slot(SlotState::Published, start)) {
            slot->state.store(SlotState::Writing);
            return *slot;
        }

        // The architecture invariant guarantees one slot is always writable.
        slots_[0].state.store(SlotState::Writing);
        return slots_[0];
    }

    // Publishes the producer-filled slot as the newest visible result and wakes
    // waiting readers. Any older Published slot is discarded and returned to
    // Free; such discarded publications are counted by drain_drop_count().
    void publish(ResultSlot& slot) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        bool dropped_published = (latest_ == &slot);
        for (auto& candidate : slots_) {
            if (&candidate == &slot) {
                continue;
            }
            if (candidate.state.load() == SlotState::Published) {
                candidate.state.store(SlotState::Free);
                dropped_published = true;
            }
        }

        if (dropped_published) {
            drop_count_.fetch_add(1);
        }

        slot.state.store(SlotState::Published);
        latest_ = &slot;
        cv_.notify_all();
    }

    // Waits until a result newer than last_seen is available, or until
    // notify_all() is used for shutdown. The returned slot is transitioned to
    // Reading and must be returned with release().
    //
    // last_seen suppresses duplicate delivery when the producer reuses the same
    // slot object for a newer frame.
    const ResultSlot& wait_for_result(const ResultSlot* last_seen) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this, last_seen] {
            if (shutdown_) {
                return true;
            }
            if (latest_ == nullptr || latest_->state.load() != SlotState::Published) {
                return false;
            }

            // If the producer reuses the same slot object for a newer frame,
            // last_seen stops being Reading before it becomes Published again.
            return last_seen == nullptr ||
                   latest_ != last_seen ||
                   last_seen->state.load() != SlotState::Reading;
        });

        ResultSlot* slot = latest_;
        if (slot == nullptr) {
            slot = &slots_[0];
        }
        slot->state.store(SlotState::Reading);
        return *slot;
    }

    // Releases a slot after the consumer finishes reading it.
    void release(const ResultSlot& slot) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const_cast<ResultSlot&>(slot).state.store(SlotState::Free);
    }

    // Returns and resets the number of Published results that were overwritten
    // before a consumer could claim them.
    uint64_t drain_drop_count() noexcept {
        return drop_count_.exchange(0);
    }

    // Wakes blocked waiters and causes future wait_for_result() calls to stop
    // blocking. Used during shutdown so consumer threads can exit cleanly.
    void notify_all() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        cv_.notify_all();
    }

private:
    std::size_t next_scan_index() const noexcept {
        if (latest_ == nullptr) {
            return 0;
        }
        const auto base =
            static_cast<std::size_t>(latest_ - slots_.data());
        return (base + 1) % slots_.size();
    }

    ResultSlot* find_slot(SlotState state, std::size_t start) noexcept {
        for (std::size_t offset = 0; offset < slots_.size(); ++offset) {
            auto& slot = slots_[(start + offset) % slots_.size()];
            if (slot.state.load() == state) {
                return &slot;
            }
        }
        return nullptr;
    }

    std::array<ResultSlot, 3>  slots_{};
    ResultSlot*                latest_{nullptr};
    std::atomic<uint64_t>      drop_count_{0};
    std::mutex                 mutex_;
    std::condition_variable    cv_;
    bool                       shutdown_{false};
};

#endif // TRAVERSABILITY_SLOT_PUBLISHER_HPP
