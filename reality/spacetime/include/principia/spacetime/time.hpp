#pragma once

#include <principia/units/quantity.hpp>

#include <compare>

namespace principia::spacetime {

template <typename ClockTag>
class TimePoint {
public:
    constexpr TimePoint() : elapsed_(units::seconds(0.0)) {}
    explicit constexpr TimePoint(units::Duration elapsed) : elapsed_(elapsed) {}

    [[nodiscard]] constexpr units::Duration elapsed() const noexcept { return elapsed_; }

    constexpr TimePoint& operator+=(units::Duration step)
    {
        elapsed_ += step;
        return *this;
    }

    friend constexpr TimePoint operator+(TimePoint point, units::Duration step)
    {
        point += step;
        return point;
    }

    friend constexpr units::Duration operator-(TimePoint left, TimePoint right)
    {
        return left.elapsed_ - right.elapsed_;
    }

    friend constexpr auto operator<=>(const TimePoint&, const TimePoint&) = default;

private:
    units::Duration elapsed_;
};

struct RealClockTag;
struct RenderClockTag;
struct SimulationClockTag;
struct TheorySubstepClockTag;

using RealTime = TimePoint<RealClockTag>;
using RenderTime = TimePoint<RenderClockTag>;
using SimulationTime = TimePoint<SimulationClockTag>;
using TheorySubstepTime = TimePoint<TheorySubstepClockTag>;

}  // namespace principia::spacetime

