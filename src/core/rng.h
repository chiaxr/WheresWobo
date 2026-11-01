#pragma once

// Seeded random number generation. Every consumer owns its own stream derived
// from the run seed and a name, so (for example) drawing a card never shifts
// the sensor noise sequence, and runs reproduce exactly from their seed.
//
// The generator and distributions are implemented here rather than taken from
// <random>: the standard distributions are implementation-defined, and a run
// should replay identically on desktop and in the browser.

#include <cstdint>
#include <string_view>

namespace wobo {

// Mix a run seed with a stream name into an independent stream seed.
std::uint64_t derive_seed(std::uint64_t seed, std::string_view stream);

// PCG32 (XSH-RR) with Box-Muller normals.
class Rng {
public:
    Rng() : Rng(0x853c49e6748fea9bULL) {}
    explicit Rng(std::uint64_t seed) { reseed(seed); }

    void reseed(std::uint64_t seed);

    std::uint32_t next_u32();
    // Uniform in [0, 1).
    float uniform();
    // Uniform in [lo, hi).
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // Uniform integer in [0, n). n must be > 0.
    std::uint32_t below(std::uint32_t n);
    // Standard normal, scaled.
    float normal(float mean = 0.0f, float stddev = 1.0f);
    bool chance(float p) { return uniform() < p; }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
    bool has_spare_ = false;
    float spare_ = 0.0f;
};

}  // namespace wobo
