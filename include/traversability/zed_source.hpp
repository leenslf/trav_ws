#pragma once
#ifndef TRAVERSABILITY_ZED_SOURCE_HPP
#define TRAVERSABILITY_ZED_SOURCE_HPP

#include <sl/Camera.hpp>

#include "traversability/config.hpp"
#include "traversability/frame_data.hpp"

class IZEDSource {
public:
    virtual ~IZEDSource() = default;
    virtual void init(const ZEDConfig& cfg) = 0;
    virtual bool capture(FrameData& frame) = 0;
    virtual void shutdown() = 0;
};

class ZEDLiveSource : public IZEDSource {
public:
    void init(const ZEDConfig& cfg) override;
    bool capture(FrameData& frame) override;
    void shutdown() override;

private:
    sl::Camera  camera_;
    sl::Mat     point_cloud_;   // GPU mat, reused every frame
    int         width_{0};
    int         height_{0};
    int         frame_skip_{0};
    int         frame_counter_{0};
};

class ZEDFileSource : public IZEDSource {
public:
    void init(const ZEDConfig& cfg) override;
    bool capture(FrameData& frame) override;
    void shutdown() override;
};

#endif // TRAVERSABILITY_ZED_SOURCE_HPP
