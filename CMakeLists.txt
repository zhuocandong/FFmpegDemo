cmake_minimum_required(VERSION 3.10)

project(ffmpeg_stream)

# set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-g")

add_subdirectory(in)

add_subdirectory(out)