#pragma once
#ifndef TRAVERSABILITY_POSE_HPP
#define TRAVERSABILITY_POSE_HPP

enum class TrackingState {
    OK,
    SEARCHING,
    FPS_TOO_LOW,
    SEARCHING_FLOOR_PLANE,
    UNAVAILABLE,
    LOOP_CLOSED,
};

struct CameraPose {
    float tx{0}, ty{0}, tz{0};         // translation in metres
    float qx{0}, qy{0}, qz{0}, qw{1}; // orientation quaternion
};

#endif // TRAVERSABILITY_POSE_HPP
