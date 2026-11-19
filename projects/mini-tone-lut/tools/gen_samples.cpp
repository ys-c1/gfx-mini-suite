// Generates the self-made sample set into data/:
//   dark_gradient.pgm  64x64   smooth dark gradient
//   dark_noise.pgm     48x80   dark noise (different pixel count on purpose)
//   dark_bimodal.pgm   32x32   two dark clusters
//   target_hist.txt    256 counts, bright reference distribution
// Deterministic (fixed-seed LCG), so the data is reproducible.
#include <cstdio>
#include <cstdint>
#include <cmath>

static std::uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static std::uint32_t rnd() { // splitmix-style LCG, deterministic
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (std::uint32_t)(rng_state >> 33);
}

static int write_pgm(const char* path, const std::uint8_t* px, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return 0;
    std::fprintf(f, "P5\n%d %d\n255\n", w, h);
    std::fwrite(px, 1, (std::size_t)w * h, f);
    std::fclose(f);
    return 1;
}

int main() {
    std::uint8_t img[80 * 64];

    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            img[y * 64 + x] = (std::uint8_t)((x * 2 + y) % 90); // dark gradient
    if (!write_pgm("data/dark_gradient.pgm", img, 64, 64)) return 1;

    for (int i = 0; i < 48 * 80; ++i)
        img[i] = (std::uint8_t)(10 + rnd() % 71); // dark noise in [10,80]
    if (!write_pgm("data/dark_noise.pgm", img, 80, 48)) return 1;

    for (int i = 0; i < 32 * 32; ++i)
        img[i] = (std::uint8_t)((i % 2) ? 95 + rnd() % 11 : 15 + rnd() % 11);
    if (!write_pgm("data/dark_bimodal.pgm", img, 32, 32)) return 1;

    FILE* f = std::fopen("data/target_hist.txt", "w");
    if (!f) return 1;
    for (int i = 0; i < 256; ++i) { // bright reference, roughly normal around 180
        double d = (i - 180.0) / 30.0;
        long c = std::lround(400.0 * std::exp(-0.5 * d * d)) + 1;
        std::fprintf(f, "%ld\n", c);
    }
    std::fclose(f);
    std::puts("wrote data/dark_gradient.pgm data/dark_noise.pgm data/dark_bimodal.pgm data/target_hist.txt");
    return 0;
}
