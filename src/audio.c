/* src/audio.c */
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <string.h>

AudioContext* audio_init() {
    AudioContext* ctx = malloc(sizeof(AudioContext));
    if(!ctx) return NULL;
    int err = snd_pcm_open(&ctx->pcm_handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if(err < 0) {
        fprintf(stderr, "Unable to open PCM device: %s\n", snd_strerror(err));
        free(ctx);
        return NULL;
    }
    ctx->sample_rate = 44100;
    ctx->channels = 2;
    err = snd_pcm_set_params(ctx->pcm_handle, SND_PCM_FORMAT_S16_LE,
                              SND_PCM_ACCESS_RW_INTERLEAVED, ctx->channels,
                              ctx->sample_rate, 1, 500000);
    if(err < 0) {
        fprintf(stderr, "Unable to set PCM parameters: %s\n", snd_strerror(err));
        snd_pcm_close(ctx->pcm_handle);
        free(ctx);
        return NULL;
    }
    ctx->is_recording = 0;
    ctx->capture_audio = 1;  // Audio capturing enabled by default
    return ctx;
}

int audio_start(AudioContext* ctx) {
    if(!ctx) return -1;
    ctx->is_recording = 1;
    return 0;
}

int audio_stop(AudioContext* ctx) {
    if(!ctx) return -1;
    ctx->is_recording = 0;
    return 0;
}

void audio_cleanup(AudioContext* ctx) {
    if(ctx) {
        if(ctx->pcm_handle)
            snd_pcm_close(ctx->pcm_handle);
        free(ctx);
    }
}

int audio_capture(AudioContext* ctx, uint8_t *buffer, int buffer_size) {
    if(!ctx || !ctx->is_recording)
        return -1;
    if (!ctx->capture_audio)  // Skip audio capture if disabled via toggle
        return 0;
    int frames = snd_pcm_readi(ctx->pcm_handle, buffer, buffer_size / (ctx->channels * 2));
    if (frames < 0) {
        frames = snd_pcm_recover(ctx->pcm_handle, frames, 0);
    }
    return frames;
}

void audio_set_capture(AudioContext* ctx, int enabled) {
    if(ctx)
        ctx->capture_audio = enabled;
}

