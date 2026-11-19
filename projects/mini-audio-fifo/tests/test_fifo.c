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
    CHECK(fifo_available(&f) == 0);
    fifo_free(&f);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
