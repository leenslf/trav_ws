cmake_minimum_required(VERSION 3.16)
project(ZedQt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

set(LIB_ROOT "${CMAKE_SOURCE_DIR}/../lib")

add_executable(zed_qt
    src/main.cpp
    src/comm_map_receiver.cpp
    src/polar_grid_widget.cpp
    include/comm_map_receiver.h
    include/polar_grid_widget.h
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
set_source_files_properties(src/comm_map_receiver.cpp
    PROPERTIES COMPILE_FLAGS "-fpermissive"
)

# ASan + UBSan support — opt-in via -DENABLE_SANITIZERS=ON (default OFF).
# This target is pure C++ (no CUDA), so flags apply unconditionally to all sources.
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan for CPU code" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(zed_qt PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(zed_qt PRIVATE
        -fsanitize=address,undefined
    )
endif()
