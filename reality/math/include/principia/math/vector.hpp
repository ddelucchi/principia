#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace principia::math {

template <std::size_t Dimension, typename Value>
class Vector {
public:
    static_assert(Dimension > 0, "a physical vector must have at least one component");

    using value_type = Value;
    static constexpr std::size_t dimension = Dimension;

    constexpr Vector() = default;
    explicit constexpr Vector(std::array<Value, Dimension> components) : components_(std::move(components)) {}

    template <typename... Components>
        requires(sizeof...(Components) == Dimension && (std::convertible_to<Components, Value> && ...))
    explicit constexpr Vector(Components&&... components)
        : components_{static_cast<Value>(std::forward<Components>(components))...}
    {
    }

    template <typename Other>
        requires std::convertible_to<Other, Value>
    constexpr Vector(const Vector<Dimension, Other>& other)
    {
        for (std::size_t index = 0; index < Dimension; ++index) {
            components_[index] = static_cast<Value>(other[index]);
        }
    }

    [[nodiscard]] constexpr Value& operator[](std::size_t index) noexcept { return components_[index]; }
    [[nodiscard]] constexpr const Value& operator[](std::size_t index) const noexcept { return components_[index]; }

    [[nodiscard]] constexpr auto begin() noexcept { return components_.begin(); }
    [[nodiscard]] constexpr auto begin() const noexcept { return components_.begin(); }
    [[nodiscard]] constexpr auto end() noexcept { return components_.end(); }
    [[nodiscard]] constexpr auto end() const noexcept { return components_.end(); }

    friend constexpr bool operator==(const Vector&, const Vector&) = default;

private:
    std::array<Value, Dimension> components_{};
};

template <std::size_t Dimension, typename Left, typename Right>
[[nodiscard]] constexpr auto operator+(const Vector<Dimension, Left>& left, const Vector<Dimension, Right>& right)
{
    using Result = std::remove_cvref_t<decltype(left[0] + right[0])>;
    std::array<Result, Dimension> result{};
    for (std::size_t index = 0; index < Dimension; ++index) {
        result[index] = left[index] + right[index];
    }
    return Vector<Dimension, Result>{result};
}

template <std::size_t Dimension, typename Left, typename Right>
[[nodiscard]] constexpr auto operator-(const Vector<Dimension, Left>& left, const Vector<Dimension, Right>& right)
{
    using Result = std::remove_cvref_t<decltype(left[0] - right[0])>;
    std::array<Result, Dimension> result{};
    for (std::size_t index = 0; index < Dimension; ++index) {
        result[index] = left[index] - right[index];
    }
    return Vector<Dimension, Result>{result};
}

template <std::size_t Dimension, typename Value, typename Scalar>
[[nodiscard]] constexpr auto operator*(const Vector<Dimension, Value>& vector, const Scalar& scalar)
{
    using Result = std::remove_cvref_t<decltype(vector[0] * scalar)>;
    std::array<Result, Dimension> result{};
    for (std::size_t index = 0; index < Dimension; ++index) {
        result[index] = vector[index] * scalar;
    }
    return Vector<Dimension, Result>{result};
}

template <std::size_t Dimension, typename Scalar, typename Value>
[[nodiscard]] constexpr auto operator*(const Scalar& scalar, const Vector<Dimension, Value>& vector)
{
    return vector * scalar;
}

template <std::size_t Dimension, typename Value, typename Scalar>
[[nodiscard]] constexpr auto operator/(const Vector<Dimension, Value>& vector, const Scalar& scalar)
{
    using Result = std::remove_cvref_t<decltype(vector[0] / scalar)>;
    std::array<Result, Dimension> result{};
    for (std::size_t index = 0; index < Dimension; ++index) {
        result[index] = vector[index] / scalar;
    }
    return Vector<Dimension, Result>{result};
}

template <std::size_t Dimension, typename Left, typename Right>
[[nodiscard]] constexpr auto dot(const Vector<Dimension, Left>& left, const Vector<Dimension, Right>& right)
{
    auto result = left[0] * right[0];
    for (std::size_t index = 1; index < Dimension; ++index) {
        result += left[index] * right[index];
    }
    return result;
}

template <typename Value>
[[nodiscard]] constexpr Vector<2, Value> rotate_quarter_turn_counterclockwise(const Vector<2, Value>& vector)
{
    return Vector<2, Value>{-vector[1], vector[0]};
}

}  // namespace principia::math
