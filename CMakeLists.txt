cmake_minimum_required(VERSION 3.10)
project(miniaudio_output VERSION 0.0.2 DESCRIPTION "miniaudio output library for Retrovert")

include_directories("retrovert_api/c")

add_library(miniaudio_output SHARED "miniaudio_output.c") 
