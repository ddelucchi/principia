#pragma once

#include <mp-units/core.h>
#include <mp-units/systems/si.h>

#include <type_traits>

namespace principia::units {

namespace symbols = mp_units::si::unit_symbols;

template <typename T>
using unqualified_t = std::remove_cvref_t<T>;

using Length = unqualified_t<decltype(1.0 * symbols::m)>;
using Duration = unqualified_t<decltype(1.0 * symbols::s)>;
using Mass = unqualified_t<decltype(1.0 * symbols::kg)>;
using Velocity = unqualified_t<decltype((1.0 * symbols::m) / (1.0 * symbols::s))>;
using Acceleration = unqualified_t<decltype((1.0 * symbols::m) / (1.0 * symbols::s) / (1.0 * symbols::s))>;
using Momentum = unqualified_t<decltype((1.0 * symbols::kg) * (1.0 * symbols::m) / (1.0 * symbols::s))>;
using Force = unqualified_t<decltype((1.0 * symbols::kg) * (1.0 * symbols::m) / (1.0 * symbols::s) / (1.0 * symbols::s))>;
using Energy = unqualified_t<decltype((1.0 * symbols::kg) * (1.0 * symbols::m) * (1.0 * symbols::m) /
                                    (1.0 * symbols::s) / (1.0 * symbols::s))>;
using AngularMomentum = unqualified_t<decltype(
    (1.0 * symbols::kg) * (1.0 * symbols::m) * (1.0 * symbols::m) / (1.0 * symbols::s))>;
using TemperatureDifference = unqualified_t<decltype(mp_units::delta<symbols::K>(1.0))>;
using TemperaturePoint = unqualified_t<decltype(mp_units::point<symbols::K>(1.0))>;

[[nodiscard]] constexpr Length metres(double value) { return value * symbols::m; }
[[nodiscard]] constexpr Duration seconds(double value) { return value * symbols::s; }
[[nodiscard]] constexpr Mass kilograms(double value) { return value * symbols::kg; }
[[nodiscard]] constexpr Velocity metres_per_second(double value)
{
    return value * symbols::m / (1.0 * symbols::s);
}
[[nodiscard]] constexpr Acceleration metres_per_second_squared(double value)
{
    return value * symbols::m / (1.0 * symbols::s) / (1.0 * symbols::s);
}
[[nodiscard]] constexpr Momentum kilogram_metres_per_second(double value)
{
    return value * symbols::kg * (1.0 * symbols::m) / (1.0 * symbols::s);
}
[[nodiscard]] constexpr Force newtons(double value)
{
    return value * symbols::kg * (1.0 * symbols::m) / (1.0 * symbols::s) / (1.0 * symbols::s);
}
[[nodiscard]] constexpr Energy joules(double value)
{
    return value * symbols::kg * (1.0 * symbols::m) * (1.0 * symbols::m) /
           (1.0 * symbols::s) / (1.0 * symbols::s);
}
[[nodiscard]] constexpr AngularMomentum kilogram_square_metres_per_second(double value)
{
    return value * symbols::kg * (1.0 * symbols::m) * (1.0 * symbols::m) / (1.0 * symbols::s);
}
[[nodiscard]] constexpr TemperatureDifference kelvin_difference(double value)
{
    return mp_units::delta<symbols::K>(value);
}
[[nodiscard]] constexpr TemperaturePoint kelvin_point(double value)
{
    return mp_units::point<symbols::K>(value);
}

[[nodiscard]] constexpr double in_metres(Length value) { return value.numerical_value_in(symbols::m); }
[[nodiscard]] constexpr double in_seconds(Duration value) { return value.numerical_value_in(symbols::s); }
[[nodiscard]] constexpr double in_kilograms(Mass value) { return value.numerical_value_in(symbols::kg); }
[[nodiscard]] constexpr double in_metres_per_second(Velocity value)
{
    return value.numerical_value_in(symbols::m / symbols::s);
}
[[nodiscard]] constexpr double in_metres_per_second_squared(Acceleration value)
{
    return value.numerical_value_in(symbols::m / symbols::s / symbols::s);
}
[[nodiscard]] constexpr double in_kilogram_metres_per_second(Momentum value)
{
    return value.numerical_value_in(symbols::kg * symbols::m / symbols::s);
}
[[nodiscard]] constexpr double in_newtons(Force value)
{
    return value.numerical_value_in(symbols::kg * symbols::m / symbols::s / symbols::s);
}
[[nodiscard]] constexpr double in_joules(Energy value)
{
    return value.numerical_value_in(symbols::kg * symbols::m * symbols::m / symbols::s / symbols::s);
}
[[nodiscard]] constexpr double in_kilogram_square_metres_per_second(AngularMomentum value)
{
    return value.numerical_value_in(symbols::kg * symbols::m * symbols::m / symbols::s);
}

}  // namespace principia::units
