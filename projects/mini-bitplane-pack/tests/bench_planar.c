#include "../src/planar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(void) {
    struct { int w, h, bpp, iters; } groups[] = {
        {8, 8, 1, 100000},
        {17, 9, 2, 50000},
        {320, 200, 4, 100},
        {641, 480, 8, 10},
    };
    printf("%-12s %-4s %10s %10s %10s %10s %-6s %12s %12s\n",
           "size", "bpp", "chunky_B", "planar_B", "plane_B", "buf_B", "rt", "c2p_us", "p2c_us");
    for (unsigned g = 0; g < sizeof(groups) / sizeof(groups[0]); ++g) {
        int w = groups[g].w, h = groups[g].h, bpp = groups[g].bpp, iters = groups[g].iters;
        size_t n = chunky_bytes(w, h), ps = planar_plane_bytes(w, h);
        size_t P = planar_bytes(w, h, bpp), need = planar_buf_bytes(w, h, bpp);
        uint8_t* buf = malloc(need);
        uint8_t* orig = malloc(n);
        uint8_t* sc = malloc(ps);
        unsigned vmax = (1u << bpp) - 1u;
        for (size_t i = 0; i < n; ++i) orig[i] = (uint8_t)((i * 7u + 3u) % (vmax + 1u));

        /* 往返一致性 */
        memcpy(buf, orig, n);
        int ok = chunky_to_planar(buf, need, w, h, bpp, sc) == BP_OK
              && planar_to_chunky(buf, need, w, h, bpp, sc) == BP_OK
              && memcmp(buf, orig, n) == 0;

        /* 计时：c2p 与 p2c 交替进行，缓冲区内容始终合法 */
        double t_c2p = 0.0, t_p2c = 0.0, t0;
        memcpy(buf, orig, n);
        for (int it = 0; it < iters; ++it) {
            t0 = now_s();
            chunky_to_planar(buf, need, w, h, bpp, sc);
            t_c2p += now_s() - t0;
            t0 = now_s();
            planar_to_chunky(buf, need, w, h, bpp, sc);
            t_p2c += now_s() - t0;
        }
        printf("%4dx%-7d %-4d %10zu %10zu %10zu %10zu %-6s %12.2f %12.2f\n",
               w, h, bpp, n, P, ps, need, ok ? "OK" : "FAIL",
               t_c2p * 1e6 / iters, t_p2c * 1e6 / iters);
        free(buf); free(orig); free(sc);
    }
    return 0;
}
