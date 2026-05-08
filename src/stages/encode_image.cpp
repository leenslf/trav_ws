#include "traversability/stages/encode_image.hpp"
#include "traversability/frame_data.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>

void EncodeImageStage::init(const PipelineConfig& cfg, FrameData& frame) {
    scale_        = cfg.image_encode.scale;
    jpeg_quality_ = cfg.image_encode.jpeg_quality;

    // Pre-allocate jpeg_bytes to the worst-case encoded size so that
    // imencode can write without reallocating on typical frames.
    const int scaled_w = static_cast<int>(cfg.zed.w * scale_);
    const int scaled_h = static_cast<int>(cfg.zed.h * scale_);
    frame.image_encoded.jpeg_bytes.reserve(
        static_cast<size_t>(scaled_w) * scaled_h * 3);
}

void EncodeImageStage::process(FrameData& frame, cudaStream_t /*stream*/) {
    frame.image_encoded.valid = false;

    const int src_w = static_cast<int>(frame.image_raw.getWidth());
    const int src_h = static_cast<int>(frame.image_raw.getHeight());
    if (src_w == 0 || src_h == 0) {
        return;
    }

    // Wrap the sl::Mat CPU buffer as a cv::Mat (BGRA, no copy).
    cv::Mat src(src_h, src_w, CV_8UC4,
                frame.image_raw.getPtr<sl::uchar1>(sl::MEM::CPU));

    const int dst_w = static_cast<int>(src_w * scale_);
    const int dst_h = static_cast<int>(src_h * scale_);
    if (dst_w <= 0 || dst_h <= 0) {
        return;
    }

    cv::Mat small;
    cv::resize(src, small, cv::Size(dst_w, dst_h), 0.0, 0.0, cv::INTER_LINEAR);

    const std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, jpeg_quality_};
    if (!cv::imencode(".jpg", small, frame.image_encoded.jpeg_bytes, params)) {
        std::fprintf(stderr, "[encode_image] imencode failed\n");
        return;
    }

    frame.image_encoded.width  = dst_w;
    frame.image_encoded.height = dst_h;
    frame.image_encoded.valid  = true;
}
