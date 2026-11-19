// Matches each sample image to the target distribution and reports, per image:
// pixel count, mean before/after, cumulative (KS) deviation to the target
// before/after, and average build+apply time over REPS repetitions.
#include "../src/tone.h"
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

#define REPS 1000

static int read_pgm(const char* path, std::vector<std::uint8_t>& px) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    int w, h;
    if (std::fscanf(f, "P5 %d %d 255%*c", &w, &h) != 2) { std::fclose(f); return 0; }
    px.resize((std::size_t)w * h);
    std::size_t got = std::fread(px.data(), 1, px.size(), f);
    std::fclose(f);
    return got == px.size();
}

static int read_target(const char* path, std::uint64_t hist[256]) {
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    for (int i = 0; i < 256; ++i)
        if (std::fscanf(f, "%llu", (unsigned long long*)&hist[i]) != 1) { std::fclose(f); return 0; }
    std::fclose(f);
    return 1;
}

int main() {
    const char* names[] = {"dark_gradient", "dark_noise", "dark_bimodal"};
    std::uint64_t target[256];
    if (!read_target("data/target_hist.txt", target)) { std::puts("missing data/target_hist.txt"); return 1; }

    std::printf("%-14s %7s | %8s %8s | %9s %9s | %10s\n",
                "image", "pixels", "mean_pre", "mean_post", "dev_pre", "dev_post", "us/build+apply");
    for (const char* name : names) {
        char path[128];
        std::snprintf(path, sizeof path, "data/%s.pgm", name);
        std::vector<std::uint8_t> src;
        if (!read_pgm(path, src)) { std::printf("%s: unreadable\n", path); return 1; }
        std::size_t n = src.size();

        std::uint64_t hpre[256], hpost[256];
        histogram_of(src.data(), n, hpre);
        double mean_pre = hist_mean(hpre, n);
        std::uint64_t nt = 0; for (int i = 0; i < 256; ++i) nt += target[i];
        double dev_pre = cdf_max_deviation(hpre, n, target, nt);

        std::uint8_t lut[256];
        std::vector<std::uint8_t> dst(n);
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < REPS; ++r) {
            if (!build_match_lut(hpre, target, lut)) { std::puts("match failed"); return 1; }
            apply_lut(src.data(), n, lut, dst.data());
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / REPS;

        histogram_of(dst.data(), n, hpost);
        std::printf("%-14s %7zu | %8.2f %8.2f | %9.4f %9.4f | %10.3f\n",
                    name, n, mean_pre, hist_mean(hpost, n),
                    dev_pre, cdf_max_deviation(hpost, n, target, nt), us);
    }
    return 0;
}
