#pragma once
#include "traversability/result_consumer.hpp"
#include "traversability/frame_result.hpp"
#include <opencv2/core.hpp>
#include <string>
#include <cstdint>

class DiskWriteConsumer : public IResultConsumer {
public:
    DiskWriteConsumer(std::string output_dir, bool write_images = true);

    void consume(const FrameResult& frame,
                 uint64_t timestamp_ns) override;

private:
    static cv::Mat colorize(const TraversabilityResult& result);
    void write_traversability_grid(const TraversabilityResult& result) const;

    std::string  output_dir_;
    bool         write_images_{true};
    uint64_t     frame_index_{0};
};
