cmake_minimum_required(VERSION 3.18)
project(traversability_pipeline LANGUAGES CXX CUDA)

if(POLICY CMP0146)
    cmake_policy(SET CMP0146 OLD)
endif()

if(POLICY CMP0153)
    cmake_policy(SET CMP0153 OLD)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_language(CUDA)

find_package(CUDA REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(ZED REQUIRED)
find_package(OpenCV REQUIRED)

set(LIB_ROOT "${CMAKE_SOURCE_DIR}/lib")

set(SOURCES
    src/config.cpp
    src/consumers/comm_sender.cpp
    src/consumers/disk_write_consumer.cpp
    src/consumers/network_streamer.cpp
    src/consumers/null_consumer.cpp
    src/frame_data.cpp
    src/main.cpp
    src/pipeline_runner.cpp
    src/stages/encode_image.cpp
    src/stages/extract_xyz.cu
    src/stages/polarize.cu
    src/stages/tilt_compensate.cu
    src/stages/traversability.cu
    src/stages/voxel_filter.cu
    src/zed_source.cpp
)

add_executable(traversability_pipeline ${SOURCES})

target_include_directories(traversability_pipeline PRIVATE
    include/
    ${ZED_INCLUDE_DIRS}
    ${EIGEN3_INCLUDE_DIR}
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${LIB_ROOT}/include
)

target_link_libraries(traversability_pipeline PRIVATE
    ${ZED_LIBRARIES}
    yaml-cpp
    Eigen3::Eigen
    ${CUDA_LIBRARIES}
    ${OpenCV_LIBS}
    ${LIB_ROOT}/libcomm.a
    ${LIB_ROOT}/libutils.a
    pthread
    ssl
    crypto
    argon2
    rt
)

# libcomm headers use old-style C++ patterns that require -fpermissive
set_source_files_properties(src/consumers/comm_sender.cpp
    PROPERTIES COMPILE_FLAGS "-fpermissive"
)

# --- polar_zones: pure zone-computation function (declared in
# traversability/stages/traversability.hpp) and its unit test.
# Not linked into traversability_pipeline.
add_executable(test_polar_zones
    src/stages/test_polar_zones.cpp
)
target_include_directories(test_polar_zones PRIVATE
    include/
    ${ZED_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIRS}
    ${LIB_ROOT}/include
)
target_compile_options(test_polar_zones PRIVATE -Wall -Wextra)

# --- seam_neighbor: zone-seam index-mapping math (map_theta_to_row /
# neighbor_flat_idx / build_row_tables, declared alongside PolarZone in
# traversability/stages/traversability.hpp) and its unit test.
# Not linked into traversability_pipeline.
add_executable(test_seam_neighbor
    src/stages/test_seam_neighbor.cpp
)
target_include_directories(test_seam_neighbor PRIVATE
    include/
    ${ZED_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIRS}
    ${LIB_ROOT}/include
)
target_compile_options(test_seam_neighbor PRIVATE -Wall -Wextra)
