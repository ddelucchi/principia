#include "cpu_canvas.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
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

}  // namespace

int main()
{
    using namespace principia::presentation;
    try {
        constexpr Color clear_color{1, 2, 3, 4};
        constexpr Color draw_color{9, 8, 7, 6};
        CpuCanvas canvas;
        require(canvas.pixels().size() ==
                    static_cast<std::size_t>(canvas_width * canvas_height),
                "canvas allocation must match its public dimensions");

        canvas.clear(clear_color);
        canvas.rectangle(
            std::numeric_limits<int>::min(),
            0,
            -1,
            canvas_height - 1,
            draw_color);
        canvas.rectangle(
            canvas_width,
            0,
            std::numeric_limits<int>::max(),
            canvas_height - 1,
            draw_color);
        canvas.rectangle(
            0,
            std::numeric_limits<int>::min(),
            canvas_width - 1,
            -1,
            draw_color);
        canvas.rectangle(
            0,
            canvas_height,
            canvas_width - 1,
            std::numeric_limits<int>::max(),
            draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "wholly off-canvas rectangles must not be collapsed onto a canvas edge");

        canvas.rectangle(-2, -2, 1, 1, draw_color);
        require(
            std::ranges::count(canvas.pixels(), rgba(draw_color)) == 4,
            "a partially visible rectangle must draw only its actual intersection");

        canvas.clear(clear_color);
        canvas.rectangle(
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::min(),
            draw_color);
        require(
            std::ranges::count(canvas.pixels(), rgba(draw_color)) ==
                static_cast<std::ranges::range_difference_t<decltype(canvas.pixels())>>(
                    canvas.pixels().size()),
            "an extreme reversed rectangle spanning the canvas must remain bounded and complete");

        canvas.clear(clear_color);
        canvas.line(
            std::numeric_limits<int>::min(),
            canvas_height / 2,
            std::numeric_limits<int>::max(),
            canvas_height / 2,
            draw_color);
        require(
            std::ranges::count(canvas.pixels(), rgba(draw_color)) == canvas_width,
            "an extreme crossing line must clip to one bounded scanline");

        canvas.clear(clear_color);
        canvas.line(
            canvas_width / 2,
            std::numeric_limits<int>::min(),
            canvas_width / 2,
            std::numeric_limits<int>::max(),
            draw_color);
        require(
            std::ranges::count(canvas.pixels(), rgba(draw_color)) == canvas_height,
            "an extreme vertical line must clip to one bounded column");

        canvas.clear(clear_color);
        canvas.line(-1, 1, 1, -1, draw_color);
        require(
            std::ranges::count(canvas.pixels(), rgba(draw_color)) == 1 &&
                canvas.pixels().front() == rgba(draw_color),
            "a line tangent to the canvas must draw its single corner intersection");

        canvas.clear(clear_color);
        canvas.line(
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::min() + 1,
            std::numeric_limits<int>::min() + 1,
            draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "a wholly off-canvas extreme line must reject without iteration");

        canvas.line(
            std::numeric_limits<int>::min(),
            0,
            -1,
            canvas_height - 1,
            draw_color);
        canvas.line(
            canvas_width,
            0,
            std::numeric_limits<int>::max(),
            canvas_height - 1,
            draw_color);
        canvas.line(
            0,
            std::numeric_limits<int>::min(),
            canvas_width - 1,
            -1,
            draw_color);
        canvas.line(
            0,
            canvas_height,
            canvas_width - 1,
            std::numeric_limits<int>::max(),
            draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "lines wholly beyond each individual canvas edge must reject");

        canvas.line(-1, -1, -1, -1, draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "an off-canvas degenerate line must reject");

        canvas.line(
            canvas_width - 1,
            canvas_height - 1,
            canvas_width - 1,
            canvas_height - 1,
            draw_color);
        require(
            canvas.pixels().back() == rgba(draw_color),
            "an on-canvas degenerate line must draw exactly its endpoint");

        canvas.clear(clear_color);
        canvas.disc(0, 0, std::numeric_limits<int>::min(), draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "a negative extreme radius must fail closed");

        canvas.disc(0, 0, std::numeric_limits<int>::max(), draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) ==
                    static_cast<std::ranges::range_difference_t<decltype(canvas.pixels())>>(
                        canvas.pixels().size()),
                "a huge visible disc must clip its work to the finite canvas");

        canvas.clear(clear_color);
        canvas.disc(
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max(),
            draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "a huge disc whose clipped bounds are empty must reject without raster work");

        canvas.disc(canvas_width, canvas_height, 0, draw_color);
        require(std::ranges::count(canvas.pixels(), rgba(draw_color)) == 0,
                "an off-canvas zero-radius disc must not collapse onto the edge");

        canvas.disc(canvas_width - 1, canvas_height - 1, 0, draw_color);
        require(
            canvas.pixels().back() == rgba(draw_color),
            "an on-canvas zero-radius disc must draw exactly its center");
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] cpu_canvas: " << error.what() << '\n';
        return 1;
    }
    std::cout << "[PASS] cpu_canvas\n";
    return 0;
}
