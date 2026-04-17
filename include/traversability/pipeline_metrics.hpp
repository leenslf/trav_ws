#pragma once
#include <cstdint>

struct PipelineMetrics {
    uint64_t frames_captured{0};
    uint64_t frames_processed{0};
    double   elapsed_sec{0.0};

    double source_fps()   const { return frames_captured  / elapsed_sec; }
    double pipeline_fps() const { return frames_processed / elapsed_sec; }
};
