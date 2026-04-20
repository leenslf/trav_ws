cmake_minimum_required(VERSION 3.16)
project(ZedQt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)

add_executable(zed_qt
    src/main.cpp
    src/udp_receiver.cpp
    src/polar_grid_widget.cpp
    include/udp_receiver.h
    include/polar_grid_widget.h
    include/frame_data.h
    include/protocol.h
)

target_include_directories(zed_qt PRIVATE include)

target_link_libraries(zed_qt
    Qt5::Widgets
    Qt5::Network
)

target_compile_options(zed_qt PRIVATE -Wall -Wextra)
