cmake_minimum_required(VERSION 3.10)

project(rtsp_enc_out)

# set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-g")

# find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
# find_library(AVCODEC_LIBRARY avcodec)

# find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
# find_library(AVFORMAT_LIBRARY avformat)

# find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
# find_library(AVUTIL_LIBRARY avutil)

add_executable(encode ffmpeg_enc.c)

target_include_directories(encode
    PRIVATE
)

target_link_libraries(encode
    avcodec
    avformat
    avutil
)

add_executable(push leixh_push.cpp)

target_include_directories(push
    PRIVATE
)

target_link_libraries(push
    avcodec
    avformat
    avutil
)

add_executable(out rtsp_encode_push.cpp)

target_include_directories(out
    PRIVATE
)

target_link_libraries(out
    avcodec
    avformat
    avutil
)