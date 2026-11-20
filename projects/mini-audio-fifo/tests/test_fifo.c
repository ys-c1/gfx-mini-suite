#include "../src/fifo.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    AudioFifo f;
    CHECK(fifo_init(&f, 8, 2) == 0);
    CHECK(fifo_space(&f) == 8);

    float in[32], out[32];
    for (int i = 0; i < 16; ++i) { in[i * 2] = (float)i; in[i * 2 + 1] = -(float)i; }

    CHECK(fifo_write(&f, in, 5) == 5);
    CHECK(fifo_available(&f) == 5);
    CHECK(fifo_space(&f) == 3);

    CHECK(fifo_read(&f, out, 3) == 3);
    CHECK(fabsf(out[0] - 0.0f) < 1e-6f);
    CHECK(fabsf(out[4] - 2.0f) < 1e-6f);

    CHECK(fifo_write(&f, in, 10) == 6);
    CHECK(fifo_available(&f) == 8);
    CHECK(fifo_write(&f, in, 1) == 0);

    CHECK(fifo_read(&f, out, 100) == 8);
    CHECK(fifo_available(&f) == 0);

    for (int i = 0; i < 20; ++i) {
        CHECK(fifo_write(&f, in, 3) >= 0);
        CHECK(fifo_read(&f, out, 2) >= 0);
    }

    fifo_clear(&f);

    /* --- zero-copy view --- */
    AudioFifoView v;
    CHECK(fifo_write(&f, in, 6) == 6);

    CHECK(fifo_read_view(&f, &v) == 0);
    CHECK(v.data == f.buf);
    CHECK(v.frames == 6);
    CHECK(v.channels == 2);
    CHECK(v.data[0] == 0.0f && v.data[10] == 5.0f);

    /* Copying read is rejected while a view is outstanding. */
    CHECK(fifo_read(&f, out, 1) == -1);

    /* Writes during a view append behind it and leave it intact. */
    CHECK(fifo_write(&f, in, 5) == 2);
    CHECK(fifo_available(&f) == 8);
    CHECK(v.data == f.buf && v.frames == 6);
    CHECK(v.data[0] == 0.0f && v.data[10] == 5.0f);

    /* A second view is refused while one is outstanding. */
    AudioFifoView v2;
    CHECK(fifo_read_view(&f, &v2) == -1);

    /* Overrun and negative commits change nothing. */
    CHECK(fifo_view_commit(&f, 7) == -1);
    CHECK(fifo_view_commit(&f, -1) == -1);
    CHECK(fifo_available(&f) == 8);

    CHECK(fifo_view_commit(&f, 4) == 0);
    CHECK(fifo_available(&f) == 4);

    /* Repeated commit for the same view is rejected. */
    CHECK(fifo_view_commit(&f, 1) == -1);
    CHECK(fifo_available(&f) == 4);

    /* Partial commit leaves the unconsumed frames for the next view. */
    CHECK(fifo_read_view(&f, &v) == 0);
    CHECK(v.data == f.buf + 8);
    CHECK(v.frames == 4);
    CHECK(v.data[0] == 4.0f && v.data[6] == 1.0f);
    CHECK(fifo_view_commit(&f, 4) == 0);
    CHECK(fifo_available(&f) == 0);

    /* Empty FIFO yields an empty view and nothing to commit. */
    CHECK(fifo_read_view(&f, &v) == 0);
    CHECK(v.data == NULL && v.frames == 0);
    CHECK(fifo_view_commit(&f, 0) == -1);
    CHECK(fifo_read(&f, out, 4) == 0);

    /* clear cancels an outstanding view. */
    CHECK(fifo_write(&f, in, 3) == 3);
    CHECK(fifo_read_view(&f, &v) == 0);
    fifo_clear(&f);
    CHECK(fifo_view_commit(&f, 0) == -1);
    CHECK(fifo_available(&f) == 0);

    /* Legacy path still behaves as before. */
    CHECK(fifo_write(&f, in, 3) == 3);
    CHECK(fifo_read(&f, out, 3) == 3);
    CHECK(out[0] == 0.0f && out[4] == 2.0f);
    CHECK(fifo_available(&f) == 0);

    fifo_free(&f);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
