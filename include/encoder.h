#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

// Default Audio Bitrate for AAC/Opus (lossy codecs)
#define DEFAULT_AUDIO_BIT_RATE 64000

/* Video quality enumeration */
typedef enum {
    QUALITY_LOW,
    QUALITY_MEDIUM,
    QUALITY_HIGH
} Quality;

/* Audio codec enumeration */
typedef enum {
    AUDIO_CODEC_AAC,
    AUDIO_CODEC_PCM,
    AUDIO_CODEC_OPUS
} AudioCodec;

typedef struct {
    AVFormatContext *fmt_ctx;
    AVCodecContext *video_enc_ctx;
    AVStream *video_stream;
    AVCodecContext *audio_enc_ctx;
    AVStream *audio_stream;
    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;    // audio resampling context
    int frame_index;
    int64_t audio_pts;           // running PTS (in samples) for audio
    Quality quality;
    char filename[512]; // The output filename
} EncoderContext;

/*
 * Initializes the encoder.
 * 'width' and 'height' are the recording dimensions (which may differ from native screen size).
 * 'fps' is the capture framerate, and 'sample_rate' and 'channels' are audio parameters.
 * 'audio_codec' selects the audio codec: AAC (lossy), PCM (lossless), or Opus (modern lossy).
 * 'audio_bitrate' specifies the desired audio bitrate (e.g., DEFAULT_AUDIO_BIT_RATE for AAC/Opus).
 * The output file is initially created in ~/Videos/Screenrecords/ with a generated name.
 */
EncoderContext* encoder_init(Quality quality, int width, int height, int fps, int sample_rate, int channels, AudioCodec audio_codec, int audio_bitrate);

/* Encode one video frame (input data in RGB24 format) */
int encoder_encode_video_frame(EncoderContext* ctx, uint8_t* data);

/* Encode one audio frame with PCM data.
   The input data is expected to be S16 interleaved.
   Internally, the data is converted to the encoder’s sample format.
*/
int encoder_encode_audio_frame(EncoderContext* ctx, uint8_t* data, int size);

/* Finalize the output file */
int encoder_finalize(EncoderContext* ctx);

/* Cleanup the encoder resources */
void encoder_cleanup(EncoderContext* ctx);

#endif // ENCODER_H

