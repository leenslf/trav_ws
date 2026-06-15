# ZED-Qt Component Reference

ZED-Qt is a Qt5 C++17 GUI application (`zed_qt`) that visualizes live data from a ZED camera pipeline. It receives two streams over an IPC transport (`libcomm`) and renders them in a split-panel window.

---

## Build

- **CMake ≥ 3.16**, C++17, Qt5 Widgets, `libcomm.a`, `libutils.a`
- External libraries under `../lib/` (relative to project root)
- `src/comm_receiver.cpp` requires `-fpermissive` due to old-style libcomm headers

---

## Data Structures (`include/frame_data.h`, `include/comm_receiver.h`)

### `FrameBundle` (wire type, `include/comm_receiver.h`)
Fixed-size POD received from mailbox 200. Contains all per-frame data from the Jetson pipeline:
- `timestamp_ns` — frame capture time (nanoseconds)
- `tx, ty, tz` — camera translation (metres)
- `qx, qy, qz, qw` — camera orientation quaternion
- `tracking_state` — ZED tracking state as `uint8_t`
- `cells[17][19]` — quantized traversability grid: `0`=free, `1`=obstacle, `2`=unknown

`sizeof(FrameBundle) == 360`. **Must stay byte-identical with the copy in `include/traversability/consumers/comm_sender.hpp`** on the Jetson side. A `static_assert` in both files enforces this.

### `FrameData` (`include/frame_data.h`)
Carries one decoded frame, passed via Qt signals to widgets:
- `seq` — sequence counter
- `timestamp_ns` — frame capture time (from `FrameBundle`)
- `nr`, `nt` — grid dimensions (17 × 19)
- `trav_grid` — flat `nr × nt` float array; `0.0`=traversable, `1.0`=obstacle, `NaN`=unknown
- `tx, ty, tz` — camera translation (metres, from `FrameBundle`)
- `qx, qy, qz, qw` — camera orientation quaternion (from `FrameBundle`)
- `tracking_state` — ZED tracking state as `uint8_t` (from `FrameBundle`)

---

## Communication (`include/comm_receiver.h`, `src/comm_receiver.cpp`)

### `CommReceiver : QObject`
The IPC backend. Owns a `CommManager` (from `libcomm`) and two `Mailbox` instances, each polled on its own `std::thread`:

| Mailbox | ID | Payload | Signal emitted |
|---|---|---|---|
| `map_box_` | 200 | `FrameBundle` (grid + pose + tracking_state + timestamp) | `frameReceived(FrameData)` |
| `image_box_` | 201 | raw JPEG bytes (≤ 32 768 B) | `imageReceived(QByteArray)` |

**Mailbox 202 is retired** — pose data is now bundled in mailbox 200.

- Initialises the portal with `mgr_->initPortal("net")`
- Each loop calls `waitData(200ms)` and emits the corresponding Qt signal on arrival
- Threads are joined and mailboxes destroyed in the destructor
- `FrameBundle::cells[r][c]`: `0`=free, `1`=obstacle, `2`=unknown → converted to float/NaN in `mapLoop()`
- Pose fields and `tracking_state` are copied from `FrameBundle` into `FrameData` in `mapLoop()`

---

## Widgets

### `PolarGridWidget : QWidget` (`include/polar_grid_widget.h`, `src/polar_grid_widget.cpp`)
Renders the traversability grid as a fan of polar sectors.

**Slot:** `updateFrame(const FrameData&)` — stores the frame and calls `update()`

**Config** (via `setConfig(Config)`):

| Field | Default | Meaning |
|---|---|---|
| `r_min_m` | 0.3 m | inner radius |
| `r_max_m` | 2.0 m | outer radius |
| `theta_min_deg` | −45° | left sweep limit |
| `theta_max_deg` | +45° | right sweep limit |
| `danger_threshold` | 0.3 | cells above this → obstacle color |
| `polar_grid_size_r_m` | 0.10 m | radial cell size |
| `polar_grid_size_theta_deg` | 12° | angular cell size |

**Rendering:**
- Robot at bottom-centre; X-forward maps to screen-up, Y-left maps to screen-left
- Green = traversable (`v ≤ danger_threshold`), Red = obstacle, Grey = unknown (NaN)
- Overlays: concentric arc ticks with range labels, radial spokes with degree labels, colour legend, and a status line (`seq`, `fps`, grid size)

### `JpegViewerWidget : QWidget` (`include/jpeg_viewer_widget.h`, `src/jpeg_viewer_widget.cpp`)
Displays the live JPEG camera stream with a pose HUD overlay.

**Slots:**
- `updateImage(const QByteArray&)` — decodes JPEG via `QImage::fromData` and repaints
- `updateFrame(const FrameData&)` — stores frame (for pose HUD) and repaints

**Rendering:**
- Dark background (`#2b2b2b`); image scaled to fit while preserving aspect ratio
- HUD (top-right corner, colour-coded by tracking state):
  - Line 0: `State: <name>` — green if OK/LOOP_CLOSED, yellow if degraded, red if unavailable
  - Line 1: `tx ty tz` translation
  - Line 2: `qx qy qz qw` quaternion

---

## Application Entry Point (`src/main.cpp`)

Creates a `QMainWindow` titled "ZED Polar Viewer" (600 × 600 px) with a horizontal `QSplitter`:
- Left (stretch 3): `PolarGridWidget`
- Right (stretch 1): `JpegViewerWidget`

Signal wiring:
```
CommReceiver::frameReceived  → PolarGridWidget::updateFrame
CommReceiver::frameReceived  → JpegViewerWidget::updateFrame
CommReceiver::imageReceived  → JpegViewerWidget::updateImage
```

`FrameData` is registered with `qRegisterMetaType` for queued cross-thread signal delivery.

---

## File Map

```
ZED-Qt/
├── CMakeLists.txt
├── include/
│   ├── frame_data.h          # FrameData struct + Q_DECLARE_METATYPE
│   ├── comm_receiver.h       # CommReceiver class + FrameBundle wire struct
│   ├── polar_grid_widget.h   # PolarGridWidget + Config
│   └── jpeg_viewer_widget.h  # JpegViewerWidget
└── src/
    ├── main.cpp              # App bootstrap and signal wiring
    ├── comm_receiver.cpp     # IPC receive loops
    ├── polar_grid_widget.cpp # Polar sector rendering
    └── jpeg_viewer_widget.cpp# JPEG display + pose HUD
```
