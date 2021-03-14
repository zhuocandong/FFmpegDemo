#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

static void get_adts_header(AVCodecContext *ctx, uint8_t *adts_header, int aac_length);

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        std::cout << argv[0] << " <input PCMfile> <output url>" << std::endl;
        exit(-1);
    }

    /* Parameters Definition */
    const char *input, *output;
    input = argv[1];
    output = argv[2];

    AVCodec *mAudioCodec;
    AVCodecContext *mAudioCodecCtx;
    AVPacket *mAudioPacket;
    AVFrame *mAudioFrame;
    SwrContext *mAudioSwrCtx;

    /* Output Stream */
    AVFormatContext *mFormatCtx;
    AVStream *OutAudioStream;

    uint8_t *mAudioFrameBuf; // Get From User
    int mAudioInFrameLength = 0;
    int mAudioOutFrameLength = 0; // Length of A Frame
    uint8_t **mAudioConvertData;  // Convert to AVFrame Data

    int mAudioStreamIndex = -1;
    int ret;

    av_register_all();
    avformat_network_init();

    /* ================== Encoder Init ================== */
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

    /* 分配空间 */
    mAudioConvertData = (uint8_t **)calloc(mAudioCodecCtx->channels, sizeof(*mAudioConvertData)); // 根据通道数分配待转换数据buffer指针
    av_samples_alloc(mAudioConvertData, NULL, mAudioCodecCtx->channels, mAudioCodecCtx->frame_size, mAudioCodecCtx->sample_fmt, 0);

#if 1
    int audio_size = av_samples_get_buffer_size(NULL, mAudioCodecCtx->channels, mAudioCodecCtx->frame_size, mAudioCodecCtx->sample_fmt, 1);
    mAudioFrameBuf = (uint8_t *)av_malloc(audio_size); // 申请一个buf与AVFrame建立关联，后续将pcm数据填入该buf（经测试，把pcm数据直接填充到AVFrame中也可以）
    avcodec_fill_audio_frame(mAudioFrame, mAudioCodecCtx->channels, mAudioCodecCtx->sample_fmt, (const uint8_t *)mAudioFrameBuf, audio_size, 1);
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

    /* Create Audio output AVStream */
    OutAudioStream = avformat_new_stream(mFormatCtx, mAudioCodec);
    if (!OutAudioStream)
        throw std::runtime_error("Error: fail to allocating output stream");

#if 1
    OutAudioStream->time_base = {1, 25};
    mAudioStreamIndex = OutAudioStream->id = mFormatCtx->nb_streams - 1; //加入到fmt_ctx流
#endif

    /* Copy the settings of AVCodecContext */
    ret = avcodec_copy_context(OutAudioStream->codec, mAudioCodecCtx);
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

    /*================== Encode Frame To Packet ==================*/

#if 1
    FILE *fp_out, *fp_PCM;
    fp_out = fopen("out.aac", "wb");
    fp_PCM = fopen(input, "rb");

    if (!fp_out || !fp_PCM)
        throw std::runtime_error("Error: fail to open file");

    int counts = 0;
    int frame_index = 0;
#endif

    while (1)
    {
        av_usleep(1000 * 20);
        // 输入一帧数据的长度
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

        // 输出一帧数据的长度
        mAudioOutFrameLength = mAudioCodecCtx->frame_size * av_get_bytes_per_sample(mAudioCodecCtx->sample_fmt);
        // 双通道赋值（输出的AAC为双通道）
        memcpy(mAudioFrame->data[0], mAudioConvertData[0], mAudioOutFrameLength);
        memcpy(mAudioFrame->data[1], mAudioConvertData[1], mAudioOutFrameLength);

        mAudioFrame->pts = counts++ /* *100 */;

        /* send the frame to the encoder */
        if (mAudioFrame)
            printf("Send frame %3" PRId64 "\n", mAudioFrame->pts);
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
            printf("Write packet %3" PRId64 " (size=%5d)\n", mAudioPacket->pts, mAudioPacket->size);

            uint8_t aac_header[7];
            get_adts_header(mAudioCodecCtx, aac_header, mAudioPacket->size); // Fill adts header
            fwrite(aac_header, 1, 7, fp_out);
            fwrite(mAudioPacket->data, 1, mAudioPacket->size, fp_out);

#if 1 //push~~
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

    if (fp_PCM)
        fclose(fp_PCM);
    if (fp_out)
        fclose(fp_out);

    return 0;

    return 0;
}

static void get_adts_header(AVCodecContext *ctx, uint8_t *adts_header, int aac_length)
{
    uint8_t freq_idx = 0; //0: 96000 Hz  3: 48000 Hz 4: 44100 Hz
    switch (ctx->sample_rate)
    {
    case 96000:
        freq_idx = 0;
        break;
    case 88200:
        freq_idx = 1;
        break;
    case 64000:
        freq_idx = 2;
        break;
    case 48000:
        freq_idx = 3;
        break;
    case 44100:
        freq_idx = 4;
        break;
    case 32000:
        freq_idx = 5;
        break;
    case 24000:
        freq_idx = 6;
        break;
    case 22050:
        freq_idx = 7;
        break;
    case 16000:
        freq_idx = 8;
        break;
    case 12000:
        freq_idx = 9;
        break;
    case 11025:
        freq_idx = 10;
        break;
    case 8000:
        freq_idx = 11;
        break;
    case 7350:
        freq_idx = 12;
        break;
    default:
        freq_idx = 4;
        break;
    }
    uint8_t chanCfg = ctx->channels;
    uint32_t frame_length = aac_length + 7;
    adts_header[0] = 0xFF;
    adts_header[1] = 0xF1;
    adts_header[2] = ((ctx->profile) << 6) + (freq_idx << 2) + (chanCfg >> 2);
    adts_header[3] = (((chanCfg & 3) << 6) + (frame_length >> 11));
    adts_header[4] = ((frame_length & 0x7FF) >> 3);
    adts_header[5] = (((frame_length & 7) << 5) + 0x1F);
    adts_header[6] = 0xFC;
}