#define _POSIX_C_SOURCE 200809L
#include "fifo.h"
#include <stdio.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#define CAP (1 << 20)
#define CH 2
#define PASSES 512
#define BLOCK (1 << 14)

int main(void) {
    static float in[BLOCK * CH], out[BLOCK * CH];
    for (int i = 0; i < BLOCK * CH; ++i) in[i] = (float)(i % 1000);

    AudioFifo f;
    if (fifo_init(&f, CAP, CH) != 0) return 1;

    /* transport only: produce into the FIFO, consume out of it */
    double t0 = now();
    for (int p = 0; p < PASSES; ++p) {
        fifo_write(&f, in, BLOCK);
        fifo_read(&f, out, BLOCK);
    }
    double t_copy = now() - t0;

    t0 = now();
    for (int p = 0; p < PASSES; ++p) {
        fifo_write(&f, in, BLOCK);
        AudioFifoView v;
        fifo_read_view(&f, &v);
        fifo_view_commit(&f, v.frames);
    }
    double t_view = now() - t0;

    printf("frames/iter=%d iters=%d  copy: %.2f ms  view: %.2f ms  (%.1fx)\n",
           BLOCK, PASSES, t_copy * 1e3, t_view * 1e3, t_copy / t_view);
    printf("sink %f\n", out[17]); /* keep the copy path's stores alive */
    fifo_free(&f);
    return 0;
}
