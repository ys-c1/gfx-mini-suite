/* Minimal WAV reader/writer for the downmix harness.
 *
 * Supports PCM16 and IEEE-float32, which is all the harness needs; no
 * external dependency. Every buffer is allocated by the caller and sized
 * by the header, and all chunk walking is bounds checked. */
#ifndef WAV_H
#define WAV_H

#include <stddef.h>

typedef struct {
    int      channels;
    int      sample_rate;
    int      bits;          /* 16 or 32 */
    int      is_float;      /* 1 for IEEE float, 0 for PCM */
    size_t   frames;
    float*   samples;       /* interleaved, normalised to [-1, 1] */
} WavData;

/* Returns 0 on success; on failure fills err (if non-NULL) with a static
 * reason and leaves w zeroed. */
int  wav_read(const char* path, WavData* w, const char** err);
void wav_free(WavData* w);

/* Writes w as a 32-bit IEEE-float WAV. */
int  wav_write_f32(const char* path, const WavData* w, const char** err);

#endif
