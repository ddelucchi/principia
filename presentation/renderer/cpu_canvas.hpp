#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace principia::presentation {

inline constexpr int canvas_width = 320;
inline constexpr int canvas_height = 180;

struct Color {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

class CpuCanvas {
public:
    CpuCanvas();

    void clear(Color color);
    void pixel(int x, int y, Color color);
    void rectangle(int x0, int y0, int x1, int y1, Color color);
    void line(int x0, int y0, int x1, int y1, Color color);
    void disc(int center_x, int center_y, int radius, Color color);

    [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept;

private:
    std::vector<std::uint32_t> pixels_;
};

}  // namespace principia::presentation
