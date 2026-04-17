# SlotPublisher

`SlotPublisher` is the handoff point between the pipeline thread that produces
`TraversabilityResult` values and the consumer thread that processes them.
Its job is to keep the hot path allocation-free, always expose the newest
result, and allow a slow consumer to skip stale frames instead of stalling the
pipeline.

## What It Actually Is

This type is best understood as a fixed-size latest-value mailbox.

- It keeps three `ResultSlot` objects in memory.
- The producer writes into one slot, then marks it as the newest published
  result.
- A consumer blocks until a newer result is available, reads it, and releases
  the slot.
- If the producer outpaces the consumer, unread published data may be
  overwritten.

This is intentionally different from a FIFO queue:

- order is only preserved for results that the consumer actually sees
- intermediate publications may be skipped
- the newest result is preferred over completeness

## Slot Lifecycle

Each slot moves through this state machine:

```text
Free -> Writing -> Published -> Reading -> Free
```

There is one additional shortcut:

```text
Published -> Writing
```

That shortcut is the overwrite path. It happens when a newer result supersedes
an older published result before the consumer claims it.

## Why Three Slots

With one producer and one active reader lease, three slots are enough to keep
the pipeline moving:

1. one slot may be `Reading`
2. one slot may be the latest `Published` result
3. one slot remains available for the next write

`acquire_write_slot()` scans starting just after `latest_`:

- first choice: a `Free` slot
- second choice: a stale `Published` slot
- never by design: a `Reading` slot

That is the invariant behind the fallback comment in the implementation: the
class assumes there is always a writable slot under the supported concurrency
model.

## Publish And Drop Semantics

`publish()` makes one slot the new `latest_` value and frees any other slot
still sitting in `Published`.

The drop counter tracks publications that were visible but got overwritten
before a consumer claimed them. In practice that means:

- the pipeline produced a newer result before the consumer caught up
- the consumer will jump forward to the newest result
- `drain_drop_count()` reports how many published snapshots were discarded

This is a mailbox-level metric. It does not mean the source dropped capture
frames upstream; it only counts results discarded inside `SlotPublisher`.

## Reader Contract

The consumer-side protocol is:

1. call `wait_for_result(last_seen)`
2. read `slot.result` and `slot.timestamp_ns`
3. call `release(slot)` exactly once
4. keep the returned slot pointer as the next `last_seen`

`last_seen` matters because the producer may reuse the same physical slot for a
newer frame. `wait_for_result()` uses both the pointer identity and the slot
state to distinguish:

- "same slot object, same already-consumed publication"
- "same slot object, but reused for a newer publication"

Without that check, a consumer could miss updates whenever the producer cycles
back to the same slot.

## Important Limitation

`SlotPublisher` does not provide true multi-consumer fan-out.

Once `wait_for_result()` returns, that slot is moved to `Reading`. Another
consumer waiting at the same time will not independently receive that same
publication. With multiple consumer threads:

- consumers compete for whichever result becomes readable next
- a frame is not guaranteed to be delivered to every consumer
- the "three slots is always enough" invariant no longer holds reliably

So the supported model is:

- one producer thread
- one active reader lease at a time
- latest-value delivery, not per-consumer broadcast

If every consumer must observe every frame, use separate publishers per
consumer or switch to a ref-counted fan-out design.

## How PipelineRunner Uses It

`PipelineRunner::run_frame()` does the producer side:

1. capture and process a frame
2. copy `frame_.result` and `frame_.timestamp_ns` into a reserved slot
3. call `publish()`

`PipelineRunner::consumer_loop()` does the consumer side:

1. block in `wait_for_result(last_seen)`
2. pass the result to `consumer.consume(...)`
3. call `release()`

This matches the current executable setup, where `main.cpp` installs a single
`DiskWriteConsumer`. If more consumer threads are added, this mailbox should
not be treated as a broadcast mechanism.
