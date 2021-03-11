#include <iostream>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

int Save_Yuv(AVCodecContext *codec_ctx, AVFrame *frame, FILE *fp_YUV)
{
    long src_width = 0, src_height = 0;
    long dst_width = 0, dst_height = 0;
    long num_bytes = 0, s_size = 0;
    static int ynum = 0;
    unsigned char *yuv_data = NULL;
    yuv_data = (unsigned char *)malloc(sizeof(unsigned char) * 4000000);

    src_width = codec_ctx->width;
    src_height = codec_ctx->height;

    dst_width = (src_width + 7) & -8;
    dst_height = (src_height + 7) & -8;

    num_bytes = (dst_width * dst_height) + (dst_width * dst_height / 2);

    s_size = dst_width * dst_height;

    if (AV_PIX_FMT_YUV420P == codec_ctx->pix_fmt)
    {
        int j = 0;
        for (int i = 0; i < dst_height; i++)
        {
            memcpy(yuv_data + i * dst_width,
                   frame->data[0] + i * frame->linesize[0],
                   dst_width);
        }

        for (int i = 0; i < dst_height / 2; i++)
        {
            memcpy(yuv_data + s_size + i * dst_width / 2,
                   frame->data[1] + i * frame->linesize[1],
                   dst_width / 2);
            memcpy(yuv_data + s_size + s_size / 4 + i * dst_width / 2,
                   frame->data[2] + i * frame->linesize[2],
                   dst_width / 2);
        }
    }
    else if (AV_PIX_FMT_NV12 == codec_ctx->pix_fmt)
    {
        int j = 0;
        for (int i = 0; i < dst_height; i++)
        {
            memcpy(yuv_data + i * dst_width,
                   frame->data[0] + i * frame->linesize[0],
                   dst_width);
        }

        for (int i = 0; i < dst_height / 2; i++)
        {
            memcpy(yuv_data + s_size + i * dst_width,
                   frame->data[1] + i * frame->linesize[1],
                   dst_width);
        }
    }
    fwrite(yuv_data, 1, num_bytes, fp_YUV);
}

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        std::cout << argv[0] << " <rtsp://xxxx/xxx> " << std::endl;
        exit(-1);
    }

    av_register_all();
    avformat_network_init();

    const char *url = argv[1];

    AVFormatContext *format_ctx = avformat_alloc_context();

    int ret = avformat_open_input(&format_ctx, url, nullptr, nullptr);
    if (ret != 0)
    {
        fprintf(stderr, "Fail to open url: %s, return value: %d\n", url, ret);
        return -1;
    }

    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0)
    {
        fprintf(stderr, "Cannot find input stream information: %d\n", ret);
        return -1;
    }

    int video_stream_index = -1;
    int audio_stream_index = -1;

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
            fprintf(stdout, "type of the encoded data: %d, sample_rate:%d, channels:%d, bits per sample:%d, frame_size:%d, sample format:%d \n",
                    stream->codecpar->codec_id, stream->codecpar->sample_rate, stream->codecpar->channels,
                    stream->codecpar->bits_per_coded_sample, stream->codecpar->frame_size, stream->codecpar->format);
        }
    }

    if (video_stream_index == -1)
    {
        fprintf(stderr, "Cannot find a video stream\n");
        return -1;
    }

    if (audio_stream_index == -1)
    {
        fprintf(stderr, "Cannot find a audio stream\n");
        return -1;
    }

    av_dump_format(format_ctx, 0, url, false);

    /* ======== Video Init ======== */
#ifdef USE_EXTRA_DECODER
    /* h264 / h264_cuvid / h264_nvv4l2dec / h264_nvmpi */
    const char *video_codec_name = argv[2];
    AVCodec *vcodec = avcodec_find_decoder_by_name(video_codec_name);
    if (!vcodec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder %s\n", video_codec_name);
        return -1;
    }
#else
    /*default decoder*/
    AVCodecParameters *vcodecpar = format_ctx->streams[video_stream_index]->codecpar;
    const AVCodec *vcodec = avcodec_find_decoder(vcodecpar->codec_id);
    if (!vcodec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder - video\n");
        return -1;
    }
#endif

    /* use avcodec_alloc_context3() and avcodec_parameters_to_context() couldn't get all complete information, Error will be reported when decoding mp4 */
    // AVCodecContext *vcodec_ctx = avcodec_alloc_context3(vcodec);

    AVCodecContext *vcodec_ctx = format_ctx->streams[video_stream_index]->codec;
    if (!vcodec_ctx)
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* open video decoder */
    if (ret = avcodec_open2(vcodec_ctx, vcodec, nullptr) < 0)
    {
        fprintf(stderr, "Could not open video codec: %d\n", ret);
        return -1;
    }

    /* ======== Video Init Finished ======== */

    /* ======== Audio Init ======== */
    AVCodecParameters *acodecpar = format_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *acodec = avcodec_find_decoder(acodecpar->codec_id);
    if (!acodec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder  - audio\n");
        return -1;
    }

    /* use avcodec_alloc_context3() and avcodec_parameters_to_context() couldn't get all complete information, Error will be reported when decoding mp4 */
    // AVCodecContext *acodec_ctx = avcodec_alloc_context3(acodec);

    AVCodecContext *acodec_ctx = format_ctx->streams[audio_stream_index]->codec;
    if (!acodec_ctx)
    {
        fprintf(stderr, "Could not allocate audio codec context\n");
        return -1;
    }

    /* open audio decoder */
    if (ret = avcodec_open2(acodec_ctx, acodec, nullptr) < 0)
    {
        fprintf(stderr, "Could not open audio codec: %d\n", ret);
        return -1;
    }

    /* ======== Audio Init Finished ======== */

    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        fprintf(stderr, "Could not allocate packet\n");
        return -1;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate frame\n");
        return -1;
    }

    /************ Audio Convert ************/
    // frame->16bit 44100 PCM 统一音频采样格式与采样率
    // 创建swrcontext上下文件
    SwrContext *swrContext = swr_alloc();
    // 音频格式  输入的采样设置参数
    AVSampleFormat inFormat = acodec_ctx->sample_fmt;
    // 出入的采样格式
    AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;
    // 输入采样率
    int inSampleRate = acodec_ctx->sample_rate ? acodec_ctx->sample_rate : (format_ctx->streams[audio_stream_index]->codecpar->sample_rate);
    // 输出采样率
    int outSampleRate = 44100;
    // 输入声道布局
    uint64_t in_ch_layout = acodec_ctx->channel_layout ? acodec_ctx->channel_layout : (format_ctx->streams[audio_stream_index]->codecpar->channel_layout);
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

    FILE *fp_YUV;
    if ((fp_YUV = fopen("pic.yuv", "wb")) == NULL) //YUV save filename
        return 0;

    /*Decode Packet To Frame*/
    while (ret = av_read_frame(format_ctx, packet) >= 0) //read the next frame
    {
        if (packet->stream_index == video_stream_index)
        {
            ret = avcodec_send_packet(vcodec_ctx, packet); // decode
            if (ret < 0)
            {
                fprintf(stderr, "Error sending a packet for decoding: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(vcodec_ctx, frame);
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

                printf("VideoFrame %5d\t pts:%lld\t packet size:%d\n", vcodec_ctx->frame_number, packet->pts, packet->size);
                fflush(stdout);
                Save_Yuv(vcodec_ctx, frame, fp_YUV);
            }
        }
        else if (packet->stream_index == audio_stream_index)
        {
            ret = avcodec_send_packet(acodec_ctx, packet); // decode
            if (ret < 0)
            {
                fprintf(stderr, "Error sending a packet for decoding: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(acodec_ctx, frame);
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

                // convert frame data to pcm
                swr_convert(swrContext, &out_buffer, 2 * 44100,
                            (const uint8_t **)frame->data, frame->nb_samples);
                // get the actual cache size
                int out_buffer_size = av_samples_get_buffer_size(NULL, outChannelCount, frame->nb_samples * outSampleRate / inSampleRate, outFormat, 1);

                printf("AudioFrame:\t%5d\t pts:\t%lld\t packet size:\t%d\n", acodec_ctx->frame_number, packet->pts, packet->size);
                // save pcm file
                fwrite(out_buffer, 1, out_buffer_size, fp_pcm);
            }
        }
        av_packet_unref(packet);
    }

    /* Release resources */
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swrContext);
    // avcodec_free_context(&vcodec_ctx); // unless avcodec_alloc_context3() is used
    // avcodec_free_context(&acodec_ctx); // unless avcodec_alloc_context3() is used
    avformat_close_input(&format_ctx);

    return 0;
}
