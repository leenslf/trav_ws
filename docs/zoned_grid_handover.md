# Handover: zoned angular binning broke the uniform-grid assumption

`TraversabilityStage` now bins the angular axis into per-radius **zones**
instead of one global bin size (see `PolarZone` / `compute_polar_zones` in
[include/traversability/stages/traversability.hpp](../include/traversability/stages/traversability.hpp)).
This fixes wildly non-uniform physical cell sizes, but it means
`trav_grid`/`height_map` are **no longer a uniform `r_bins × theta_bins`
grid** — every consumer that assumed `r * theta_bins + t` needs to change.

No consumer has been updated to decode the new layout yet. This doc is the
handover for whoever does that next.

## The grid is now ragged, not rectangular

Each radial bin belongs to a zone, and each zone has its own angular bin
count. `trav_grid`/`height_map` pack the rows back-to-back, dense but
variable-width:

```
row 0 (zone A, 2 bins)   [c0][c1]
row 1 (zone A, 2 bins)   [c0][c1]
row 2 (zone B, 5 bins)   [c0][c1][c2][c3][c4]
row 3 (zone B, 5 bins)   [c0][c1][c2][c3][c4]
```

flattened as one contiguous `vector<float>`, in that row order — not padded
out to a common width.

## How to read a cell — wrong vs. correct

`TraversabilityResult` (in
[frame_result.hpp](../include/traversability/frame_result.hpp)) now carries
two new fields for this:

```cpp
std::vector<int> row_offset;     // size r_bins: flat start index of row r
std::vector<int> row_theta_bins; // size r_bins: valid column count of row r
```

**Wrong** (the old assumption, still used by every existing consumer):

```cpp
float v = result.trav_grid[r * result.theta_bins + t];
```

`theta_bins` is only the **widest** row's count now (kept as a legacy
field so old bounds math doesn't divide by zero). Using it as a stride for
every row either reads the wrong cell or reads past the end of the vector.

**Correct:**

```cpp
for (int r = 0; r < result.r_bins; ++r) {
    const int nt = result.row_theta_bins[r];
    for (int t = 0; t < nt; ++t) {
        float v = result.trav_grid[result.row_offset[r] + t];
        // ...
    }
}
```

`row_offset[r] + t` is valid for `0 <= t < row_theta_bins[r]`; there is no
cell beyond that in row `r` — don't loop up to `theta_bins`.

If a consumer needs a fixed-width output (e.g. a wire struct or an image),
it has to resample each row to that width itself — there's no shortcut
that avoids knowing per-row widths.

## Current consumer status

None of these understand the ragged layout. Each was given a guard so a
zoned grid makes it **skip the frame safely** instead of reading out of
bounds — none of them produce correct output for a zoned grid yet:

| Consumer | File | Current behaviour on a zoned grid |
|---|---|---|
| `comm_sender` (`consumer: comm`, the default) | [src/consumers/comm_sender.cpp](../src/consumers/comm_sender.cpp) | Guarded: refuses to send, logs an error |
| `disk_write_consumer` | [src/consumers/disk_write_consumer.cpp](../src/consumers/disk_write_consumer.cpp) | Guarded: skips the frame, logs an error |
| `network_streamer` | [src/consumers/network_streamer.cpp](../src/consumers/network_streamer.cpp) | Already guarded (`has_valid_layout`) before this change |
| ZED-Qt `comm_receiver.h` / `polar_grid_widget.cpp` | `ZED-Qt/include/comm_receiver.h`, `ZED-Qt/src/polar_grid_widget.cpp` | Not touched — still assumes `FrameBundle::cells[MAX_R][MAX_T]` is uniform; irrelevant until `comm_sender` sends real zoned data |

`comm_sender`'s wire struct (`FrameBundle`) is a bigger problem than the
others: it's a fixed `cells[MAX_R][MAX_T]` array with no per-row width
field, hand-synced by comment with `ZED-Qt/include/comm_receiver.h`. Fixing
it for real means adding a per-row-width field to `FrameBundle` (or
resampling to a uniform width before sending) **and** updating the ZED-Qt
copy and `polar_grid_widget.cpp`'s rendering in lockstep.

## Where to look for more context

- Zone table + seam-neighbour index math:
  [include/traversability/stages/traversability.hpp](../include/traversability/stages/traversability.hpp)
- Kernels that produce the ragged buffer:
  [src/stages/traversability.cu](../src/stages/traversability.cu)
- Standalone tests for the above (no CUDA/GPU needed to run them):
  `src/stages/test_polar_zones.cpp`, `src/stages/test_seam_neighbor.cpp`
