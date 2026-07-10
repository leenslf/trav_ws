#include "traversability/config.hpp"
#include <cstdio>
#include <yaml-cpp/yaml.h>
#include <iostream>

PipelineConfig PipelineConfig::defaults() {
    return PipelineConfig{};
}

std::pair<int,int> zed_resolution_to_dims(const std::string& res) {
    if (res == "VGA")    return {672, 376};
    if (res == "SVGA")   return {800, 600};
    if (res == "HD720")  return {1280, 720};
    if (res == "HD1080") return {1920, 1080};
    if (res == "HD2K")   return {2208, 1242};
    fprintf(stderr, "[config] unrecognised resolution '%s' — defaulting to HD1080\n", res.c_str());
    return {1920, 1080};
}

PipelineConfig PipelineConfig::load_from_file(const std::string& path) {
    PipelineConfig cfg;
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception& e) {
        std::cerr << "[config] failed to load " << path << ": " << e.what()
                  << " — using defaults\n";
        return cfg;
    }

    if (root["consumer"]) {
        cfg.consumer = root["consumer"].as<std::string>();
    }

    if (auto n = root["zed"]) {
        if (n["coordinate_units"])  cfg.zed.coordinate_units  = n["coordinate_units"].as<std::string>();
        if (n["coordinate_system"]) cfg.zed.coordinate_system = n["coordinate_system"].as<std::string>();
        if (n["depth_mode"])        cfg.zed.depth_mode        = n["depth_mode"].as<std::string>();
        if (n["resolution"])        cfg.zed.resolution        = n["resolution"].as<std::string>();
        if (n["svo_path"])          cfg.zed.svo_path          = n["svo_path"].as<std::string>();
        if (n["svo_real_time"])     cfg.zed.svo_real_time     = n["svo_real_time"].as<bool>();
        if (n["fps"])               cfg.zed.fps               = n["fps"].as<int>();
        if (n["frame_skip"])            cfg.zed.frame_skip            = n["frame_skip"].as<int>();
        if (n["tracking_mode"])         cfg.zed.tracking_mode         = n["tracking_mode"].as<std::string>();
        if (n["enable_area_memory"])    cfg.zed.enable_area_memory    = n["enable_area_memory"].as<bool>();
        if (n["enable_imu_fusion"])     cfg.zed.enable_imu_fusion     = n["enable_imu_fusion"].as<bool>();
        if (n["enable_pose_smoothing"]) cfg.zed.enable_pose_smoothing = n["enable_pose_smoothing"].as<bool>();
        const auto dims = zed_resolution_to_dims(cfg.zed.resolution);
        cfg.zed.w = dims.first;
        cfg.zed.h = dims.second;
    }
    if (auto n = root["voxel_filter"]) {
        if (n["voxel_size_x"])          cfg.voxel_filter.voxel_size_x         = n["voxel_size_x"].as<float>();
        if (n["voxel_size_y"])          cfg.voxel_filter.voxel_size_y         = n["voxel_size_y"].as<float>();
        if (n["voxel_size_z"])          cfg.voxel_filter.voxel_size_z         = n["voxel_size_z"].as<float>();
        if (n["min_points_per_voxel"])  cfg.voxel_filter.min_points_per_voxel = n["min_points_per_voxel"].as<int>();
    }
    if (auto n = root["polarize"]) {
        if (n["z_threshold"]) cfg.polarize.z_threshold = n["z_threshold"].as<float>();
        if (n["min_range"])   cfg.polarize.min_range   = n["min_range"].as<float>();
    }
    if (auto n = root["traversability"]) {
        if (n["r_min_m"])                    cfg.traversability.r_min_m                    = n["r_min_m"].as<float>();
        if (n["r_max_m"])                    cfg.traversability.r_max_m                    = n["r_max_m"].as<float>();
        if (n["theta_min_deg"])              cfg.traversability.theta_min_deg              = n["theta_min_deg"].as<float>();
        if (n["theta_max_deg"])              cfg.traversability.theta_max_deg              = n["theta_max_deg"].as<float>();
        if (n["danger_threshold"])           cfg.traversability.danger_threshold           = n["danger_threshold"].as<float>();
        if (n["scrit_deg"])                  cfg.traversability.scrit_deg                  = n["scrit_deg"].as<float>();
        if (n["rcrit_m"])                    cfg.traversability.rcrit_m                    = n["rcrit_m"].as<float>();
        if (n["hcrit_m"])                    cfg.traversability.hcrit_m                    = n["hcrit_m"].as<float>();
        if (n["polar_grid_size_r_m"])        cfg.traversability.polar_grid_size_r_m        = n["polar_grid_size_r_m"].as<float>();
        if (n["polar_grid_size_theta_deg"])  cfg.traversability.polar_grid_size_theta_deg  = n["polar_grid_size_theta_deg"].as<float>();
        if (n["adaptive_theta_merge"])       cfg.traversability.adaptive_theta_merge       = n["adaptive_theta_merge"].as<bool>();
    }
    if (auto n = root["image_encode"]) {
        if (n["scale"])        cfg.image_encode.scale        = n["scale"].as<float>();
        if (n["jpeg_quality"]) cfg.image_encode.jpeg_quality = n["jpeg_quality"].as<int>();
    }
    if (auto n = root["disk_writer"]) {
        if (n["output_dir"])   cfg.disk_writer.output_dir   = n["output_dir"].as<std::string>();
        if (n["write_images"]) cfg.disk_writer.write_images = n["write_images"].as<bool>();
    }
    if (auto n = root["comm_sender"]) {
        if (n["remote_ip"]) cfg.comm_sender.remote_ip = n["remote_ip"].as<std::string>();
    }

    return cfg;
}
