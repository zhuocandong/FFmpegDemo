#include <iostream>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

int main(int argc, char **argv)
{
    av_register_all();
    avformat_network_init(); //初始化网络模块

    AVDictionary *options = nullptr;
    // const char *url = "rtsp://172.16.96.117:8554/live/test"; //rtsp地址
    // const char* url = "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov";
    const char *url = argv[1];

    AVFormatContext *format_ctx = avformat_alloc_context(); //定义封装结构体

    /*将输入流媒体流链接为封装结构体的句柄，将url句柄挂载至format_ctx结构里，之后ffmpeg即可对format_ctx进行操作*/
    int ret = avformat_open_input(&format_ctx, url, nullptr, &options);
    if (ret != 0)
    {
        fprintf(stderr, "Fail to open url: %s, return value: %d\n", url, ret);
        return -1;
    }

    /*查找音视频流信息，从format_ctx中建立输入文件对应的流信息*/
    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot find input stream information: %d\n", ret);
        return -1;
    }

    int video_stream_index = -1;
    int audio_stream_index = -1;
    /*穷举所有的音视频流信息，找出流编码类型为AVMEDIA_TYPE_VIDEO对应的索引*/
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i)
    {
        const AVStream *stream = format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            fprintf(stdout, "type of the encoded data: %d, dimensions of the video frame in pixels: width: %d, height: %d, pixel format: %d\n",
                    stream->codecpar->codec_id, stream->codecpar->width, stream->codecpar->height, stream->codecpar->format);
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
            fprintf(stdout, "type of the encoded data: %d, dimensions of the video frame in pixels: width: %d, height: %d, pixel format: %d\n",
                    stream->codecpar->codec_id, stream->codecpar->width, stream->codecpar->height, stream->codecpar->format);
        }
    }

    // if (video_stream_index == -1 || audio_stream_index == -1)
    // {
    //     fprintf(stderr, "Cannot find a video/audio stream\n");
    //     return -1;
    // }

    // /*查找解码器*/
    // /*首先从输入的AVFormatContext中得到stream,然后从stream中根据编码器的CodeID获得对应的Decoder*/
    // AVCodecParameters *codecpar = format_ctx->streams[video_stream_index]->codecpar;
    // const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    // if (!codec)
    // {
    //     fprintf(stderr, "fail to avcodec_find_decoder\n");
    //     return -1;
    // }

    // AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    // if (!codec_ctx)
    // {
    //     fprintf(stderr, "Could not allocate video codec context\n");
    //     return -1;
    // }

    // /* 打开解码器 */
    // if (ret = avcodec_open2(codec_ctx, codec, nullptr) < 0)
    // {
    //     fprintf(stderr, "Could not open codec: %d\n", ret);
    //     return -1;
    // }
    // printf("decodec name: %s\n", codec->name);

    /*查找audio解码器*/
    /*首先从输入的AVFormatContext中得到stream,然后从stream中根据编码器的CodeID获得对应的Decoder*/
    AVCodecParameters *ad_codecpar = format_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *ad_codec = avcodec_find_decoder(ad_codecpar->codec_id);
    if (!ad_codec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder\n");
        return -1;
    }

    AVCodecContext *ad_codec_ctx = avcodec_alloc_context3(ad_codec);
    if (!ad_codec_ctx)
    {
        fprintf(stderr, "Could not allocate audio codec context\n");
        return -1;
    }

    /* 打开audio解码器 */
    if (ret = avcodec_open2(ad_codec_ctx, ad_codec, nullptr) < 0)
    {
        fprintf(stderr, "Could not open codec: %d\n", ret);
        return -1;
    }
    printf("decodec name: %s\n", ad_codec->name);

    AVFrame *frame = av_frame_alloc(); //用于存放解码后的数据
    if (!frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        return -1;
    }

    AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket)); //用于存放音视频数据包
    if (!packet)
    {
        fprintf(stderr, "Could not allocate video packet\n");
        return -1;
    }

    /************ Audio Convert ************/
    //frame->16bit 44100 PCM 统一音频采样格式与采样率
    //创建swrcontext上下文件
    SwrContext *swrContext = swr_alloc();
    //音频格式  输入的采样设置参数
    AVSampleFormat inFormat = ad_codec_ctx->sample_fmt;
    // 出入的采样格式
    AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;
    // 输入采样率
    int inSampleRate = ad_codec_ctx->sample_rate ? ad_codec_ctx->sample_rate : (format_ctx->streams[audio_stream_index]->codecpar->sample_rate);
    // 输出采样率
    int outSampleRate = 44100;
    // 输入声道布局
    uint64_t in_ch_layout = ad_codec_ctx->channel_layout ? ad_codec_ctx->channel_layout : (format_ctx->streams[audio_stream_index]->codecpar->channel_layout);
    //输出声道布局
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    //给Swrcontext 分配空间，设置公共参数
    swr_alloc_set_opts(swrContext, out_ch_layout, outFormat, outSampleRate,
                       in_ch_layout, inFormat, inSampleRate, 0, NULL);
    // 初始化
    swr_init(swrContext);
    // 获取声道数量
    int outChannelCount = av_get_channel_layout_nb_channels(out_ch_layout);

    int currentIndex = 0;
    printf("声道数量%d \n", outChannelCount);
    // 设置音频缓冲区间 16bit   44100  PCM数据, 双声道
    uint8_t *out_buffer = (uint8_t *)av_malloc(2 * 44100);
    // 创建pcm的文件对象
    FILE *fp_pcm = fopen("xxx.pcm", "wb");
    /************ Audio Convert ************/

#define DEC_default
#if defined DEC_default
    while (1)
    {
        ret = av_read_frame(format_ctx, packet); //读取下一帧数据
        // if (ret >= 0 && packet->stream_index == video_stream_index)
        // {
        //     ret = avcodec_send_packet(codec_ctx, packet); //解码
        //     if (ret < 0)
        //     {
        //         fprintf(stderr, "Error sending a packet for decoding: %d\n", ret);
        //         av_packet_unref(packet);
        //         continue;
        //     }

        //     ret = avcodec_receive_frame(codec_ctx, frame); //接收获取解码收的数据
        //     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        //     {
        //         av_packet_unref(packet);
        //         continue;
        //     }
        //     else if (ret < 0)
        //     {
        //         fprintf(stderr, "fail to avcodec_receive_frame\n");
        //         av_packet_unref(packet);
        //         continue;
        //     }
        // }
        if (ret >= 0 && packet->stream_index == audio_stream_index)
        {
            ret = avcodec_send_packet(ad_codec_ctx, packet); //解码
            if (ret < 0)
            {
                fprintf(stderr, "Error sending a packet for decoding: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }
            ret = avcodec_receive_frame(ad_codec_ctx, frame); //接收获取解码收的数据
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                av_packet_unref(packet);
                continue;
            }
            else if (ret < 0)
            {
                fprintf(stderr, "fail to avcodec_receive_frame\n");
                av_packet_unref(packet);
                continue;
            }

            //将每一帧数据转换成pcm
            swr_convert(swrContext, &out_buffer, 2 * 44100,
                        (const uint8_t **)frame->data, frame->nb_samples);
            //获取实际的缓存大小
            int out_buffer_size = av_samples_get_buffer_size(NULL, outChannelCount, frame->nb_samples, outFormat, 1);
            // 写入文件
            fwrite(out_buffer, 1, out_buffer_size, fp_pcm);
        }

        av_packet_unref(packet);
    }
/*     int got_frame = 0;
    while (0)
    {
        ret = av_read_frame(format_ctx, packet); //读取下一帧数据
        if (ret >= 0 && packet->stream_index == audio_stream_index)
        {
            //解码音频帧
            if (avcodec_decode_audio4(ad_codec_ctx, frame, &got_frame, packet) < 0)
            {
                printf("decode Audio error.\n");
                return -1;
            }
            if (got_frame)
            {
                if (frame->format == AV_SAMPLE_FMT_S16P) //signed 16 bits, planar 16位 平面数据
                {
                    //AV_SAMPLE_FMT_S16P
                    //代表每个data[]的数据是连续的（planar），每个单位是16bits
                    for (int i = 0; i < frame->linesize[0]; i += 2)
                    {
                        //如果是多通道的话，保存成c1低位、c1高位、c2低位、c2高位...
                        for (int j = 0; j < frame->channels; ++j)
                            fwrite(frame->data[j] + i, 2, 1, fp_pcm);
                    }
                }
                else if (frame->format == AV_SAMPLE_FMT_FLTP)
                {
                    for (int i = 0; i < frame->linesize[0]; i += 4)
                    {
                        for (int j = 0; j < frame->channels; ++j)
                            fwrite(frame->data[j] + i, 4, 1, fp_pcm);
                    }
                }
            }
        }
        av_packet_unref(packet);
    } */
#elif defined DEC_two
    int got_frame = 0;
    while (1)
    {
        ret = av_read_frame(format_ctx, packet); //读取下一帧数据
        if (ret >= 0 && packet->stream_index == video_stream_index)
        {
            if (0 > (ret = avcodec_decode_video2(codec_ctx, frame,
                                                 &got_frame, packet)))
            {
                printf("avcodec_decode_video() ret[%ld].\n", ret);
                return -1;
            }
            if (0 == got_frame)
                printf("have no frame data\n");
        }
        av_packet_unref(packet);
    }
#else
    while (1)
    {
        ;
    }
#endif

exit_label:
    av_frame_free(&frame);
    av_dict_free(&options);
    avcodec_free_context(&ad_codec_ctx);
    avformat_close_input(&format_ctx);
    av_freep(packet);

    fprintf(stdout, "test finish\n");
    return 0;
}