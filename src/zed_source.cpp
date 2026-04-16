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
    throw std::runtime_error("ZEDLiveSource: unknown coordinate_units: " + value);
}

sl::COORDINATE_SYSTEM parse_coordinate_system(const std::string& value) {
    if (value == "RIGHT_HANDED_Z_UP")       return sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP;
    if (value == "RIGHT_HANDED_Z_UP_X_FWD") return sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD;
    if (value == "RIGHT_HANDED_Y_UP")       return sl::COORDINATE_SYSTEM::RIGHT_HANDED_Y_UP;
    if (value == "LEFT_HANDED_Y_UP")        return sl::COORDINATE_SYSTEM::LEFT_HANDED_Y_UP;
    if (value == "LEFT_HANDED_Z_UP")        return sl::COORDINATE_SYSTEM::LEFT_HANDED_Z_UP;
    throw std::runtime_error("ZEDLiveSource: unknown coordinate_system: " + value);
}

sl::DEPTH_MODE parse_depth_mode(const std::string& value) {
    if (value == "PERFORMANCE") return sl::DEPTH_MODE::PERFORMANCE;
    if (value == "QUALITY")     return sl::DEPTH_MODE::QUALITY;
    if (value == "ULTRA")       return sl::DEPTH_MODE::ULTRA;
    if (value == "NEURAL")      return sl::DEPTH_MODE::NEURAL;
    throw std::runtime_error("ZEDLiveSource: unknown depth_mode: " + value);
}

sl::RESOLUTION parse_resolution(const std::string& value) {
    if (value == "VGA")    return sl::RESOLUTION::VGA;
    if (value == "SVGA")   return sl::RESOLUTION::SVGA;
    if (value == "HD720")  return sl::RESOLUTION::HD720;
    if (value == "HD1080") return sl::RESOLUTION::HD1080;
    if (value == "HD2K")   return sl::RESOLUTION::HD2K;
    throw std::runtime_error("ZEDLiveSource: unknown resolution: " + value);
}

} // namespace

// ---------------------------------------------------------------------------
// ZEDLiveSource
// ---------------------------------------------------------------------------

void ZEDLiveSource::init(const ZEDConfig& cfg) {

    sl::InitParameters init_params;
    init_params.coordinate_units  = parse_coordinate_units(cfg.coordinate_units);
    init_params.coordinate_system = parse_coordinate_system(cfg.coordinate_system);
    init_params.depth_mode        = parse_depth_mode(cfg.depth_mode);
    init_params.camera_resolution = parse_resolution(cfg.resolution);
    init_params.camera_fps        = cfg.fps;

    const sl::ERROR_CODE open_err = camera_.open(init_params);
    if (open_err != sl::ERROR_CODE::SUCCESS) {
        std::cerr << "ZEDLiveSource: camera_.open() failed: "
                  << sl::toString(open_err).c_str() << "\n";
        throw std::runtime_error("ZEDLiveSource: failed to open camera");
    }

    const sl::ERROR_CODE tracking_err =
        camera_.enablePositionalTracking(sl::PositionalTrackingParameters());
    if (tracking_err != sl::ERROR_CODE::SUCCESS) {
        camera_.close();
        std::cerr << "ZEDLiveSource: enablePositionalTracking() failed: "
                  << sl::toString(tracking_err).c_str() << "\n";
        throw std::runtime_error("ZEDLiveSource: failed to enable positional tracking");
    }

    width_      = cfg.w;
    height_     = cfg.h;
    frame_skip_ = cfg.frame_skip;
    frame_counter_ = 0;

    point_cloud_.alloc(width_, height_, sl::MAT_TYPE::F32_C4, sl::MEM::GPU);
}

bool ZEDLiveSource::capture(FrameData& frame) {
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
    camera_.getPosition(zed_pose, sl::REFERENCE_FRAME::WORLD);
    const sl::Orientation ori = zed_pose.getOrientation();
    frame.camera_pose = Quaternion{ori.ox, ori.oy, ori.oz, ori.ow};

    frame.timestamp_ns =
        camera_.getTimestamp(sl::TIME_REFERENCE::IMAGE).getNanoseconds();
    ++frame_counter_;

    return true;
}

void ZEDLiveSource::shutdown() {
    point_cloud_.free();
    camera_.disablePositionalTracking();
    camera_.close();
}

// ---------------------------------------------------------------------------
// ZEDFileSource — stub, not yet implemented (i think this can be deleted)
// ---------------------------------------------------------------------------

void ZEDFileSource::init(const ZEDConfig&)    { fprintf(stderr, "ZEDFileSource: init\n"); }
bool ZEDFileSource::capture(FrameData& frame) { fprintf(stderr, "ZEDFileSource: capture\n"); frame.raw_count = 0; return false; }
void ZEDFileSource::shutdown()                {}
