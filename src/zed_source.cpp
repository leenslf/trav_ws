#include "traversability/zed_source.hpp"

#include <cstdio>
#include <cuda_runtime.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

sl::UNIT parse_coordinate_units(const std::string& value) {
    if (value == "METER")       return sl::UNIT::METER;
    if (value == "CENTIMETER")  return sl::UNIT::CENTIMETER;
    if (value == "MILLIMETER")  return sl::UNIT::MILLIMETER;
    if (value == "INCH")        return sl::UNIT::INCH;
    if (value == "FOOT")        return sl::UNIT::FOOT;
    throw std::runtime_error("ZEDSource: unknown coordinate_units: " + value);
}

sl::COORDINATE_SYSTEM parse_coordinate_system(const std::string& value) {
    if (value == "RIGHT_HANDED_Z_UP")       return sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP;
    if (value == "RIGHT_HANDED_Z_UP_X_FWD") return sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD;
    if (value == "RIGHT_HANDED_Y_UP")       return sl::COORDINATE_SYSTEM::RIGHT_HANDED_Y_UP;
    if (value == "LEFT_HANDED_Y_UP")        return sl::COORDINATE_SYSTEM::LEFT_HANDED_Y_UP;
    if (value == "LEFT_HANDED_Z_UP")        return sl::COORDINATE_SYSTEM::LEFT_HANDED_Z_UP;
    throw std::runtime_error("ZEDSource: unknown coordinate_system: " + value);
}

sl::DEPTH_MODE parse_depth_mode(const std::string& value) {
    if (value == "PERFORMANCE") return sl::DEPTH_MODE::PERFORMANCE;
    if (value == "QUALITY")     return sl::DEPTH_MODE::QUALITY;
    if (value == "ULTRA")       return sl::DEPTH_MODE::ULTRA;
    if (value == "NEURAL")      return sl::DEPTH_MODE::NEURAL;
    throw std::runtime_error("ZEDSource: unknown depth_mode: " + value);
}

sl::RESOLUTION parse_resolution(const std::string& value) {
    if (value == "VGA")    return sl::RESOLUTION::VGA;
    if (value == "SVGA")   return sl::RESOLUTION::SVGA;
    if (value == "HD720")  return sl::RESOLUTION::HD720;
    if (value == "HD1080") return sl::RESOLUTION::HD1080;
    if (value == "HD2K")   return sl::RESOLUTION::HD2K;
    throw std::runtime_error("ZEDSource: unknown resolution: " + value);
}

} // namespace

void ZEDSource::init(const ZEDConfig& cfg) {
    stop_requested_.store(false);

    sl::InitParameters init_params;
    init_params.coordinate_units  = parse_coordinate_units(cfg.coordinate_units);
    init_params.coordinate_system = parse_coordinate_system(cfg.coordinate_system);
    init_params.depth_mode        = parse_depth_mode(cfg.depth_mode);
    init_params.camera_resolution = parse_resolution(cfg.resolution);
    init_params.camera_fps        = cfg.fps;

    if (!cfg.svo_path.empty()) {
        init_params.input.setFromSVOFile(cfg.svo_path.c_str());
        init_params.svo_real_time_mode = cfg.svo_real_time;
    }

    const sl::ERROR_CODE open_err = camera_.open(init_params);
    if (open_err != sl::ERROR_CODE::SUCCESS) {
        std::cerr << "[zed] open failed: " << sl::toString(open_err).c_str() << "\n";
        throw std::runtime_error("ZEDSource: failed to open camera");
    }

    const sl::ERROR_CODE tracking_err =
        camera_.enablePositionalTracking(sl::PositionalTrackingParameters());
    if (tracking_err != sl::ERROR_CODE::SUCCESS) {
        camera_.close();
        std::cerr << "[zed] enablePositionalTracking failed: "
                  << sl::toString(tracking_err).c_str() << "\n";
        throw std::runtime_error("ZEDSource: failed to enable positional tracking");
    }

    const auto camera_info = camera_.getCameraInformation();
    width_ = camera_info.camera_configuration.resolution.width;
    height_ = camera_info.camera_configuration.resolution.height;
    frame_skip_ = cfg.frame_skip;

    std::fprintf(stderr, "[zed] opened resolution: %dx%d\n", width_, height_);

    point_cloud_.alloc(width_, height_, sl::MAT_TYPE::F32_C4, sl::MEM::GPU);

    std::fprintf(stderr, "[zed] mode: %s | svo: %s\n",
                 cfg.svo_path.empty() ? "live" : "svo",
                 cfg.svo_path.empty() ? "" : cfg.svo_path.c_str());
}

bool ZEDSource::capture(FrameData& frame) {
    if (stop_requested_.load()) {
        return false;
    }

    if (frame_skip_ > 0) {
        for (int i = 0; i < frame_skip_; ++i) {
            if (camera_.grab() != sl::ERROR_CODE::SUCCESS) {
                return false;
            }
        }
    }
    if (camera_.grab() != sl::ERROR_CODE::SUCCESS) {
        return false;
    }

    // Retrieve XYZRGBA directly into the GPU mat — no CPU round-trip.
    if (camera_.retrieveMeasure(point_cloud_, sl::MEASURE::XYZRGBA, sl::MEM::GPU)
            != sl::ERROR_CODE::SUCCESS) {
        return false;
    }

    // Retrieve CPU-side left image for the encode stage (BGRA, U8_C4).
    if (camera_.retrieveImage(frame.image_raw, sl::VIEW::LEFT, sl::MEM::CPU)
            != sl::ERROR_CODE::SUCCESS) {
        return false;
    }

    if (frame.raw_points.ptr != nullptr && frame.raw_points.ptr_owned_) {
        cudaFree(frame.raw_points.ptr);
    }

    frame.raw_points.ptr = reinterpret_cast<float4*>(
        point_cloud_.getPtr<sl::float4>(sl::MEM::GPU));
    if (frame.raw_points.ptr == nullptr) {
        frame.raw_points.count = 0;
        frame.raw_count = 0;
        return false;
    }

    frame.raw_points.ptr_owned_ = false;
    frame.raw_points.count      = width_ * height_;
    frame.raw_count             = width_ * height_;

    sl::Pose zed_pose;
    camera_.getPosition(zed_pose);
    const sl::Orientation ori = zed_pose.getOrientation();
    frame.camera_pose = Quaternion{ori.ox, ori.oy, ori.oz, ori.ow};

    frame.timestamp_ns = camera_.getTimestamp(sl::TIME_REFERENCE::IMAGE).getNanoseconds();

    return true;
}

void ZEDSource::request_stop() noexcept {
    stop_requested_.store(true);
}

void ZEDSource::shutdown() {
    point_cloud_.free();
    camera_.disablePositionalTracking();
    camera_.close();
    std::fprintf(stderr, "[zed] shutdown\n");
}
