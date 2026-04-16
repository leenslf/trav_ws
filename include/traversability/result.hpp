#pragma once
#ifndef TRAVERSABILITY_RESULT_HPP
#define TRAVERSABILITY_RESULT_HPP

#include <vector>

struct TraversabilityResult {
    std::vector<float> trav_grid;   // row-major, rows=r_bins, cols=theta_bins
    std::vector<float> height_map;  // same layout
    std::vector<float> r_edges;     // size = r_bins + 1
    std::vector<float> theta_edges; // size = theta_bins + 1
    int r_bins{0};
    int theta_bins{0};
};

#endif // TRAVERSABILITY_RESULT_HPP
