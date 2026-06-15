#include "traversability/consumers/disk_write_consumer.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <fstream>

DiskWriteConsumer::DiskWriteConsumer(std::string output_dir, bool write_images)
    : output_dir_(std::move(output_dir)),
      write_images_(write_images)
{
    std::filesystem::create_directories(output_dir_);
}

void DiskWriteConsumer::consume(const FrameResult& frame,
                                uint64_t /*timestamp_ns*/)
{
    const TraversabilityResult& result = frame.traversability;

    if (write_images_) {
        cv::Mat img = colorize(result);

        char name[32];
        std::snprintf(name, sizeof(name), "/frame_%06llu.png",
                      static_cast<unsigned long long>(frame_index_));
        cv::imwrite(output_dir_ + name, img);
    } else {
        write_traversability_grid(result);
    }

    ++frame_index_;
}

cv::Mat DiskWriteConsumer::colorize(const TraversabilityResult& result)
{
    const int nr = result.r_bins;
    const int nt = result.theta_bins;

    cv::Mat img(nr, nt, CV_8UC3);

    for (int r = 0; r < nr; ++r) {
        for (int t = 0; t < nt; ++t) {
            const float v = result.trav_grid[r * nt + t];
            cv::Vec3b color;
            if (std::isnan(v)) {
                color = {128, 128, 128};  // gray — unknown
            } else if (v <= 0.5f) {
                color = {0, 200, 0};      // green — traversable
            } else {
                color = {0, 0, 220};      // red — obstacle
            }
            img.at<cv::Vec3b>(r, t) = color;
        }
    }

    cv::Mat scaled;
    cv::resize(img, scaled, cv::Size(nt * 8, nr * 8), 0, 0, cv::INTER_NEAREST);
    return scaled;
}

void DiskWriteConsumer::write_traversability_grid(const TraversabilityResult& result) const
{
    char name[32];
    std::snprintf(name, sizeof(name), "/frame_%06llu.csv",
                  static_cast<unsigned long long>(frame_index_));

    std::ofstream out(output_dir_ + name);
    for (int r = 0; r < result.r_bins; ++r) {
        for (int t = 0; t < result.theta_bins; ++t) {
            if (t > 0) {
                out << ',';
            }
            out << result.trav_grid[r * result.theta_bins + t];
        }
        out << '\n';
    }
}
