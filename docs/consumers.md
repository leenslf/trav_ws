# Consumers

A consumer receives a completed `TraversabilityResult` after each frame and does something with it — write to disk, stream over the network, send to another machine, etc.

All consumers implement the `IResultConsumer` interface ([include/traversability/result_consumer.hpp](../include/traversability/result_consumer.hpp)):

```cpp
virtual void consume(const TraversabilityResult& result, uint64_t timestamp_ns) = 0;
```

The active consumer is selected at runtime via the `consumer` key in config. One consumer runs per pipeline instance. See [pipeline.md](pipeline.md) for the full list.

## CommMapSender (`comm`)

**Source:** [src/consumers/comm_sender.cpp](../src/consumers/comm_sender.cpp)  
**Config key:** `consumer: comm`

Sends the traversability grid to a remote machine over the network using **libcomm** (RHexLib's connectionless messaging library). libcomm uses a mailbox model: only the latest message is kept, so dropped frames are fine, the receiver always gets the most recent map.

### What it sends

The float `trav_grid` is quantized into a fixed-size `TravMap` struct:

```cpp
struct TravMap {
    static const int WIDTH  = 19;   // theta_bins — hardcoded
    static const int HEIGHT = 17;   // r_bins — hardcoded
    uint8_t cells[HEIGHT][WIDTH];
};
```

Each cell is encoded as a `uint8_t`:

| Value | Meaning |
|---|---|
| `0` | Traversable (trav_grid ≤ 0.5) |
| `1` | Non-traversable (trav_grid > 0.5) |
| `2` | Unknown (trav_grid is NaN) |

> **Note:** `TravMap` dimensions are hardcoded to 17×19. If the pipeline is configured with a different grid size the send is skipped with an error.

### Setup

`CommMapSender` is constructed with a remote IP and an optional port (default `3000`):

```
CommMapSender sender("192.168.1.10"); // hardcoded rn?
```

Internally it:
1. Creates a `CommManager` and initialises a network portal on the given port.
2. Opens a remote connection to the target machine.
3. Creates a `Mailer` bound to mailbox ID `200` (`COMM_MAILBOX_ID`).

### Per-frame flow

```
consume() called
    → quantize trav_grid floats → TravMap uint8 cells
    → mailer_->createMsg()
    → msg->setStruct(&map)
    → mailer_->sendMsg(msg)
```

---
In the future, if we want to send different messages, we may need to create a new Mailer bound to new mailbox ID, and define a fixed-size relevant to the data we want to send to feed `setStruct`.