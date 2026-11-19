#ifndef FIFO_H
#define FIFO_H
typedef struct {
    float* buf;
    int cap;      /* frames */
    int head;     /* read position */
    int count;    /* frames stored */
    int channels;
} AudioFifo;
int fifo_init(AudioFifo* f, int cap_frames, int channels);
void fifo_free(AudioFifo* f);
int fifo_write(AudioFifo* f, const float* interleaved, int frames);
int fifo_read(AudioFifo* f, float* interleaved, int frames);
int fifo_available(const AudioFifo* f);
int fifo_space(const AudioFifo* f);
void fifo_clear(AudioFifo* f);
#endif
