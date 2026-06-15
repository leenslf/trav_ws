#pragma once

#include "traversability/frame_result.hpp"

namespace fusion {

// "Last observation wins", with one exception: a NaN new_value does NOT
// overwrite a known (non-NaN) existing value — it preserves existing
// information when a bin is unobserved in the current frame.
//
// Truth table:
//   existing  new_value  result
//   NaN       NaN        NaN
//   NaN       0 or 1     new_value
//   0 or 1    NaN        existing  (unchanged)
//   0 or 1    0 or 1     new_value (overwrite)
float overwrite(float existing, float new_value);

// Returns true if a frame with this tracking state should be fused into the
// global map.  OK and LOOP_CLOSED have reliable pose; all other states
// indicate the pose is absent or unreliable.
bool is_fusable(TrackingState state);

} // namespace fusion
