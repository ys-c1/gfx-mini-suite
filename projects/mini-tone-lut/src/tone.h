#ifndef TONE_H
#define TONE_H
#include <cstddef>
#include <cstdint>
void build_gamma_lut(double gamma, std::uint8_t lut[256]);
void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst);
int lut_is_monotonic(const std::uint8_t lut[256]);
double lut_mean(const std::uint8_t lut[256]);
#endif
