cmake_minimum_required(VERSION 3.18)
project(traversability_pipeline LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_language(CUDA)

find_package(CUDA REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(ZED REQUIRED)
find_package(OpenCV REQUIRED)

file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.cu
)

add_executable(traversability_pipeline ${SOURCES})

target_include_directories(traversability_pipeline PRIVATE
    include/
    ${ZED_INCLUDE_DIRS}
    ${EIGEN3_INCLUDE_DIR}
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(traversability_pipeline PRIVATE
    ${ZED_LIBRARIES}
    yaml-cpp
    Eigen3::Eigen
    ${CUDA_LIBRARIES}
    ${OpenCV_LIBS}
)
