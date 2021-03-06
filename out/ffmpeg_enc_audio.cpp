#include <iostream>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

static void get_adts_header(AVCodecContext *ctx, uint8_t *adts_header, int aac_length);

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        std::cout << argv[0] << " <input PCMfile> " << std::endl;
        exit(-1);
    }

    /* Parameters Definition */
    const char *input;
    input = argv[1];

    AVCodec *mAudioCodec;
    AVCodecContext *mAudioCodecCtx;
    AVPacket *mAudioPacket;
    AVFrame *mAudioFrame;

    uint8_t *mAudioFrameBuf; // Get From User
    int mAudioInFrameLength = 0;
    int mAudioOutFrameLength = 0; // Length of A Frame
    uint8_t **mAudioConvertData;  // Convert to AVFrame Data

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
        throw std::runtime_error("Error: Could not allocate audio/video packet");

    mAudioFrame = av_frame_alloc();
    if (!mAudioFrame)
        throw std::runtime_error("Error: Could not allocate audio/video frame");

    mAudioFrame->format = mAudioCodecCtx->sample_fmt;
    mAudioFrame->nb_samples = mAudioCodecCtx->frame_size;
    mAudioFrame->channels = 2;

#if 1
    ret = av_frame_get_buffer(mAudioFrame, 32);
    if (ret < 0)
        throw std::runtime_error("Error: Could not allocate the audio/video frame data");
#endif

    // PCM ReSample: Input PCM must be 44100kHz + signed_16_bits + double_channels
    // Change: S16 -> FLTP
    SwrContext *mAudioSwrCtx = swr_alloc();

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

    /*================== Encode Frame To Packet ==================*/

#if 1
    FILE *fp_out, *fp_PCM;
    fp_out = fopen("out.aac", "wb");
    fp_PCM = fopen(input, "rb");

    if (!fp_out || !fp_PCM)
        throw std::runtime_error("Error: fail to open file");

    int counts = 0;
#endif

    while (1)
    {
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

        mAudioFrame->pts = 100 * counts++;

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

    if (fp_PCM)
        fclose(fp_PCM);
    if (fp_out)
        fclose(fp_out);

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