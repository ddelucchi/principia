#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>

namespace principia::core {

template <typename Tag, std::unsigned_integral Representation = std::uint64_t>
class StrongId {
public:
    using representation_type = Representation;

    constexpr StrongId() = default;
    explicit constexpr StrongId(Representation value) noexcept : value_(value) {}

    [[nodiscard]] constexpr Representation value() const noexcept { return value_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != Representation{}; }

    friend constexpr auto operator<=>(StrongId, StrongId) noexcept = default;

private:
    Representation value_{};
};

struct StrongIdHash {
    template <typename Tag, std::unsigned_integral Representation>
    [[nodiscard]] constexpr std::size_t operator()(StrongId<Tag, Representation> id) const noexcept
    {
        return std::hash<Representation>{}(id.value());
    }
};

}  // namespace principia::core

