cmake_minimum_required(VERSION 3.16)
project(ZedQt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

set(LIB_ROOT "${CMAKE_SOURCE_DIR}/../lib")

add_executable(zed_qt
    src/main.cpp
    src/comm_receiver.cpp
    src/polar_grid_widget.cpp
    src/jpeg_viewer_widget.cpp
    include/comm_receiver.h
    include/polar_grid_widget.h
    include/jpeg_viewer_widget.h
    include/frame_data.h
)

target_include_directories(zed_qt PRIVATE
    include
    ${LIB_ROOT}/include
)

target_link_libraries(zed_qt
    Qt5::Widgets
    ${LIB_ROOT}/libcomm.a
    ${LIB_ROOT}/libutils.a
    pthread
    ssl
    crypto
    argon2
    rt
)

target_compile_options(zed_qt PRIVATE -Wall -Wextra)

# libcomm headers use old-style C++ patterns that require -fpermissive
set_source_files_properties(
    src/comm_receiver.cpp
    PROPERTIES COMPILE_FLAGS "-fpermissive"
)

# --- global_map: local→world geometry module and its unit test ---
# Not yet linked into zed_qt; integration is a separate phase.
add_executable(test_local_to_world
    src/global_map/local_to_world.cpp
    src/global_map/test_local_to_world.cpp
)
target_include_directories(test_local_to_world PRIVATE include)
target_compile_options(test_local_to_world PRIVATE -Wall -Wextra)

# --- global_map: container + fusion rules and their unit test ---
# fusion_rules.h includes traversability/frame_result.hpp (no Qt/CUDA deps).
# Not yet linked into zed_qt; integration is a separate phase.
set(TRAV_INCLUDE "${CMAKE_SOURCE_DIR}/../include")
add_executable(test_global_map
    src/global_map/global_map.cpp
    src/global_map/fusion_rules.cpp
    src/global_map/test_global_map.cpp
)
target_include_directories(test_global_map PRIVATE include ${TRAV_INCLUDE})
target_compile_options(test_global_map PRIVATE -Wall -Wextra)
