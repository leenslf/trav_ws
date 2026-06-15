#include "global_map/fusion_rules.h"
#include <cmath>

namespace fusion {

float overwrite(float existing, float new_value) {
    if (std::isnan(new_value)) return existing;
    return new_value;
}

bool is_fusable(TrackingState state) {
    return state == TrackingState::OK || state == TrackingState::LOOP_CLOSED;
}

} // namespace fusion
