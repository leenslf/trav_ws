#pragma once

#include <cmath>
#include <vector>

// Bin-edge reconstruction helpers.
// These produce the same values PolarGridWidget::paintEvent computes inline
// (r1 = r_min + ri*dr, a1 = th0_rad + ti*dt_rad).  Factored here so
// GlobalMapModule uses the same source of truth rather than duplicating the
// formulas.  PolarGridWidget can be migrated to call these in a later clean-up.

// Returns r_edges of size (nr + 1):  r_edges[i] = r_min_m + i * dr_m.
inline std::vector<float> make_r_edges(int nr, float r_min_m, float dr_m)
{
    std::vector<float> e(static_cast<std::size_t>(nr + 1));
    for (int i = 0; i <= nr; ++i)
        e[i] = r_min_m + i * dr_m;
    return e;
}

// Returns theta_edges of size (nt + 1) in radians:
//   theta_edges[j] = (theta_min_deg + j * dtheta_deg) * PI/180.
inline std::vector<float> make_theta_edges(int nt, float theta_min_deg, float dtheta_deg)
{
    constexpr float kToRad = static_cast<float>(M_PI) / 180.0f;
    std::vector<float> e(static_cast<std::size_t>(nt + 1));
    for (int j = 0; j <= nt; ++j)
        e[j] = (theta_min_deg + j * dtheta_deg) * kToRad;
    return e;
}
