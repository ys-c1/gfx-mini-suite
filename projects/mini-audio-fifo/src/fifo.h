#ifndef FIFO_H
#define FIFO_H

typedef struct {
    float* buf;
    int cap;      /* frames */
    int head;     /* read position */
    int count;    /* frames stored */
    int channels;
    int view_active; /* nonzero while a zero-copy view is outstanding */
    int view_frames; /* frames in the outstanding view, starting at head */
} AudioFifo;

/*
 * A contiguous window into the FIFO's internal buffer. The window never wraps:
 * it runs from the current read position to the end of the ring, so it may be
 * shorter than fifo_available().
 */
typedef struct {
    const float* data; /* interleaved samples; NULL when frames == 0 */
    int frames;        /* readable frames starting at data */
    int channels;
} AudioFifoView;

int fifo_init(AudioFifo* f, int cap_frames, int channels);
void fifo_free(AudioFifo* f);
int fifo_write(AudioFifo* f, const float* interleaved, int frames);

/*
 * Copying read. Unchanged, except that it is rejected while a zero-copy view
 * is outstanding (it would invalidate the view's pointer).
 */
int fifo_read(AudioFifo* f, float* interleaved, int frames);

/*
 * Zero-copy read.
 *
 * On success returns 0 and fills *view. If the FIFO is empty, view->data is
 * NULL, view->frames is 0, and no view is registered. If a view is already
 * outstanding, returns -1 and opens nothing (one view at a time).
 *
 * While a view is outstanding:
 *   - fifo_write() is allowed and appends to free space only. It never touches
 *     the viewed frames, which stay counted as occupied until committed, so
 *     the pointer and frame count remain valid.
 *   - fifo_read() returns -1.
 *   - fifo_clear() and fifo_free() cancel the view.
 *
 * A view's pointer stays valid until fifo_view_commit(), fifo_clear(), or
 * fifo_free() is called on that FIFO. Reallocations never happen.
 */
int fifo_read_view(AudioFifo* f, AudioFifoView* view);

/*
 * Consume `frames` from the outstanding view and close it. `frames` may be
 * less than view->frames; unconsumed frames stay in the FIFO for the next
 * view. frames == 0 closes the view without consuming anything.
 *
 * Returns 0 on success. Returns -1 and changes nothing if:
 *   - no view is outstanding (including a second commit for the same view),
 *   - frames < 0 or frames > view->frames (commit overrun).
 * After a rejected commit the view is still outstanding, so the caller can
 * retry with a valid count.
 */
int fifo_view_commit(AudioFifo* f, int frames);

int fifo_available(const AudioFifo* f);
int fifo_space(const AudioFifo* f);
void fifo_clear(AudioFifo* f);
#endif
