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

Sends all per-frame data to a remote machine over the network using **libcomm** (RHexLib's connectionless messaging library). libcomm uses a mailbox model: only the latest message is kept, so dropped frames are fine, the receiver always gets the most recent data.

### What it sends

All per-frame data is packed into a single fixed-size `FrameBundle` struct and sent on **mailbox 200**:

```cpp
struct FrameBundle {
    static const int MAX_R = 20;   // maximum r_bins supported
    static const int MAX_T = 20;   // maximum theta_bins supported

    uint64_t timestamp_ns;          // frame capture time
    float    tx, ty, tz;            // camera translation (metres)
    float    qx, qy, qz, qw;       // camera orientation quaternion
    uint8_t  tracking_state;        // ZED TrackingState cast to uint8_t
    // 3 bytes implicit padding
    int32_t  r_bins;                // actual r dimension this run (≤ MAX_R)
    int32_t  theta_bins;            // actual theta dimension this run (≤ MAX_T)
    uint8_t  cells[MAX_R][MAX_T];  // quantized trav grid; only [0:r_bins, 0:theta_bins] is valid
};
// sizeof(FrameBundle) == 448
```

`r_bins` and `theta_bins` are derived once at startup from config (`polar_grid_size_r_m` / `polar_grid_size_theta_deg` and the fixed extents) and are constant for the run. The receiver uses them to know which sub-rectangle of `cells` is populated; the rest of the array is unused.

The trav grid cells are encoded as `uint8_t`:

| Value | Meaning |
|---|---|
| `0` | Traversable (trav_grid ≤ 0.5) |
| `1` | Non-traversable (trav_grid > 0.5) |
| `2` | Unknown (trav_grid is NaN) |

The `tracking_state` integer maps to the `TrackingState` enum values (0=OK, 1=SEARCHING, 2=FPS_TOO_LOW, 3=SEARCHING_FLOOR_PLANE, 4=UNAVAILABLE, 5=LOOP_CLOSED).

> **Note:** `CommMapSender` validates `r_bins ≤ MAX_R` and `theta_bins ≤ MAX_T` on the first frame. If either is exceeded it logs an error and drops all subsequent frames for the run (fail-fast at startup).

A separate image mailer on **mailbox 201** sends raw JPEG bytes (≤ 32 768 B) for the camera stream. **Mailbox 202 is retired.**

### Setup

`CommMapSender` is constructed with a remote IP and an optional port (default `3000`):

```
CommMapSender sender("192.168.1.10");
```

Internally it:
1. Creates a `CommManager` and initialises a network portal on the given port.
2. Opens a remote connection to the target machine.
3. Creates a `Mailer` bound to mailbox ID `200` (`TRAVMAP_MAILBOX_ID`) sized for `FrameBundle`.
4. Creates a `Mailer` bound to mailbox ID `201` (`IMAGE_MAILBOX_ID`) for JPEG frames.

### Per-frame flow

```
consume() called
    → populate FrameBundle: timestamp, pose, tracking_state
    → quantize trav_grid floats → FrameBundle::cells
    → mailer_->createMsg()
    → msg->setStruct(&bundle)
    → mailer_->sendMsg(msg)
    → (separately) send JPEG via image_mailer_ on mailbox 201
```

### Dual-header note

`FrameBundle` is defined in **both**:
- `include/traversability/consumers/comm_sender.hpp` (Jetson pipeline)
- `ZED-Qt/include/comm_receiver.h` (receiver)

Both copies must remain byte-identical (field order, types, sizes). A `static_assert(sizeof(FrameBundle) == 448)` in each file catches layout divergence at compile time.
