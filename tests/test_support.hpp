#pragma once

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace principia::tests {

inline void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

inline void require_near(double actual, double expected, double tolerance, std::string_view message)
{
    if (!std::isfinite(actual) || !std::isfinite(expected) || !std::isfinite(tolerance) || tolerance < 0.0 ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string{message} + ": expected " + std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

}  // namespace principia::tests
