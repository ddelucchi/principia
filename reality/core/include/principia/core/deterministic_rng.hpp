#pragma once

#include <cstdint>
#include <string_view>

namespace principia::core {

inline constexpr std::uint32_t semantic_seed_mixer_version = 1;

[[nodiscard]] constexpr std::uint64_t stable_hash(std::string_view text) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] constexpr std::uint64_t mix_semantic_seed(
    std::uint64_t world_seed,
    std::uint64_t chunk_key,
    std::string_view system,
    std::uint64_t event_key) noexcept
{
    auto mix = [](std::uint64_t value) constexpr {
        value += 0x9E3779B97F4A7C15ULL;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31U);
    };

    // Combine in a fixed, domain-separated sequence. A commutative XOR of
    // independently mixed values makes (world, chunk) permutations collide
    // and equal components cancel, which defeats semantic stream isolation.
    auto state = mix(world_seed ^ 0xA0761D6478BD642FULL);
    state = mix(state ^ mix(chunk_key ^ 0xE7037ED1A0B428DBULL));
    state = mix(state ^ mix(stable_hash(system) ^ 0x8EBC6AF09C88C6E3ULL));
    state = mix(state ^ mix(event_key ^ 0x589965CC75374CC3ULL));
    return state;
}

class DeterministicStream {
public:
    explicit constexpr DeterministicStream(std::uint64_t semantic_seed) noexcept : state_(semantic_seed) {}

    [[nodiscard]] constexpr std::uint64_t next_u64() noexcept
    {
        state_ += 0x9E3779B97F4A7C15ULL;
        auto value = state_;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] constexpr double uniform_zero_to_one() noexcept
    {
        constexpr double inverse_53_bits = 1.0 / 9007199254740992.0;
        return static_cast<double>(next_u64() >> 11U) * inverse_53_bits;
    }

private:
    std::uint64_t state_{};
};

}  // namespace principia::core
