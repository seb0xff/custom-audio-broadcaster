cmake_minimum_required(VERSION 3.15.3)
project(gst-broadcaster VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 20)

add_library(broadcaster src/broadcaster.cpp src/rtsp_pusher.cpp)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.4)
pkg_search_module(gstreamer-audio REQUIRED IMPORTED_TARGET gstreamer-audio-1.0>=1.4)

target_link_libraries(broadcaster
  PkgConfig::gstreamer
  PkgConfig::gstreamer-audio
)

target_include_directories(broadcaster PUBLIC include/ external/)