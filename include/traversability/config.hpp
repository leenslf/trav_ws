#pragma once
#include <string>
#include <utility>

struct ZEDConfig {
    std::string coordinate_units{"METER"};
    std::string coordinate_system{"RIGHT_HANDED_Z_UP_X_FWD"};
    std::string depth_mode{"PERFORMANCE"};
    std::string resolution{"HD720"};
    std::string svo_path{""};
    bool        svo_real_time{false};
    int         fps{30};
    int         w{1280};
    int         h{720};
    int         frame_skip{0};
};

// Returns {width, height} in pixels for a given ZED resolution string.
// Supported values: VGA, HD720, HD1080, HD2K, SVGA.
// Returns {1920, 1080} as a safe default for unrecognised strings.
std::pair<int,int> zed_resolution_to_dims(const std::string& resolution);

struct ExtractXYZConfig {};

struct TiltCompensateConfig {};

struct VoxelFilterConfig {
    float voxel_size_x{0.05f};
    float voxel_size_y{0.05f};
    float voxel_size_z{0.05f};
    int   min_points_per_voxel{3};
};

struct PolarizeConfig {
    float z_threshold{0.45f};
    float min_range{0.1f};
};

struct TraversabilityConfig {
    float r_min_m{0.3f};
    float r_max_m{2.0f};
    float theta_min_deg{-45.0f};
    float theta_max_deg{45.0f};
    float danger_threshold{0.30f};
    float scrit_deg{30.0f};
    float rcrit_m{0.10f};
    float hcrit_m{0.20f};
    float polar_grid_size_r_m{0.10f};
    float polar_grid_size_theta_deg{5.0f};
};

struct ImageEncodeConfig {
    float scale{0.25f};
    int   jpeg_quality{50};
};

struct DiskWriterConfig {
    std::string output_dir{"../output/frames"};
    bool write_images{true};
};

struct CommSenderConfig {
    std::string remote_ip{"127.0.0.1"};
};

struct PipelineConfig {
    ZEDConfig            zed;
    ExtractXYZConfig     extract_xyz;
    TiltCompensateConfig tilt_compensate;
    VoxelFilterConfig    voxel_filter;
    PolarizeConfig       polarize;
    TraversabilityConfig traversability;
    ImageEncodeConfig    image_encode;
    std::string          consumer{"network"};
    DiskWriterConfig     disk_writer;
    CommSenderConfig     comm_sender;

    static PipelineConfig load_from_file(const std::string& path);
    static PipelineConfig defaults();
};
