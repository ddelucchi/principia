#include "cpu_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace principia::presentation {
namespace {

[[nodiscard]] constexpr std::uint32_t rgba(Color color) noexcept
{
    return static_cast<std::uint32_t>(color.red) |
           (static_cast<std::uint32_t>(color.green) << 8U) |
           (static_cast<std::uint32_t>(color.blue) << 16U) |
           (static_cast<std::uint32_t>(color.alpha) << 24U);
}

[[nodiscard]] bool clip_test(double denominator, double numerator, double& first, double& last)
{
    if (denominator == 0.0) {
        return numerator >= 0.0;
    }
    const auto ratio = numerator / denominator;
    if (denominator < 0.0) {
        if (ratio > last) {
            return false;
        }
        first = std::max(first, ratio);
    } else {
        if (ratio < first) {
            return false;
        }
        last = std::min(last, ratio);
    }
    return true;
}

[[nodiscard]] bool clip_line_to_canvas(int& x0, int& y0, int& x1, int& y1)
{
    const auto start_x = static_cast<double>(x0);
    const auto start_y = static_cast<double>(y0);
    const auto delta_x = static_cast<double>(x1) - start_x;
    const auto delta_y = static_cast<double>(y1) - start_y;
    auto first = 0.0;
    auto last = 1.0;
    if (!clip_test(-delta_x, start_x, first, last) ||
        !clip_test(delta_x, static_cast<double>(canvas_width - 1) - start_x, first, last) ||
        !clip_test(-delta_y, start_y, first, last) ||
        !clip_test(delta_y, static_cast<double>(canvas_height - 1) - start_y, first, last)) {
        return false;
    }
    const auto clipped_coordinate = [](double value, int maximum) {
        return std::clamp(static_cast<int>(std::lround(value)), 0, maximum);
    };
    x1 = clipped_coordinate(start_x + last * delta_x, canvas_width - 1);
    y1 = clipped_coordinate(start_y + last * delta_y, canvas_height - 1);
    x0 = clipped_coordinate(start_x + first * delta_x, canvas_width - 1);
    y0 = clipped_coordinate(start_y + first * delta_y, canvas_height - 1);
    return true;
}

}  // namespace

CpuCanvas::CpuCanvas() : pixels_(static_cast<std::size_t>(canvas_width * canvas_height)) {}

void CpuCanvas::clear(Color color)
{
    std::ranges::fill(pixels_, rgba(color));
}

void CpuCanvas::pixel(int x, int y, Color color)
{
    if (x >= 0 && x < canvas_width && y >= 0 && y < canvas_height) {
        pixels_[static_cast<std::size_t>(y * canvas_width + x)] = rgba(color);
    }
}

void CpuCanvas::rectangle(int x0, int y0, int x1, int y1, Color color)
{
    const auto unclipped_left = std::min(x0, x1);
    const auto unclipped_right = std::max(x0, x1);
    const auto unclipped_top = std::min(y0, y1);
    const auto unclipped_bottom = std::max(y0, y1);
    if (unclipped_right < 0 || unclipped_left >= canvas_width || unclipped_bottom < 0 ||
        unclipped_top >= canvas_height) {
        return;
    }

    const auto left = std::max(unclipped_left, 0);
    const auto right = std::min(unclipped_right, canvas_width - 1);
    const auto top = std::max(unclipped_top, 0);
    const auto bottom = std::min(unclipped_bottom, canvas_height - 1);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            pixel(x, y, color);
        }
    }
}

void CpuCanvas::line(int x0, int y0, int x1, int y1, Color color)
{
    if (!clip_line_to_canvas(x0, y0, x1, y1)) {
        return;
    }
    const auto dx = std::abs(x1 - x0);
    const auto sx = x0 < x1 ? 1 : -1;
    const auto dy = -std::abs(y1 - y0);
    const auto sy = y0 < y1 ? 1 : -1;
    auto error = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const auto twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void CpuCanvas::disc(int center_x, int center_y, int radius, Color color)
{
    if (radius < 0) {
        return;
    }
    const auto center_x_wide = static_cast<std::int64_t>(center_x);
    const auto center_y_wide = static_cast<std::int64_t>(center_y);
    const auto radius_wide = static_cast<std::int64_t>(radius);
    const auto minimum_x = static_cast<int>(std::max<std::int64_t>(0, center_x_wide - radius_wide));
    const auto maximum_x = static_cast<int>(std::min<std::int64_t>(
        canvas_width - 1,
        center_x_wide + radius_wide));
    const auto minimum_y = static_cast<int>(std::max<std::int64_t>(0, center_y_wide - radius_wide));
    const auto maximum_y = static_cast<int>(std::min<std::int64_t>(
        canvas_height - 1,
        center_y_wide + radius_wide));
    if (minimum_x > maximum_x || minimum_y > maximum_y) {
        return;
    }
    for (int y = minimum_y; y <= maximum_y; ++y) {
        for (int x = minimum_x; x <= maximum_x; ++x) {
            const auto dx = static_cast<double>(static_cast<std::int64_t>(x) - center_x_wide);
            const auto dy = static_cast<double>(static_cast<std::int64_t>(y) - center_y_wide);
            if (std::hypot(dx, dy) <= static_cast<double>(radius_wide)) {
                pixel(x, y, color);
            }
        }
    }
}

std::span<const std::uint32_t> CpuCanvas::pixels() const noexcept
{
    return pixels_;
}

}  // namespace principia::presentation
