#ifndef AUDIO_H
#define AUDIO_H

#include <alsa/asoundlib.h>
#include <stdint.h>

typedef struct {
    snd_pcm_t *pcm_handle;
    int is_recording;
    int sample_rate;
    int channels;
    int capture_audio; // New flag for dynamic audio recording toggle (1 = enabled, 0 = disabled)
} AudioContext;

/* Initialize and configure ALSA capture */
AudioContext* audio_init();

/* Start audio capture */
int audio_start(AudioContext* ctx);

/* Stop audio capture */
int audio_stop(AudioContext* ctx);

/* Cleanup audio capture resources */
void audio_cleanup(AudioContext* ctx);

/* Capture audio into the provided buffer.
   Returns the number of frames captured. */
int audio_capture(AudioContext* ctx, uint8_t *buffer, int buffer_size);

/* Set dynamic audio capture toggle.
   If enabled is 1, audio will be captured; if 0, audio capture is skipped.
*/
void audio_set_capture(AudioContext* ctx, int enabled);

#endif // AUDIO_H

