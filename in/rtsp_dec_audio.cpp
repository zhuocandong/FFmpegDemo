#include <iostream>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

int main(int argc, char **argv)
{

    if (argc <= 1)
    {
        std::cout << argv[0] << " <rtsp://xxxx/xxx> " << std::endl;
        exit(-1);
    }

    av_register_all();
    avformat_network_init(); // 初始化网络模块

    int index = 0;
    // const char *url = "rtsp://172.16.96.117:8554/live/test"; // rtsp地址
    // const char* url = "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov";
    const char *url = argv[1];

    AVFormatContext *format_ctx = avformat_alloc_context(); // 定义封装结构体

    /* 将输入流媒体流链接为封装结构体的句柄，将url句柄挂载至format_ctx结构里，之后ffmpeg即可对format_ctx进行操作 */
    int ret = avformat_open_input(&format_ctx, url, nullptr, nullptr);
    if (ret != 0)
    {
        fprintf(stderr, "Fail to open url: %s, return value: %d\n", url, ret);
        return -1;
    }

    /* 查找音视频流信息，从format_ctx中建立输入文件对应的流信息 */
    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot find input stream information: %d\n", ret);
        return -1;
    }

    int audio_stream_index = -1;
    /* 穷举所有的音视频流信息，找出流编码类型为AVMEDIA_TYPE_AUDIO对应的索引 */
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i)
    {
        const AVStream *stream = format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
            fprintf(stdout, "type of the encoded data: %d, sample_rate:%d, channels:%d, bits per sample:%d, sample format:%d \n",
                    stream->codecpar->codec_id, stream->codecpar->sample_rate, stream->codecpar->channels,
                    stream->codecpar->bits_per_coded_sample, stream->codecpar->format);
        }
    }

    if (audio_stream_index == -1)
    {
        fprintf(stderr, "Cannot find a audio stream\n");
        return -1;
    }

    // Dump valid information onto standard error
    av_dump_format(format_ctx, 0, url, false);

    /* 查找audio解码器 */
    /* 首先从输入的AVFormatContext中得到stream,然后从stream中根据编码器的CodeID获得对应的Decoder */
    AVCodecParameters *ad_codecpar = format_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *ad_codec = avcodec_find_decoder(ad_codecpar->codec_id);
    if (!ad_codec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder\n");
        return -1;
    }

    // AVCodecContext *ad_codec_ctx = avcodec_alloc_context3(ad_codec); // 这种申请方式 + avcodec_parameters_to_context接口无法获取到完整的音频信息
    AVCodecContext *ad_codec_ctx = format_ctx->streams[audio_stream_index]->codec;
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

    // avcodec_parameters_to_context(ad_codec_ctx, format_ctx->streams[audio_stream_index]->codecpar);

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

#ifndef NON_DEFAULT
    /************ Audio Convert ************/
    // frame->16bit 44100 PCM 统一音频采样格式与采样率
    // 创建swrcontext上下文件
    SwrContext *swrContext = swr_alloc();
    // 音频格式  输入的采样设置参数
    AVSampleFormat inFormat = ad_codec_ctx->sample_fmt;
    // 出入的采样格式
    AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;
    // 输入采样率
    int inSampleRate = ad_codec_ctx->sample_rate ? ad_codec_ctx->sample_rate : (format_ctx->streams[audio_stream_index]->codecpar->sample_rate);
    // 输出采样率（若输入采样率高于输出采样率，会有音质损失）
    int outSampleRate = 44100;
    // 输入声道布局
    uint64_t in_ch_layout = ad_codec_ctx->channel_layout ? ad_codec_ctx->channel_layout : (format_ctx->streams[audio_stream_index]->codecpar->channel_layout);
    // 输出声道布局
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    // 给Swrcontext 分配空间，设置公共参数
    swr_alloc_set_opts(swrContext, out_ch_layout, outFormat, outSampleRate,
                       in_ch_layout, inFormat, inSampleRate, 0, NULL);
    // 初始化
    swr_init(swrContext);
    // 获取声道数量
    int outChannelCount = av_get_channel_layout_nb_channels(out_ch_layout);

    // 设置音频缓冲区间 16bit   44100  PCM数据, 双声道
    uint8_t *out_buffer = (uint8_t *)av_malloc(2 * 44100);
    // 创建pcm的文件对象
    FILE *fp_pcm = fopen("audio.pcm", "wb");
    /************ Audio Convert ************/

#else

    //Out Audio Param
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    //nb_samples: AAC-1024 MP3-1152
    int out_nb_samples = ad_codec_ctx->frame_size;
    AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;
    int out_sample_rate = 44100;
    int outChannelCount = av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size
    int out_buffer_size = av_samples_get_buffer_size(NULL, outChannelCount, out_nb_samples, outFormat, 1);

    uint8_t *out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    // pFrame = av_frame_alloc();

    //FIX:Some Codec's Context Information is missing
    uint64_t in_channel_layout = av_get_default_channel_layout(ad_codec_ctx->channels);
    //Swr
    SwrContext *swrContext = swr_alloc();
    swrContext = swr_alloc_set_opts(swrContext, out_channel_layout, outFormat, out_sample_rate,
                                    in_channel_layout, ad_codec_ctx->sample_fmt, ad_codec_ctx->sample_rate, 0, NULL);
    swr_init(swrContext);
    FILE *fp_pcm = fopen("audio.pcm", "wb");
#endif

#ifndef NON_DEFAULT
    while (1)
    {
        ret = av_read_frame(format_ctx, packet); // 读取下一帧数据

        if (ret >= 0 && packet->stream_index == audio_stream_index)
        {
            ret = avcodec_send_packet(ad_codec_ctx, packet); // 解码
            if (ret < 0)
            {
                fprintf(stderr, "Error sending a packet for decoding: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(ad_codec_ctx, frame); // 接收获取解码收的数据
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    av_packet_unref(packet);
                    break;
                }
                else if (ret < 0)
                {
                    fprintf(stderr, "Error during decoding: %d\n", ret);
                    av_packet_unref(packet);
                    break;
                }

                // 将每一帧数据转换成pcm
                swr_convert(swrContext, &out_buffer, 2 * 44100,
                            (const uint8_t **)frame->data, frame->nb_samples);
                // 获取实际的缓存大小
                int out_buffer_size = av_samples_get_buffer_size(NULL, outChannelCount, frame->nb_samples, outFormat, 1);

                printf("index:%5d\t pts:%lld\t packet size:%d\n", index++, packet->pts, packet->size);
                // 写入文件
                fwrite(out_buffer, 1, out_buffer_size, fp_pcm);
            }
        }
        else if (ret < 0)
            break;
        av_packet_unref(packet);
    }

#else

    int got_picture = 0;
    while (av_read_frame(format_ctx, packet) >= 0)
    {
        if (packet->stream_index == audio_stream_index)
        {

            ret = avcodec_decode_audio4(ad_codec_ctx, frame, &got_picture, packet);
            if (ret < 0)
            {
                printf("Error in decoding audio frame.\n");
                return -1;
            }
            if (got_picture > 0)
            {
                swr_convert(swrContext, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)frame->data, frame->nb_samples);

                //获取实际的缓存大小
                int out_buffer_size = av_samples_get_buffer_size(NULL, outChannelCount, frame->nb_samples, outFormat, 1);

                printf("index:%5d\t pts:%lld\t packet size:%d\n", index, packet->pts, packet->size);
                //Write PCM
                fwrite(out_buffer, 1, out_buffer_size, fp_pcm);
                index++;
            }
        }
        av_free_packet(packet);
    }
#endif

    /* Release resources */
    av_frame_free(&frame);
    av_packet_free(&packet);
    // avcodec_free_context(&ad_codec_ctx); // if use avcodec_alloc_context3()
    avformat_close_input(&format_ctx);

    return 0;
}