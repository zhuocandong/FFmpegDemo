#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

static void PrepareIntputFrame(AVCodecContext *mVideoCodecCtx, AVFrame *mFrame, FILE *infile)
{
    int ret;
    static int enc_count = 0;

#if 1 //prepare input yuv
    fflush(stdout);
    /* make sure the frame data is writable */
    ret = av_frame_make_writable(mFrame);
    if (ret < 0)
        throw std::runtime_error("Error: fail to av_frame_make_writable");
    /* prepare a dummy image */
    unsigned char y_data[mVideoCodecCtx->width * mVideoCodecCtx->height];
    unsigned char u_data[mVideoCodecCtx->width * mVideoCodecCtx->height / 4];
    unsigned char v_data[mVideoCodecCtx->width * mVideoCodecCtx->height / 4];
    /* Y */
    ret = fread(y_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height, infile);
    if (ret <= 0)
        throw std::runtime_error("End Of File");

    memcpy(mFrame->data[0], y_data, mVideoCodecCtx->width * mVideoCodecCtx->height);

    /* Cb and Cr */
    fread(u_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height / 4, infile);
    memcpy(mFrame->data[1], u_data, mVideoCodecCtx->width * mVideoCodecCtx->height / 4);

    fread(v_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height / 4, infile);
    memcpy(mFrame->data[2], v_data, mVideoCodecCtx->width * mVideoCodecCtx->height / 4);

    mFrame->pts = enc_count++;
    /* Now Ready to encode the image */
    return;
#endif

#if 0
    /* send the frame to the encoder */
    if (mFrame)
        printf("Send frame %3" PRId64 "\n", mFrame->pts);
    ret = avcodec_send_frame(mVideoCodecCtx, mFrame);
    if (ret < 0)
        throw std::runtime_error("Error sending a frame for encoding\n");

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(mVideoCodecCtx, mPacket);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
        {
            fprintf(stderr, "Error during encoding\n");
            break;
        }
        printf("Write packet %3" PRId64 " (size=%5d)\n", mPacket->pts, mPacket->size);
        fwrite(mPacket->data, 1, mPacket->size, outfile);
        av_packet_unref(mPacket);
    }
#endif
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
    FILE *fp_out, *fp_YUV;
    fp_out = fopen("out.h264", "wb");
    fp_YUV = fopen(input, "rb");

    if (!fp_out || !fp_YUV)
        throw std::runtime_error("Error: fail to open file");

#endif

    while (1)
    {
        /* Read from local yuv file */
        PrepareIntputFrame(mVideoCodecCtx, mFrame, fp_YUV);

        /* send the frame to the encoder */
        if (mFrame)
            printf("Send frame %3" PRId64 "\n", mFrame->pts);
        ret = avcodec_send_frame(mVideoCodecCtx, mFrame);
        if (ret < 0)
            throw std::runtime_error("Error sending a frame for encoding\n");

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(mVideoCodecCtx, mPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0)
            {
                fprintf(stderr, "Error during encoding\n");
                break;
            }

            /* Handle Encoded Packet Data */
            printf("Write packet %3" PRId64 " (size=%5d)\n", mPacket->pts, mPacket->size);
            fwrite(mPacket->data, 1, mPacket->size, fp_out);

            av_packet_unref(mPacket);
        }
    }

    /* Release resources */

    return 0;
}
