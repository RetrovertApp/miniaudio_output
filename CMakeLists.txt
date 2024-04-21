cmake_minimum_required(VERSION 3.10)
include(${CMAKE_SOURCE_DIR}/../scripts/CMakeSettings.txt)

project(miniaudio_output VERSION 0.0.2 DESCRIPTION "miniaudio output library for Retrovert")

include_directories("retrovert_api/c")

add_library(miniaudio_output SHARED "miniaudio_output.c") 

set_custom_library_extension(miniaudio_output)
