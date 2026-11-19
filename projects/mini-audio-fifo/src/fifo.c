#include "fifo.h"
#include <stdlib.h>

int fifo_init(AudioFifo* f, int cap_frames, int channels) {
    if (!f || cap_frames <= 0 || channels <= 0) return -1;
    f->buf = (float*)calloc((size_t)cap_frames * (size_t)channels, sizeof(float));
    if (!f->buf) return -1;
    f->cap = cap_frames; f->head = 0; f->count = 0; f->channels = channels;
    return 0;
}

void fifo_free(AudioFifo* f) { if (f) { free(f->buf); f->buf = NULL; f->cap = 0; f->count = 0; } }

int fifo_write(AudioFifo* f, const float* interleaved, int frames) {
    if (!f || !interleaved || frames < 0) return -1;
    int written = 0;
    for (int i = 0; i < frames; ++i) {
        if (f->count == f->cap) break;
        int slot = (f->head + f->count) % f->cap;
        for (int c = 0; c < f->channels; ++c)
            f->buf[(size_t)slot * f->channels + c] = interleaved[(size_t)i * f->channels + c];
        ++f->count; ++written;
    }
    return written;
}

int fifo_read(AudioFifo* f, float* interleaved, int frames) {
    if (!f || !interleaved || frames < 0) return -1;
    int got = 0;
    for (int i = 0; i < frames && f->count > 0; ++i) {
        for (int c = 0; c < f->channels; ++c)
            interleaved[(size_t)i * f->channels + c] = f->buf[(size_t)f->head * f->channels + c];
        f->head = (f->head + 1) % f->cap;
        --f->count; ++got;
    }
    return got;
}

int fifo_available(const AudioFifo* f) { return f ? f->count : 0; }
int fifo_space(const AudioFifo* f) { return f ? f->cap - f->count : 0; }
void fifo_clear(AudioFifo* f) { if (f) { f->head = 0; f->count = 0; } }
