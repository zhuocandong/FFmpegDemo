cmake_minimum_required(VERSION 3.10)

project(rtsp_enc_out)

# set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-g")

add_executable(video_enc ffmpeg_enc_video.c)

target_include_directories(video_enc
    PRIVATE
)

target_link_libraries(video_enc
    avcodec
    avformat
    avutil
)

add_executable(audio_enc ffmpeg_enc_audio.cpp)

target_include_directories(audio_enc
    PRIVATE
)

target_link_libraries(audio_enc
    avcodec
    avformat
    avutil
    swresample
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