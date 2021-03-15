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

add_executable(video_out rtsp_encode_push_video.cpp)

target_include_directories(video_out
    PRIVATE
)

target_link_libraries(video_out
    avcodec
    avformat
    avutil
)

add_executable(audio_out rtsp_encode_push_audio.cpp)

target_include_directories(audio_out
    PRIVATE
)

target_link_libraries(audio_out
    avcodec
    avformat
    avutil
    swresample
)

add_executable(all_out rtsp_encode_push_all.cpp)

target_include_directories(all_out
    PRIVATE
)

target_link_libraries(all_out
    avcodec
    avformat
    avutil
    swresample
)