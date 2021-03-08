#include <iostream>
#include <unistd.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#define SAVE_YUV

#ifdef SAVE_YUV
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

    if(AV_PIX_FMT_YUV420P == codec_ctx->pix_fmt)
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
    else if(AV_PIX_FMT_NV12 == codec_ctx->pix_fmt)
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
#endif

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        std::cout << argv[0] << " <rtsp://xxxx/xxx> " << "<codec_name>" << std::endl;
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

    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i)
    {
        const AVStream *stream = format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            fprintf(stdout, "type of the encoded data: %d, dimensions of the video frame in pixels: width: %d, height: %d, pixel format: %d\n",
                    stream->codecpar->codec_id, stream->codecpar->width, stream->codecpar->height, stream->codecpar->format);
        }
    }

    if (video_stream_index == -1)
    {
        fprintf(stderr, "Cannot find a video stream\n");
        return -1;
    }

#ifndef USE_DEFAULT_DECODER
    /*h264_cuvid*/
    const char *codec_name = argv[2]; //"h264_cuvid";
    // AVCodec *codec = find_codec_or_die(codec_name, AVMEDIA_TYPE_VIDEO, 0);
    AVCodec *codec = avcodec_find_decoder_by_name(codec_name);
    if (!codec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder %s\n", codec_name);
        return -1;
    }
#else
    /*default decoder*/
    AVCodecParameters *codecpar = format_ctx->streams[video_stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec)
    {
        fprintf(stderr, "fail to avcodec_find_decoder\n");
        return -1;
    }
#endif

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* open decoder */
    if (ret = avcodec_open2(codec_ctx, codec, nullptr) < 0)
    {
        fprintf(stderr, "Could not open codec: %d\n", ret);
        return -1;
    }

    /* Not Needed in x86, but mDstWidth and mDstHeight need it*/
    avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar);


    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        fprintf(stderr, "Could not allocate video packet\n");
        return -1;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        return -1;
    }

#ifdef SAVE_YUV
    FILE *fp_YUV;
    if ((fp_YUV = fopen("pic.yuv", "wb")) == NULL) //YUV save filename
        return 0;
#endif

    /*Decode Packet To Frame*/
    while (1)
    {
        ret = av_read_frame(format_ctx, packet); //read the next frame
        if (ret >= 0 && packet->stream_index == video_stream_index)
        {
            ret = avcodec_send_packet(codec_ctx, packet); // decode
            if (ret < 0)
            {
                fprintf(stderr, "Error sending a packet for decoding: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(codec_ctx, frame);
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
#ifdef SAVE_YUV
                printf("saving frame %3d\n", codec_ctx->frame_number);
                fflush(stdout);
                Save_Yuv(codec_ctx, frame, fp_YUV);
#endif
            }
        }
        av_packet_unref(packet);
    }

    /* Release resources */
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);

    return 0;
}
