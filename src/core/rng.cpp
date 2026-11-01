#include "core/rng.h"

#include <cmath>

namespace wobo {

namespace {

std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

}  // namespace

std::uint64_t derive_seed(std::uint64_t seed, std::string_view stream) {
    // FNV-1a over the name, then mixed with the seed.
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : stream) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ULL;
    }
    return splitmix64(seed ^ splitmix64(h));
}

void Rng::reseed(std::uint64_t seed) {
    state_ = 0;
    inc_ = (splitmix64(seed + 1) << 1u) | 1u;
    next_u32();
    state_ += splitmix64(seed);
    next_u32();
    has_spare_ = false;
}

std::uint32_t Rng::next_u32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

float Rng::uniform() {
    // 24 random bits -> exactly representable floats in [0, 1).
    return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f);
}

std::uint32_t Rng::below(std::uint32_t n) {
    // Lemire's nearly-divisionless bounded integer, with rejection.
    std::uint64_t m = static_cast<std::uint64_t>(next_u32()) * n;
    auto low = static_cast<std::uint32_t>(m);
    if (low < n) {
        const std::uint32_t threshold = (-n) % n;
        while (low < threshold) {
            m = static_cast<std::uint64_t>(next_u32()) * n;
            low = static_cast<std::uint32_t>(m);
        }
    }
    return static_cast<std::uint32_t>(m >> 32);
}

float Rng::normal(float mean, float stddev) {
    if (has_spare_) {
        has_spare_ = false;
        return mean + stddev * spare_;
    }
    float u1 = uniform();
    while (u1 <= 1e-7f) u1 = uniform();
    const float u2 = uniform();
    const float r = std::sqrt(-2.0f * std::log(u1));
    const float theta = 6.28318530718f * u2;
    spare_ = r * std::sin(theta);
    has_spare_ = true;
    return mean + stddev * r * std::cos(theta);
}

}  // namespace wobo
