# Pipeline Architecture

## Source — `ZEDSource`

A ZED stereo camera grabs frames. Each call to `capture()` retrieves an XYZRGBA point cloud **directly into GPU memory** (no CPU round-trip), plus the camera's orientation quaternion from positional tracking. Supports both live capture and SVO playback, with an optional `frame_skip` setting.

---

## Processing — 5 CUDA Stages

Stages run sequentially per frame on a single `cudaStream`.

| Stage | Input → Output | What it does |
|---|---|---|
| **ExtractXYZ** | `float4[]` (XYZRGBA) → `float3[]` | GPU stream-compaction: strips invalid/infinite pixels, outputs dense `[x,y,z]` array |
| **TiltCompensate** | `float3[]` → `float3[]` | Applies a rotation matrix derived from the camera quaternion (pitch+roll only, yaw stripped) to level the point cloud to world-horizontal |
| **VoxelFilter** | `float3[]` → `float3[]` | Downsamples by snapping points to a voxel grid (21-bit packed key encoding), keeps only voxels with ≥ N points, outputs voxel centres |
| **Polarize** | `float3[]` → `float3[]` | Filters by height and minimum range, then converts surviving `[x,y,z]` to polar `[r, θ, z]`; discards points too close or too tall |
| **Traversability** | `float3[]` (polar) → `TraversabilityResult` | Bins points into a polar height-map grid (r × θ), then runs a 5×5 sliding-window variance/slope analysis to classify each cell as traversable / obstacle / unknown |

After all stages, `cudaStreamSynchronize` is called to flush before the result is published.

---

## Fanout — `ResultPublisher`

The `PipelineRunner` writes each completed result into a shared slot via `acquire_write_slot()` / `publish()`. Consumer threads each block on `wait_for_result()`, consume the slot, then release it. This decouples the fast GPU pipeline from potentially slower output.

---

## Consumers

One consumer is selected at runtime via config.

| Consumer | Config value | What it does |
|---|---|---|
| **`CommMapSender`** | `comm` | Quantizes the float grid to 3-state (0/1/2) and sends it as a `TravMap` struct over `libcomm` (UDP/network) to a remote IP |
| **`NetworkStreamer`** | `network` | Serializes the full float grids + bin edges into a binary UDP packet and sends to localhost on a fixed port |
| **`DiskWriteConsumer`** | `disk` | Writes either colorized PNG images or CSV files per frame to an output directory |
| **`NullConsumer`** | `null` | Drops results (benchmarking/testing) |

---

## Control Flow

```
main() loads config → builds stages + source + consumer
         ↓
PipelineRunner::init() → CUDA stream, ZED open, allocate frame buffers, start consumer threads
         ↓
PipelineRunner::run() loop:
    ZEDSource::capture()       → GPU point cloud + pose → FrameData
    Stage 1–5 on cudaStream    → TraversabilityResult
    cudaStreamSynchronize()
    publisher.publish(slot)    → consumer threads pick it up asynchronously
         ↓
SIGINT → ZEDSource::request_stop() → loop exits → metrics printed
```

The pipeline is fully GPU-resident from capture through traversability computation; only the final consumer step touches the CPU.
