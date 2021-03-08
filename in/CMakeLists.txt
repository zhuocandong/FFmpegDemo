cmake_minimum_required(VERSION 3.10)

project(rtsp_dec_in)

# set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-g")

# find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
# find_library(AVCODEC_LIBRARY avcodec)

# find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
# find_library(AVFORMAT_LIBRARY avformat)

# find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
# find_library(AVUTIL_LIBRARY avutil)

add_executable(decode rtsp_decode.cpp)

add_executable(audio audio.cpp)

target_include_directories(decode
    PRIVATE

)

target_link_libraries(decode
    avcodec
    avformat
    avutil
)

target_include_directories(audio
    PRIVATE

)

target_link_libraries(audio
    avcodec
    avformat
    avutil
    swresample
)
