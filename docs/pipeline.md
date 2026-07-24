# Pipeline Architecture

## Source — `ZEDSource`

A ZED stereo camera grabs frames. Each call to `capture()` retrieves an XYZRGBA point cloud **directly into GPU memory** (no CPU round-trip), plus the camera's orientation quaternion from positional tracking. Supports both live capture and SVO playback, with an optional `frame_skip` setting.

---

## Processing — 6 Pipeline Stages

`PipelineRunner` calls each stage's `process(frame, stream)` in order every frame (`src/pipeline_runner.cpp`). Five stages run as CUDA kernels on a shared `cudaStream` and operate on the GPU point cloud; `EncodeImageStage` runs first, entirely on the CPU (it ignores the stream argument), and operates on the raw BGRA image rather than the point cloud, it produces the low-res JPEG preview independently of the traversability computation below it.

| Stage | Input → Output | What it does |
|---|---|---|
| **EncodeImage** (CPU) | `cv::Mat` (BGRA) → JPEG bytes | Resizes the raw camera frame and JPEG-encodes it via OpenCV for a lightweight preview payload |
| **ExtractXYZ** | `float4[]` (XYZRGBA) → `float3[]` | GPU stream-compaction: strips invalid/infinite pixels, outputs dense `[x,y,z]` array |
| **TiltCompensate** | `float3[]` → `float3[]` | Applies a rotation matrix derived from the camera quaternion (pitch+roll only, yaw stripped) to level the point cloud to world-horizontal |
| **VoxelFilter** | `float3[]` → `float3[]` | Downsamples by snapping points to a voxel grid (21-bit packed key encoding), keeps only voxels with ≥ N points, outputs voxel centres |
| **Polarize** | `float3[]` → `float3[]` | Filters by height and minimum range, then converts surviving `[x,y,z]` to polar `[r, θ, z]`; discards points too close or too tall |
| **Traversability** | `float3[]` (polar) → `TraversabilityResult` | Bins points into a polar height-map grid (r × θ), then runs a 5×5 sliding-window variance/slope analysis to classify each cell as traversable / obstacle / unknown |

After the GPU stages, `cudaStreamSynchronize` is called to flush before the result is published.


## CPU Reference Implementations — `reference/`

Each stage in `src/stages/` has a plain NumPy/Python translation in `reference/` (`extract_xyz.py`, `tilt_compensate.py`, `voxel_filter.py`, `polarize.py`, `traversability.py`, `encode_image.py`). These exist to verify the *algorithm*, filtering rules, voxel key encoding, quaternion math, the height-map/slope/roughness/step-height/ray-cast logic. `traversability.py` in particular mirrors each `__global__` kernel as its own function (including the `reflect_index` padding and the ray-cast's no-obstacle edge case) so it can be read side-by-side with `traversability.cu`. They are not used by the runtime pipeline; use them to sanity-check expected output on a captured frame or a synthetic point cloud.


## Fanout — `ResultPublisher`

The `PipelineRunner` writes each completed result into a shared slot via `acquire_write_slot()` / `publish()`. Consumer threads each block on `wait_for_result()`, consume the slot, then release it. This decouples the fast GPU pipeline from potentially slower output.


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
    EncodeImage (CPU) → Stage 1–5 on cudaStream → TraversabilityResult
    cudaStreamSynchronize()
    publisher.publish(slot)    → consumer threads pick it up asynchronously
         ↓
SIGINT → ZEDSource::request_stop() → loop exits → metrics printed
```

The point-cloud path (ExtractXYZ → Traversability) is fully GPU-resident; only `EncodeImageStage` and the final consumer step touch the CPU.
