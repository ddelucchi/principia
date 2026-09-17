#include "world_view.hpp"

#include <principia/fields/field.hpp>
#include <principia/fields/gravity.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] constexpr std::uint32_t rgba(principia::presentation::Color color) noexcept
{
    return static_cast<std::uint32_t>(color.red) |
           (static_cast<std::uint32_t>(color.green) << 8U) |
           (static_cast<std::uint32_t>(color.blue) << 16U) |
           (static_cast<std::uint32_t>(color.alpha) << 24U);
}

[[nodiscard]] principia::fields::ConstantField<principia::fields::GravityVector<2>, 2>
constant_gravity(double x, double y)
{
    return principia::fields::ConstantField<principia::fields::GravityVector<2>, 2>{
        principia::fields::effective_gravity_metadata<2>(),
        principia::fields::GravityVector<2>{
            principia::units::metres_per_second_squared(x),
            principia::units::metres_per_second_squared(y),
        },
    };
}

struct FieldPixelCounts {
    std::ranges::range_difference_t<std::span<const std::uint32_t>> vectors{};
    std::ranges::range_difference_t<std::span<const std::uint32_t>> arrowheads{};
};

[[nodiscard]] FieldPixelCounts draw_field(double x, double y)
{
    using namespace principia;
    presentation::CpuCanvas canvas;
    const auto gravity = constant_gravity(x, y);
    const diagnostics::WorldInspectionFrame2 inspection{};
    const std::map<world::ParticleId, game::EntityRole> roles;
    const operators::GravityOperatorNode unused_operator{};
    presentation::draw_scene(
        canvas,
        inspection,
        gravity,
        roles,
        unused_operator,
        false,
        true,
        false);
    constexpr auto vector_color = rgba(presentation::Color{78, 224, 238, 255});
    constexpr auto arrowhead_color = rgba(presentation::Color{184, 251, 255, 255});
    return FieldPixelCounts{
        std::ranges::count(canvas.pixels(), vector_color),
        std::ranges::count(canvas.pixels(), arrowhead_color),
    };
}

}  // namespace

int main()
{
    try {
        const auto not_a_number = draw_field(std::numeric_limits<double>::quiet_NaN(), 0.0);
        require(not_a_number.arrowheads == 0, "NaN field samples must fail closed");

        const auto infinity = draw_field(std::numeric_limits<double>::infinity(), 0.0);
        require(infinity.arrowheads == 0, "infinite field samples must fail closed");

        const auto overflowing_magnitude = draw_field(
            std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
        require(
            overflowing_magnitude.arrowheads == 0,
            "a non-representable field magnitude must fail closed");

        const auto enormous_finite = draw_field(std::numeric_limits<double>::max(), 0.0);
        require(enormous_finite.arrowheads > 0, "a normalizable finite field must remain visible");
        require(
            enormous_finite.vectors < 1000,
            "normalization must precede display scaling so finite extremes draw short arrows");
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] world_view: " << error.what() << '\n';
        return 1;
    }
    std::cout << "[PASS] world_view\n";
    return 0;
}
