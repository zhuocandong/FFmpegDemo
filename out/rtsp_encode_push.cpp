#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
{
    int ret;
    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3" PRId64 "\n", frame->pts);
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0)
        {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }
        printf("Write packet %3" PRId64 " (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int main(int argc, char *argv[])
{
    if (argc <= 3)
    {
        std::cout << argv[0] << " <input file> <codec name> <rtsp://xxxx/xxx>" << std::endl;
        exit(-1);
    }

    /* Parameters Definition */
    const char *input, *output, *codec_name;
    input = argv[1];
    codec_name = argv[2];
    output = argv[3];

    AVCodec *mVideoCodec;
    AVCodecContext *mVideoCodecCtx;
    AVPacket *mPacket;
    AVFrame *mFrame;

    AVFormatContext *mFormatCtx;

    int mVideoStreamIndex = -1;
    int ret;

    // av_register_all();
    avformat_network_init();

    /* ================== Encoder Init ================== */
    mVideoCodec = avcodec_find_encoder_by_name(codec_name);
    if (!mVideoCodec)
    {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        throw std::runtime_error("Error: fail to find encoder");
    }

    mVideoCodecCtx = avcodec_alloc_context3(mVideoCodec);
    if (!mVideoCodecCtx)
        throw std::runtime_error("Error: Could not allocate video codec context");

    /* put sample parameters */
    mVideoCodecCtx->bit_rate = 400000;
    /* resolution must be a multiple of two */
    mVideoCodecCtx->width = 640;  //352;
    mVideoCodecCtx->height = 480; //288;
    /* frames per second */
    mVideoCodecCtx->time_base = (AVRational){1, 25};
    mVideoCodecCtx->framerate = (AVRational){25, 1};

    mVideoCodecCtx->gop_size = 10;
    mVideoCodecCtx->max_b_frames = 1;
    mVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    if (mVideoCodec->id == AV_CODEC_ID_H264)
        av_opt_set(mVideoCodecCtx->priv_data, "preset", "slow", 0);

#if 0
    mVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    mVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    mVideoCodecCtx->channels = 3;
    mVideoCodecCtx->time_base = { 1, 25 };
    mVideoCodecCtx->gop_size = 5;
    mVideoCodecCtx->max_b_frames = 0;
    //mVideoCodecCtx->qcompress = 0.6;
    //mVideoCodecCtx->bit_rate = 90000;
    mVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;   //add PPS、SPS

    av_opt_set(mVideoCodecCtx->priv_data, "preset", "ultrafast", 0);    //快速编码，但会损失质量
    //av_opt_set(mVideoCodecCtx->priv_data, "tune", "zerolatency", 0);  //适用于快速编码和低延迟流式传输,但是会出现绿屏
    //av_opt_set(mVideoCodecCtx->priv_data, "x264opts", "crf=26:vbv-maxrate=728:vbv-bufsize=3640:keyint=25", 0);
#endif

    ret = avcodec_open2(mVideoCodecCtx, mVideoCodec, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open video codec: %d\n", ret);
        throw std::runtime_error("Error: Could not open video codec");
    }

    mPacket = av_packet_alloc();
    if (!mPacket)
        throw std::runtime_error("Error: Could not allocate audio/video packet");

    mFrame = av_frame_alloc();
    if (!mFrame)
        throw std::runtime_error("Error: Could not allocate audio/video frame");

    mFrame->format = mVideoCodecCtx->pix_fmt;
    mFrame->width = mVideoCodecCtx->width;
    mFrame->height = mVideoCodecCtx->height;

#if 1
    ret = av_frame_get_buffer(mFrame, 32);
    if (ret < 0)
        throw std::runtime_error("Error: Could not allocate the audio/video frame data");
#endif
    /* ==================================== */

    /*================== Pusher Init ==================*/
    ret = avformat_alloc_output_context2(&mFormatCtx, NULL, "RTSP", output); //RTSP
    if (ret < 0 || !mFormatCtx)
        throw std::runtime_error("Error: fail to create output context");

    av_opt_set(mFormatCtx->priv_data, "rtsp_transport", "tcp", 0); //Transport With TCP

#if 0
    //检查所有流是否都有数据，如果没有数据会等待max_interleave_delta微秒
    mFormatCtx->max_interleave_delta = 1000000;
#endif

    /* Create Video output AVStream */
    AVStream *OutVideoStream = avformat_new_stream(mFormatCtx, mVideoCodec);
    if (!OutVideoStream)
        throw std::runtime_error("Error: fail to allocating output stream");

#if 0    
    OutVideoStream->time_base = { 1, 25 };
    mVideoStreamIndex = OutVideoStream->id = mFormatCtx->nb_streams - 1;  //加入到fmt_ctx流
#endif

    /* Copy the settings of AVCodecContext */
    ret = avcodec_copy_context(OutVideoStream->codec, mVideoCodecCtx);
    if (ret < 0)
        throw std::runtime_error("Error: fail to avcodec_copy_context");

    OutVideoStream->codec->codec_tag = 0;
    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        OutVideoStream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

#if 0
    avcodec_parameters_from_context(OutVideoStream->codecpar, mVideoCodecCtx);
#endif

    /* Create Audio output AVStream */
    /* @Todo: Audio */

    av_dump_format(mFormatCtx, 0, mFormatCtx->filename, 1); // Dump Format

    /* Open output URL */
    if (!(mFormatCtx->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&mFormatCtx->pb, mFormatCtx->filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            fprintf(stderr, "Could not open output URL '%s'", mFormatCtx->filename);
            throw std::runtime_error("Error: fail to avio_open");
        }
    }

#if 0
    /* Write file header */
    ret = avformat_write_header(mFormatCtx, NULL);
    if (ret < 0)
        throw std::runtime_error("Error occurred when opening output URL");

    long start_time = av_gettime();
#endif
    /*====================================*/

    /*================== Encode Frame To Packet And Push Streaming ==================*/

#if 1 // extra
    FILE *f = fopen("out.h264", "wb");
    if (!f)
    {
        fprintf(stderr, "Could not open %s\n", "out.h264");
        exit(1);
    }
#endif

    while (1)
    {
        /* encode local yuv file to h264 */
        FILE *fp_YUV;
        if ((fp_YUV = fopen(input, "rb")) == NULL) //input YUV filename
            return 0;
        for (int i = 0; i < 250 * 7; i++)
        {
            fflush(stdout);
            /* make sure the frame data is writable */
            ret = av_frame_make_writable(mFrame);
            if (ret < 0)
                exit(1);
            /* prepare a dummy image */
            unsigned char y_data[mVideoCodecCtx->width * mVideoCodecCtx->height];
            unsigned char u_data[mVideoCodecCtx->width * mVideoCodecCtx->height / 4];
            unsigned char v_data[mVideoCodecCtx->width * mVideoCodecCtx->height / 4];
            /* Y */
            ret = fread(y_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height, fp_YUV);
            if (ret <= 0)
            // break;
            {
                printf("End Of File\n");
                break;
            }
            memcpy(mFrame->data[0], y_data, mVideoCodecCtx->width * mVideoCodecCtx->height);

            /* Cb and Cr */
            fread(u_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height / 4, fp_YUV);
            memcpy(mFrame->data[1], u_data, mVideoCodecCtx->width * mVideoCodecCtx->height / 4);

            fread(v_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height / 4, fp_YUV);
            memcpy(mFrame->data[2], v_data, mVideoCodecCtx->width * mVideoCodecCtx->height / 4);

            mFrame->pts = i;
            /* encode the image */
            encode(mVideoCodecCtx, mFrame, mPacket, f);
        }
        /* ret = av_read_frame(format_ctx, packet); //read the next frame
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
            }
        } */
        av_packet_unref(mPacket);
    }

    /* Release resources */

    return 0;
}
