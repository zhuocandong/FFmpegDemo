#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

static int PrepareIntputFrame(AVCodecContext *mVideoCodecCtx, AVFrame *mFrame, FILE *infile)
{
    int ret = 0;
    static int enc_count = 0;

    //prepare input yuv
    fflush(stdout);
    /* make sure the frame data is writable */
    ret = av_frame_make_writable(mFrame);
    if (ret < 0)
    {
        // throw std::runtime_error("Error: fail to av_frame_make_writable");
        fprintf(stderr, "Error: fail to av_frame_make_writable\n");
        return -1;
    }
    /* prepare a dummy image */
    unsigned char y_data[mVideoCodecCtx->width * mVideoCodecCtx->height];
    unsigned char u_data[mVideoCodecCtx->width * mVideoCodecCtx->height / 4];
    unsigned char v_data[mVideoCodecCtx->width * mVideoCodecCtx->height / 4];
    /* Y */
    ret = fread(y_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height, infile);
    if (ret <= 0)
    {
        // throw std::runtime_error("End Of File");
        fprintf(stderr, "End Of File\n");
        return -1;
    }

    memcpy(mFrame->data[0], y_data, mVideoCodecCtx->width * mVideoCodecCtx->height);

    /* Cb and Cr */
    fread(u_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height / 4, infile);
    memcpy(mFrame->data[1], u_data, mVideoCodecCtx->width * mVideoCodecCtx->height / 4);

    fread(v_data, 1, mVideoCodecCtx->width * mVideoCodecCtx->height / 4, infile);
    memcpy(mFrame->data[2], v_data, mVideoCodecCtx->width * mVideoCodecCtx->height / 4);

    mFrame->pts = enc_count++;
    /* Now Ready to encode the image */

    return ret;
}

int main(int argc, char *argv[])
{
    if (argc <= 4)
    {
        std::cout << argv[0] << " <input YUVfile> <input PCMfile> <codec name> <output url>" << std::endl;
        exit(-1);
    }

    /* Parameters Definition */
    const char *inputV, *inputA, *output, *codec_name;
    inputV = argv[1];
    inputA = argv[2];
    codec_name = argv[3];
    output = argv[4];

    AVCodec *mVideoCodec;
    AVCodecContext *mVideoCodecCtx;
    AVPacket *mPacket;
    AVFrame *mFrame;

    AVCodec *mAudioCodec;
    AVCodecContext *mAudioCodecCtx;
    AVPacket *mAudioPacket;
    AVFrame *mAudioFrame;
    SwrContext *mAudioSwrCtx;

    /* Output Stream */
    AVFormatContext *mFormatCtx;
    AVStream *OutVideoStream;
    AVStream *OutAudioStream;

    uint8_t *mAudioFrameBuf; // Get From User
    int mAudioInFrameLength = 0;
    int mAudioOutFrameLength = 0; // Length of A Frame
    uint8_t **mAudioConvertData;

    int mVideoStreamIndex = -1;
    int mAudioStreamIndex = -1;
    int ret;

    av_register_all();
    avformat_network_init();

    /* ================== Video Encoder Init ================== */
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
    mVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;   //add PPS???SPS

    av_opt_set(mVideoCodecCtx->priv_data, "preset", "ultrafast", 0);    //?????????????????????????????????
    //av_opt_set(mVideoCodecCtx->priv_data, "tune", "zerolatency", 0);  //?????????????????????????????????????????????,?????????????????????
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

    /* ================== Audio Encoder Init ================== */
    mAudioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!mAudioCodec)
    {
        // fprintf(stderr, "Codec '%s' not found\n", codec_name);
        throw std::runtime_error("Error: fail to find encoder");
    }

    mAudioCodecCtx = avcodec_alloc_context3(mAudioCodec);
    if (!mAudioCodecCtx)
        throw std::runtime_error("Error: Could not allocate audio codec context");

    /* put sample parameters */
    mAudioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    mAudioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    mAudioCodecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
    mAudioCodecCtx->channels = av_get_channel_layout_nb_channels(mAudioCodecCtx->channel_layout);
    mAudioCodecCtx->sample_rate = 44100;
    mAudioCodecCtx->bit_rate = 128000;

    ret = avcodec_open2(mAudioCodecCtx, mAudioCodec, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open audio codec: %d\n", ret);
        throw std::runtime_error("Error: Could not open audio codec");
    }

    mAudioPacket = av_packet_alloc();
    if (!mAudioPacket)
        throw std::runtime_error("Error: Could not allocate audio packet");

    mAudioFrame = av_frame_alloc();
    if (!mAudioFrame)
        throw std::runtime_error("Error: Could not allocate audio frame");

    mAudioFrame->format = mAudioCodecCtx->sample_fmt;
    mAudioFrame->nb_samples = mAudioCodecCtx->frame_size;
    mAudioFrame->channels = 2;

#if 1
    ret = av_frame_get_buffer(mAudioFrame, 32);
    if (ret < 0)
        throw std::runtime_error("Error: Could not allocate the audio frame data");
#endif

    // PCM ReSample: Input PCM must be 44100kHz + signed_16_bits + double_channels
    // Change: S16 -> FLTP
    mAudioSwrCtx = swr_alloc();

    swr_alloc_set_opts(mAudioSwrCtx,
                       av_get_default_channel_layout(mAudioCodecCtx->channels),
                       mAudioCodecCtx->sample_fmt,
                       mAudioCodecCtx->sample_rate,
                       av_get_default_channel_layout(mAudioFrame->channels),
                       AV_SAMPLE_FMT_S16, /* Src PCM Sample Format */
                       44100, 0, NULL);
    swr_init(mAudioSwrCtx);

    /* ???????????? */
    mAudioConvertData = (uint8_t **)calloc(mAudioCodecCtx->channels, sizeof(*mAudioConvertData)); // ????????????????????????????????????buffer??????
    av_samples_alloc(mAudioConvertData, NULL, mAudioCodecCtx->channels, mAudioCodecCtx->frame_size, mAudioCodecCtx->sample_fmt, 0);

#if 1
    int audio_size = av_samples_get_buffer_size(NULL, mAudioCodecCtx->channels, mAudioCodecCtx->frame_size, mAudioCodecCtx->sample_fmt, 1);
    mAudioFrameBuf = (uint8_t *)av_malloc(audio_size); // ????????????buf???AVFrame????????????????????????pcm???????????????buf??????????????????pcm?????????????????????AVFrame???????????????
    avcodec_fill_audio_frame(mAudioFrame, mAudioCodecCtx->channels, mAudioCodecCtx->sample_fmt, (const uint8_t *)mAudioFrameBuf, audio_size, 1);
#endif

    /* ==================================== */

    /*================== Pusher Init ==================*/
    ret = avformat_alloc_output_context2(&mFormatCtx, NULL, "RTSP", output); //RTSP
    if (ret < 0 || !mFormatCtx)
        throw std::runtime_error("Error: fail to create output context");

    av_opt_set(mFormatCtx->priv_data, "rtsp_transport", "tcp", 0); //Transport With TCP

#if 0
    //???????????????????????????????????????????????????????????????max_interleave_delta??????
    mFormatCtx->max_interleave_delta = 1000000;
#endif

    /* Create Video output AVStream */
    OutVideoStream = avformat_new_stream(mFormatCtx, mVideoCodec);
    if (!OutVideoStream)
        throw std::runtime_error("Error: fail to allocating output stream");

#if 1
    OutVideoStream->time_base = {1, 25};
    mVideoStreamIndex = OutVideoStream->id = mFormatCtx->nb_streams - 1; //?????????fmt_ctx???
#endif

    /* Copy the settings of AVCodecContext */
    // ret = avcodec_copy_context(OutVideoStream->codec, mVideoCodecCtx);
    ret = avcodec_parameters_from_context(OutVideoStream->codecpar, mVideoCodecCtx);
    if (ret < 0)
        throw std::runtime_error("Error: fail to avcodec_copy_context");

    OutVideoStream->codec->codec_tag = 0;
    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        OutVideoStream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /* Create Audio output AVStream */
    OutAudioStream = avformat_new_stream(mFormatCtx, mAudioCodec);
    if (!OutAudioStream)
        throw std::runtime_error("Error: fail to allocating output stream");

#if 1
    OutAudioStream->time_base = {1, 25};
    mAudioStreamIndex = OutAudioStream->id = mFormatCtx->nb_streams - 1; //?????????fmt_ctx???
#endif

    /* Copy the settings of AVCodecContext */
    // ret = avcodec_copy_context(OutAudioStream->codec, mAudioCodecCtx);
    ret = avcodec_parameters_from_context(OutAudioStream->codecpar, mAudioCodecCtx);
    if (ret < 0)
        throw std::runtime_error("Error: fail to avcodec_copy_context");

    OutAudioStream->codec->codec_tag = 0;
    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        OutAudioStream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

#if 0
    avcodec_parameters_from_context(OutAudioStream->codecpar, mAudioCodecCtx);
#endif

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

#if 1 // Important~ From avformat_new_stream() * When muxing, should be called by the user before avformat_write_header().
    /* Write file header */
    ret = avformat_write_header(mFormatCtx, NULL);
    if (ret < 0)
        throw std::runtime_error("Error occurred when opening output URL");

    long start_time = av_gettime();
#endif
    /*====================================*/

    /*================== Encode Frame To Packet And Push Streaming ==================*/

#if 1 // extra
    FILE *fp_out, *fp_YUV, *fp_PCM;
    // fp_out = fopen("out.h264", "wb");
    fp_YUV = fopen(inputV, "rb");
    fp_PCM = fopen(inputA, "rb");

    if (!fp_YUV || !fp_PCM)
        throw std::runtime_error("Error: fail to open file");

    int counts = 0;
    int frame_index = 0;
#endif

    while (1)
    {
        av_usleep(1000 * 20);
        /* Read from local yuv file */
        if (PrepareIntputFrame(mVideoCodecCtx, mFrame, fp_YUV) < 0)
            break;

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
            // printf("Write Video packet %3" PRId64 " (size=%5d)\n", mPacket->pts, mPacket->size);
            // fwrite(mPacket->data, 1, mPacket->size, fp_out);

#if 1 //push video~~
            mPacket->stream_index = mVideoStreamIndex;
            if (mPacket->pts == AV_NOPTS_VALUE)
            {
                //Write PTS
                AVRational time_base1 = mVideoCodecCtx->time_base; // ifmt_ctx->streams[videoindex]->time_base;
                //Duration between 2 frames (us)
                int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(mVideoCodecCtx->framerate /*ifmt_ctx->streams[videoindex]->r_frame_rate*/);
                //Parameters
                mPacket->pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
                mPacket->dts = mPacket->pts;
                mPacket->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
            }

            //Important:Delay
            if (mPacket->stream_index == mVideoStreamIndex)
            {
                AVRational time_base = mVideoCodecCtx->time_base; // ifmt_ctx->streams[videoindex]->time_base;
                AVRational time_base_q = {1, AV_TIME_BASE};
                int64_t pts_time = av_rescale_q(mPacket->dts, time_base, time_base_q);
                int64_t now_time = av_gettime() - start_time;
                if (pts_time > now_time)
                    av_usleep(pts_time - now_time);
            }

            /* copy packet */
            //Convert PTS/DTS
            mPacket->pts = av_rescale_q_rnd(mPacket->pts, mVideoCodecCtx->time_base, OutVideoStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            mPacket->dts = av_rescale_q_rnd(mPacket->dts, mVideoCodecCtx->time_base, OutVideoStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            mPacket->duration = av_rescale_q(mPacket->duration, mVideoCodecCtx->time_base, OutVideoStream->time_base);
            mPacket->pos = -1;
            //Print to Screen
            if (mPacket->stream_index == mVideoStreamIndex)
            {
                printf("Send %8d video frames to output URL\n", frame_index);
                frame_index++;
            }

            ret = av_interleaved_write_frame(mFormatCtx, mPacket);

            if (ret < 0)
            {
                printf("Error muxing packet\n");
                break;
            }
#endif

            av_packet_unref(mPacket);
        }

        /* Add Aduio Data*/
        // ???????????????????????????
        mAudioInFrameLength = mAudioFrame->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * mAudioFrame->channels;

        /* Read from local pcm file */
        if (fread(mAudioFrameBuf, 1, mAudioInFrameLength, fp_PCM) <= 0) // Or Use: if (fread(mAudioFrame->data[0], 1, mAudioInFrameLength, fp_PCM) <= 0)
        {
            printf("failed to read raw data\n");
            return -1;
        }
        else if (feof(fp_PCM))
        {
            break;
        }

        /* PCM Convert: Input_S16 to Output_FLTP */
        swr_convert(mAudioSwrCtx, mAudioConvertData, mAudioCodecCtx->frame_size, (const uint8_t **)mAudioFrame->data, mAudioFrame->nb_samples);

        // ???????????????????????????
        mAudioOutFrameLength = mAudioCodecCtx->frame_size * av_get_bytes_per_sample(mAudioCodecCtx->sample_fmt);
        // ???????????????????????????AAC???????????????
        memcpy(mAudioFrame->data[0], mAudioConvertData[0], mAudioOutFrameLength);
        memcpy(mAudioFrame->data[1], mAudioConvertData[1], mAudioOutFrameLength);

        mAudioFrame->pts = counts++ /* *100 */;

        /* send the frame to the encoder */
        if (mAudioFrame)
            printf("Send Audio frame %3" PRId64 "\n", mAudioFrame->pts);
        ret = avcodec_send_frame(mAudioCodecCtx, mAudioFrame);
        if (ret < 0)
            throw std::runtime_error("Error sending a frame for encoding\n");

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(mAudioCodecCtx, mAudioPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0)
            {
                fprintf(stderr, "Error during encoding\n");
                break;
            }

            /* Handle Encoded Packet Data */
            // printf("Write Audio packet %3" PRId64 " (size=%5d)\n", mAudioPacket->pts, mAudioPacket->size);

            // uint8_t aac_header[7];
            // get_adts_header(mAudioCodecCtx, aac_header, mAudioPacket->size); // Fill adts header
            // fwrite(aac_header, 1, 7, fp_out);
            // fwrite(mAudioPacket->data, 1, mAudioPacket->size, fp_out);

#if 1 //push audio~~
            mAudioPacket->stream_index = mAudioStreamIndex;
            if (mAudioPacket->pts == AV_NOPTS_VALUE)
            {
                //Write PTS
                AVRational time_base1 = mAudioCodecCtx->time_base; // ifmt_ctx->streams[audioindex]->time_base;
                //Duration between 2 frames (us)
                int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(mAudioCodecCtx->framerate /*ifmt_ctx->streams[audioindex]->r_frame_rate*/);
                //Parameters
                mAudioPacket->pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
                mAudioPacket->dts = mAudioPacket->pts;
                mAudioPacket->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
            }

            //Important:Delay
            if (mAudioPacket->stream_index == mAudioStreamIndex)
            {
                AVRational time_base = mAudioCodecCtx->time_base; // ifmt_ctx->streams[audioindex]->time_base;
                AVRational time_base_q = {1, AV_TIME_BASE};
                int64_t pts_time = av_rescale_q(mAudioPacket->dts, time_base, time_base_q);
                int64_t now_time = av_gettime() - start_time;
                if (pts_time > now_time)
                    av_usleep(pts_time - now_time);
            }

            /* copy packet */
            //Convert PTS/DTS
            mAudioPacket->pts = av_rescale_q_rnd(mAudioPacket->pts, mAudioCodecCtx->time_base, OutAudioStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            mAudioPacket->dts = av_rescale_q_rnd(mAudioPacket->dts, mAudioCodecCtx->time_base, OutAudioStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            mAudioPacket->duration = av_rescale_q(mAudioPacket->duration, mAudioCodecCtx->time_base, OutAudioStream->time_base);
            mAudioPacket->pos = -1;
            //Print to Screen
            if (mAudioPacket->stream_index == mAudioStreamIndex)
            {
                printf("Send %8d audio frames to output URL\n", frame_index);
                frame_index++;
            }

            ret = av_interleaved_write_frame(mFormatCtx, mAudioPacket);

            if (ret < 0)
            {
                printf("Error muxing packet\n");
                break;
            }
#endif

            av_packet_unref(mAudioPacket);
        }
    }

    /* Release resources */
    if (mFrame)
        av_frame_free(&mFrame);
    if (mPacket)
        av_packet_free(&mPacket);
    if (mVideoCodecCtx)
        avcodec_free_context(&mVideoCodecCtx);
    if (mAudioFrameBuf)
        free(mAudioFrameBuf);
    if (mAudioConvertData)
        av_freep(&mAudioConvertData[0]);
    if (mAudioFrame)
        av_frame_free(&mAudioFrame);
    if (mAudioPacket)
        av_packet_free(&mAudioPacket);
    if (mAudioSwrCtx)
        swr_free(&mAudioSwrCtx);
    if (mAudioCodecCtx)
        avcodec_free_context(&mAudioCodecCtx);
    if (mFormatCtx)
    {
        if (!(mFormatCtx->oformat->flags & AVFMT_NOFILE))
            avio_close(mFormatCtx->pb);
        avformat_free_context(mFormatCtx);
    }

    if (fp_YUV)
        fclose(fp_YUV);
    if (fp_PCM)
        fclose(fp_PCM);

    return 0;
}
