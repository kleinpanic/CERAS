/* src/encoder.c */
#include "encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libswresample/swresample.h>

#define VIDEO_BIT_RATE 400000
// DEFAULT_AUDIO_BIT_RATE is defined in encoder.h

// Helper: Generate a filename based on current time.
static void generate_filename(char* buffer, size_t size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buffer, size, "screenrecording_%Y%m%d_%H%M%S.mp4", tm_info);
}

static int setup_video_stream(EncoderContext* ctx, int width, int height, int fps) {
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "H.264 codec not found\n");
        return -1;
    }
    ctx->video_stream = avformat_new_stream(ctx->fmt_ctx, codec);
    if (!ctx->video_stream) {
        fprintf(stderr, "Could not allocate video stream\n");
        return -1;
    }
    ctx->video_enc_ctx = avcodec_alloc_context3(codec);
    if (!ctx->video_enc_ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }
    ctx->video_enc_ctx->codec_id = AV_CODEC_ID_H264;
    ctx->video_enc_ctx->bit_rate = VIDEO_BIT_RATE;
    ctx->video_enc_ctx->width = width;
    ctx->video_enc_ctx->height = height;
    ctx->video_enc_ctx->time_base = (AVRational){1, fps};
    ctx->video_enc_ctx->framerate = (AVRational){fps, 1};
    ctx->video_enc_ctx->gop_size = 12;
    ctx->video_enc_ctx->max_b_frames = 2;
    ctx->video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if (ctx->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ctx->video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int ret = avcodec_open2(ctx->video_enc_ctx, codec, NULL);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Could not open video codec: %s\n", errbuf);
        return ret;
    }
    ret = avcodec_parameters_from_context(ctx->video_stream->codecpar, ctx->video_enc_ctx);
    if (ret < 0) {
        fprintf(stderr, "Could not copy video codec parameters\n");
        return ret;
    }
    ctx->video_stream->time_base = ctx->video_enc_ctx->time_base;
    ctx->sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                                  width, height, AV_PIX_FMT_YUV420P,
                                  SWS_BICUBIC, NULL, NULL, NULL);
    if (!ctx->sws_ctx) {
        fprintf(stderr, "Could not initialize the scaling context\n");
        return -1;
    }
    return 0;
}

static int setup_audio_stream(EncoderContext* ctx, int sample_rate, int channels, AudioCodec audio_codec, int audio_bitrate) {
    const AVCodec *codec = NULL;
    switch(audio_codec) {
        case AUDIO_CODEC_AAC:
            codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            break;
        case AUDIO_CODEC_PCM:
            codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
            break;
        case AUDIO_CODEC_OPUS:
            codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
            break;
        default:
            codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            break;
    }
    if (!codec) {
        fprintf(stderr, "Audio codec not found for selected option\n");
        return -1;
    }
    ctx->audio_stream = avformat_new_stream(ctx->fmt_ctx, codec);
    if (!ctx->audio_stream) {
        fprintf(stderr, "Could not allocate audio stream\n");
        return -1;
    }
    ctx->audio_enc_ctx = avcodec_alloc_context3(codec);
    if (!ctx->audio_enc_ctx) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        return -1;
    }
    if(audio_codec == AUDIO_CODEC_PCM) {
        /* For PCM, bitrate is not used. */
        ctx->audio_enc_ctx->bit_rate = 0;
    } else {
        ctx->audio_enc_ctx->bit_rate = audio_bitrate;
    }
    if(audio_codec == AUDIO_CODEC_OPUS && sample_rate != 48000) {
        fprintf(stderr, "Opus codec requires a sample rate of 48000 Hz, forcing sample_rate to 48000\n");
        sample_rate = 48000;
    }
    ctx->audio_enc_ctx->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    ctx->audio_enc_ctx->sample_rate = sample_rate;
    av_channel_layout_default(&ctx->audio_enc_ctx->ch_layout, channels);
    ctx->audio_enc_ctx->time_base = (AVRational){1, sample_rate};
    if (ctx->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ctx->audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int ret = avcodec_open2(ctx->audio_enc_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec\n");
        return ret;
    }
    ret = avcodec_parameters_from_context(ctx->audio_stream->codecpar, ctx->audio_enc_ctx);
    if (ret < 0) {
        fprintf(stderr, "Could not copy audio codec parameters\n");
        return ret;
    }
    ctx->audio_stream->time_base = ctx->audio_enc_ctx->time_base;

    /* Initialize resampling context */
    ctx->swr_ctx = swr_alloc();
    if (!ctx->swr_ctx) {
        fprintf(stderr, "Could not allocate resampling context\n");
        return -1;
    }
    av_opt_set_int(ctx->swr_ctx, "in_channel_layout", channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(ctx->swr_ctx, "out_channel_layout", ctx->audio_enc_ctx->ch_layout.nb_channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(ctx->swr_ctx, "in_sample_rate", sample_rate, 0);
    av_opt_set_int(ctx->swr_ctx, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(ctx->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_sample_fmt(ctx->swr_ctx, "out_sample_fmt", ctx->audio_enc_ctx->sample_fmt, 0);
    ret = swr_init(ctx->swr_ctx);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        return ret;
    }
    return 0;
}

EncoderContext* encoder_init(Quality quality, int width, int height, int fps, int sample_rate, int channels, AudioCodec audio_codec, int audio_bitrate) {
    EncoderContext* ctx = malloc(sizeof(EncoderContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(EncoderContext));
    ctx->quality = quality;
    ctx->frame_index = 0;
    ctx->audio_pts = 0;  // initialize audio pts

    char filepath[1024];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(filepath, sizeof(filepath), "%s/Videos/Screenrecords/", home);
    char mkdir_cmd[1200];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", filepath);
    system(mkdir_cmd);
    generate_filename(ctx->filename, sizeof(ctx->filename));
    /* If PCM is selected, change extension from .mp4 to .mov */
    if (audio_codec == AUDIO_CODEC_PCM) {
        char *dot = strrchr(ctx->filename, '.');
        if (dot)
            strcpy(dot, ".mov");
    }
    char fullpath[2048];
    snprintf(fullpath, sizeof(fullpath), "%s%s", filepath, ctx->filename);

    /* For PCM, force MOV container; otherwise use default */
    int ret;
    if (audio_codec == AUDIO_CODEC_PCM)
        ret = avformat_alloc_output_context2(&ctx->fmt_ctx, NULL, "mov", fullpath);
    else
        ret = avformat_alloc_output_context2(&ctx->fmt_ctx, NULL, NULL, fullpath);
    if (ret < 0 || !ctx->fmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        free(ctx);
        return NULL;
    }

    ret = setup_video_stream(ctx, width, height, fps);
    if (ret < 0) {
        free(ctx);
        return NULL;
    }
    ret = setup_audio_stream(ctx, sample_rate, channels, audio_codec, audio_bitrate);
    if (ret < 0) {
        free(ctx);
        return NULL;
    }
    if (!(ctx->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ctx->fmt_ctx->pb, fullpath, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'\n", fullpath);
            free(ctx);
            return NULL;
        }
    }
    ret = avformat_write_header(ctx->fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        free(ctx);
        return NULL;
    }
    printf("Encoder initialized, output file: %s\n", fullpath);
    return ctx;
}

int encoder_encode_video_frame(EncoderContext* ctx, uint8_t* data) {
    if (!ctx || !data) return -1;
    int ret;
    AVFrame *frame = av_frame_alloc();
    if (!frame) return -1;
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = ctx->video_enc_ctx->width;
    frame->height = ctx->video_enc_ctx->height;
    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data\n");
        av_frame_free(&frame);
        return ret;
    }
    AVFrame *rgb_frame = av_frame_alloc();
    if (!rgb_frame) {
        av_frame_free(&frame);
        return -1;
    }
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width  = ctx->video_enc_ctx->width;
    rgb_frame->height = ctx->video_enc_ctx->height;
    ret = av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, data,
                               AV_PIX_FMT_RGB24, ctx->video_enc_ctx->width, ctx->video_enc_ctx->height, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not fill RGB frame\n");
        av_frame_free(&frame);
        av_frame_free(&rgb_frame);
        return ret;
    }
    sws_scale(ctx->sws_ctx, (const uint8_t* const*)rgb_frame->data, rgb_frame->linesize, 0,
              ctx->video_enc_ctx->height, frame->data, frame->linesize);
    frame->pts = ctx->frame_index++;
    ret = avcodec_send_frame(ctx->video_enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending video frame\n");
        av_frame_free(&frame);
        av_frame_free(&rgb_frame);
        return ret;
    }
    AVPacket *pkt = av_packet_alloc();
    ret = avcodec_receive_packet(ctx->video_enc_ctx, pkt);
    if (ret == 0) {
        pkt->stream_index = ctx->video_stream->index;
        pkt->pts = av_rescale_q(pkt->pts, ctx->video_enc_ctx->time_base, ctx->video_stream->time_base);
        pkt->dts = av_rescale_q(pkt->dts, ctx->video_enc_ctx->time_base, ctx->video_stream->time_base);
        pkt->duration = av_rescale_q(pkt->duration, ctx->video_enc_ctx->time_base, ctx->video_stream->time_base);
        ret = av_interleaved_write_frame(ctx->fmt_ctx, pkt);
        av_packet_free(&pkt);
    } else {
        av_packet_free(&pkt);
    }
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    return ret;
}

int encoder_encode_audio_frame(EncoderContext* ctx, uint8_t* data, int size) {
    if (!ctx || !data) return -1;
    int ret;
    AVFrame *frame = av_frame_alloc();
    if (!frame) return -1;
    
    /* Determine number of input samples based on S16 input format */
    int in_samples = size / (ctx->audio_enc_ctx->ch_layout.nb_channels * sizeof(int16_t));
    
    /* For PCM we bypass the fixed frame size and use the available samples */
    if(ctx->audio_enc_ctx->codec_id == AV_CODEC_ID_PCM_S16LE) {
        frame->nb_samples = in_samples;
    } else {
        frame->nb_samples = ctx->audio_enc_ctx->frame_size;
    }
    
    frame->format = ctx->audio_enc_ctx->sample_fmt;
    ret = av_channel_layout_copy(&frame->ch_layout, &ctx->audio_enc_ctx->ch_layout);
    if (ret < 0) {
        fprintf(stderr, "Could not copy channel layout\n");
        av_frame_free(&frame);
        return ret;
    }
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }
    
    /* Use swr_convert to convert input S16 to encoder sample format */
    int converted = swr_convert(ctx->swr_ctx, frame->data, frame->nb_samples, (const uint8_t **)&data, in_samples);
    if (converted < 0) {
        fprintf(stderr, "Error while converting audio samples\n");
        av_frame_free(&frame);
        return converted;
    }
    frame->nb_samples = converted;
    frame->pts = ctx->audio_pts;
    ctx->audio_pts += converted;
    
    ret = avcodec_send_frame(ctx->audio_enc_ctx, frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }
    AVPacket *pkt = av_packet_alloc();
    ret = avcodec_receive_packet(ctx->audio_enc_ctx, pkt);
    if (ret == 0) {
        pkt->stream_index = ctx->audio_stream->index;
        pkt->pts = av_rescale_q(pkt->pts, ctx->audio_enc_ctx->time_base, ctx->audio_stream->time_base);
        pkt->dts = av_rescale_q(pkt->dts, ctx->audio_enc_ctx->time_base, ctx->audio_stream->time_base);
        pkt->duration = av_rescale_q(pkt->duration, ctx->audio_enc_ctx->time_base, ctx->audio_stream->time_base);
        ret = av_interleaved_write_frame(ctx->fmt_ctx, pkt);
        av_packet_free(&pkt);
    } else {
        av_packet_free(&pkt);
    }
    av_frame_free(&frame);
    return ret;
}

int encoder_finalize(EncoderContext* ctx) {
    if (!ctx) return -1;
    int ret = av_write_trailer(ctx->fmt_ctx);
    if (ret < 0)
        fprintf(stderr, "Error writing trailer\n");
    return ret;
}

void encoder_cleanup(EncoderContext* ctx) {
    if (!ctx) return;
    if (ctx->swr_ctx) {
        swr_free(&ctx->swr_ctx);
    }
    if (ctx->sws_ctx) sws_freeContext(ctx->sws_ctx);
    if (ctx->video_enc_ctx) avcodec_free_context(&ctx->video_enc_ctx);
    if (ctx->audio_enc_ctx) avcodec_free_context(&ctx->audio_enc_ctx);
    if (ctx->fmt_ctx) {
        if (!(ctx->fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&ctx->fmt_ctx->pb);
        avformat_free_context(ctx->fmt_ctx);
    }
    free(ctx);
}

