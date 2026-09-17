#include "world_view.hpp"

#include <principia/boundaries/boundary.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace principia::presentation {
namespace {

constexpr double world_left = -25.0;
constexpr double world_right = 25.0;
constexpr double world_bottom = -12.0;
constexpr double world_top = 16.0;

struct ScreenPoint {
    int x{};
    int y{};
};

[[nodiscard]] int saturating_round_to_int(double value) noexcept
{
    if (std::isnan(value)) {
        return 0;
    }
    if (value <= static_cast<double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    if (value >= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(std::lround(value));
}

[[nodiscard]] int saturating_add(int left, int right) noexcept
{
    const auto sum = static_cast<std::int64_t>(left) + static_cast<std::int64_t>(right);
    return static_cast<int>(std::clamp<std::int64_t>(
        sum,
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}

[[nodiscard]] int saturating_negate(int value) noexcept
{
    return value == std::numeric_limits<int>::min() ? std::numeric_limits<int>::max() : -value;
}

[[nodiscard]] ScreenPoint world_to_screen(double x, double y)
{
    const auto normalized_x = (x - world_left) / (world_right - world_left);
    const auto normalized_y = (world_top - y) / (world_top - world_bottom);
    return ScreenPoint{
        saturating_round_to_int(normalized_x * static_cast<double>(canvas_width - 1)),
        saturating_round_to_int(normalized_y * static_cast<double>(canvas_height - 1)),
    };
}

void draw_world_backdrop(CpuCanvas& canvas)
{
    for (int y = 0; y < canvas_height; ++y) {
        const auto blend = static_cast<double>(y) / static_cast<double>(canvas_height - 1);
        canvas.rectangle(
            0,
            y,
            canvas_width - 1,
            y,
            Color{
                static_cast<std::uint8_t>(12.0 + 18.0 * blend),
                static_cast<std::uint8_t>(22.0 + 24.0 * blend),
                static_cast<std::uint8_t>(48.0 + 38.0 * blend),
                255});
    }

    const auto ridge = world_to_screen(0.0, -5.0).y;
    canvas.rectangle(0, ridge, canvas_width - 1, canvas_height - 1, Color{30, 45, 48, 255});
    for (int x = 0; x < canvas_width; ++x) {
        const auto height = static_cast<int>(2.0 * std::sin(static_cast<double>(x) * 0.08));
        canvas.pixel(x, ridge + height, Color{68, 115, 78, 255});
        canvas.pixel(x, ridge + height + 1, Color{49, 82, 59, 255});
    }
}

void draw_operator_region(CpuCanvas& canvas, const operators::GravityOperatorNode& node)
{
    const auto center = world_to_screen(
        units::in_metres(node.support.center[0]), units::in_metres(node.support.center[1]));
    const auto radius = units::in_metres(node.support.radius) / (world_right - world_left) *
                        static_cast<double>(canvas_width - 1);
    constexpr int segments = 160;
    constexpr double tau = 2.0 * 3.141592653589793;
    for (int segment = 0; segment < segments; segment += 2) {
        const auto a0 = static_cast<double>(segment) / static_cast<double>(segments) * tau;
        const auto a1 = static_cast<double>(segment + 1) / static_cast<double>(segments) * tau;
        const auto offset_x0 = saturating_round_to_int(radius * std::cos(a0));
        const auto offset_y0 = saturating_round_to_int(radius * std::sin(a0));
        const auto offset_x1 = saturating_round_to_int(radius * std::cos(a1));
        const auto offset_y1 = saturating_round_to_int(radius * std::sin(a1));
        canvas.line(
            saturating_add(center.x, offset_x0),
            saturating_add(center.y, saturating_negate(offset_y0)),
            saturating_add(center.x, offset_x1),
            saturating_add(center.y, saturating_negate(offset_y1)),
            Color{125, 92, 220, 255});
    }
}

void draw_field_vectors(
    CpuCanvas& canvas, const fields::EffectiveGravityField<2>& gravity, spacetime::SimulationTime time)
{
    constexpr Color field_color{78, 224, 238, 255};
    for (double y = -8.0; y <= 12.0; y += 4.0) {
        for (double x = -22.0; x <= 22.0; x += 4.0) {
            const spacetime::Event<2> event{
                time,
                spacetime::WorldPosition<2>{units::metres(x), units::metres(y)},
                spacetime::FrameId{1},
            };
            const auto vector = gravity.sample(event);
            const auto gx = units::in_metres_per_second_squared(vector[0]);
            const auto gy = units::in_metres_per_second_squared(vector[1]);
            if (!std::isfinite(gx) || !std::isfinite(gy)) {
                continue;
            }
            const auto magnitude = std::hypot(gx, gy);
            if (!std::isfinite(magnitude) || magnitude <= 1.0e-12) {
                continue;
            }
            const auto origin = world_to_screen(x, y);
            const auto end_x =
                saturating_add(origin.x, saturating_round_to_int((gx / magnitude) * 6.0));
            const auto end_y = saturating_add(
                origin.y,
                saturating_negate(saturating_round_to_int((gy / magnitude) * 6.0)));
            canvas.line(origin.x, origin.y, end_x, end_y, field_color);
            canvas.disc(end_x, end_y, 1, Color{184, 251, 255, 255});
        }
    }
}

void draw_boundary(CpuCanvas& canvas, const boundaries::BoundaryDefinition& boundary)
{
    const auto minimum = world_to_screen(
        units::in_metres(boundary.extent.minimum[0]), units::in_metres(boundary.extent.minimum[1]));
    const auto maximum = world_to_screen(
        units::in_metres(boundary.extent.maximum[0]), units::in_metres(boundary.extent.maximum[1]));
    canvas.rectangle(minimum.x, maximum.y, maximum.x, minimum.y, Color{88, 98, 111, 255});
    canvas.line(minimum.x, maximum.y, minimum.x, minimum.y, Color{181, 190, 202, 255});
}

void draw_entities(
    CpuCanvas& canvas,
    const diagnostics::WorldInspectionFrame2& inspection,
    const std::map<world::ParticleId, game::EntityRole>& roles)
{
    using game::EntityRole;
    for (const auto& inspected : inspection.particles) {
        const auto& particle = inspected.state;
        const auto point = world_to_screen(
            units::in_metres(particle.position[0]), units::in_metres(particle.position[1]));
        const auto contact_radius = inspected.mechanical_contact
                                        ? std::max(
                                              1,
                                              saturating_round_to_int(
                                                  units::in_metres(inspected.mechanical_contact->radius) /
                                                  (world_right - world_left) *
                                                  static_cast<double>(canvas_width - 1)))
                                        : 1;
        switch (roles.at(particle.id)) {
        case EntityRole::Player:
            canvas.rectangle(
                saturating_add(point.x, -2),
                saturating_add(point.y, -5),
                saturating_add(point.x, 2),
                saturating_add(point.y, 3),
                Color{236, 201, 126, 255});
            canvas.disc(point.x, saturating_add(point.y, -6), 2, Color{246, 220, 163, 255});
            canvas.pixel(
                saturating_add(point.x, -2),
                saturating_add(point.y, -6),
                Color{74, 47, 61, 255});
            canvas.pixel(
                saturating_add(point.x, 2),
                saturating_add(point.y, -6),
                Color{74, 47, 61, 255});
            break;
        case EntityRole::Rock:
            canvas.disc(point.x, point.y, contact_radius, Color{159, 166, 178, 255});
            canvas.pixel(
                saturating_add(point.x, -1),
                saturating_add(point.y, -2),
                Color{206, 211, 218, 255});
            break;
        case EntityRole::Sand:
            canvas.disc(point.x, point.y, contact_radius, Color{225, 178, 79, 255});
            canvas.pixel(
                saturating_add(point.x, 1),
                saturating_add(point.y, -1),
                Color{255, 224, 137, 255});
            break;
        }
    }
}

}  // namespace

void draw_scene(
    CpuCanvas& canvas,
    const diagnostics::WorldInspectionFrame2& inspection,
    const fields::EffectiveGravityField<2>& gravity,
    const std::map<world::ParticleId, game::EntityRole>& roles,
    const operators::GravityOperatorNode& operator_node,
    bool operator_installed,
    bool field_visible,
    bool paused)
{
    draw_world_backdrop(canvas);
    if (operator_installed) {
        draw_operator_region(canvas, operator_node);
    }
    if (field_visible) {
        draw_field_vectors(canvas, gravity, inspection.simulation_time);
    }
    for (const auto& boundary : inspection.boundaries) {
        draw_boundary(canvas, boundary);
    }
    draw_entities(canvas, inspection, roles);

    canvas.rectangle(5, 5, 15, 9, Color{68, 115, 78, 255});
    canvas.rectangle(
        18, 5, 28, 9, field_visible ? Color{78, 224, 238, 255} : Color{47, 57, 70, 255});
    canvas.rectangle(
        31, 5, 41, 9, operator_installed ? Color{125, 92, 220, 255} : Color{47, 57, 70, 255});
    if (paused) {
        canvas.rectangle(46, 5, 48, 12, Color{244, 186, 72, 255});
        canvas.rectangle(51, 5, 53, 12, Color{244, 186, 72, 255});
    }
}

}  // namespace principia::presentation
