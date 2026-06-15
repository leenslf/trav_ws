# frame_result.hpp

Defined in [include/traversability/frame_result.hpp](../include/traversability/frame_result.hpp).

Single header consolidating all per-frame output types.

---

## TrackingState

Enum reflecting the ZED camera's positional tracking state:

| Value | Meaning |
|---|---|
| `OK` | Tracking is running normally |
| `SEARCHING` | Lost tracking, attempting to relocalize |
| `FPS_TOO_LOW` | Frame rate too low to maintain tracking |
| `SEARCHING_FLOOR_PLANE` | Searching for floor plane to initialize |
| `UNAVAILABLE` | Tracking not started or module disabled |
| `LOOP_CLOSED` | Positional correction applied after loop closure |

---

## CameraPose

Camera position and orientation in world space.

| Field | Type | Description |
|---|---|---|
| `tx, ty, tz` | `float` | Translation in metres |
| `qx, qy, qz, qw` | `float` | Orientation as a unit quaternion |

---

## TraversabilityResult

Polar-grid traversability map produced by `TraversabilityStage::process()` each frame.

| Field | Type | Description |
|---|---|---|
| `trav_grid` | `vector<float>` | Row-major traversability values (`r_bins × theta_bins`) |
| `height_map` | `vector<float>` | Row-major height values, same layout |
| `r_edges` | `vector<float>` | Radial bin edges, size `r_bins + 1` |
| `theta_edges` | `vector<float>` | Angular bin edges, size `theta_bins + 1` |
| `r_bins` | `int` | Number of radial bins |
| `theta_bins` | `int` | Number of angular bins |

---

## ImagePayload

JPEG-encoded camera frame, optionally attached to each result.

| Field | Type | Description |
|---|---|---|
| `valid` | `bool` | False if no image was captured this frame |
| `width, height` | `int` | Image dimensions in pixels |
| `jpeg_bytes` | `vector<uint8_t>` | Compressed JPEG data |

---

## FrameResult

Top-level output of the pipeline for a single frame. Populated by `PipelineRunner` and passed to all `IResultConsumer` implementations.

| Field | Type | Description |
|---|---|---|
| `traversability` | `TraversabilityResult` | Traversability and height maps |
| `has_image` | `bool` | Whether `image` contains valid data |
| `image` | `ImagePayload` | Encoded camera frame |
| `timestamp_ns` | `uint64_t` | Frame capture time (nanoseconds) |
| `camera_pose` | `CameraPose` | Camera position and orientation |
| `tracking_state` | `TrackingState` | ZED tracking status at capture time |
